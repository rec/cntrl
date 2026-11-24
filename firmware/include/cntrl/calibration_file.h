#pragma once

#include "expression.h"
#include "filesystem.h"
#include <charconv>
#include <string>
#include <string_view>
#include <vector>

namespace cntrl {

class CalibrationFile {
public:
    static constexpr const char *path = "/cntrl/calibration.toml";
    static constexpr unsigned maximum_settle_ms = 60000;

    CalibrationFile(files::FileSystem &filesystem, unsigned count)
        : filesystem_(filesystem), entries_(count) {}

    bool load() {
        if (!filesystem_.exists(path) && filesystem_.exists(previous))
            if (!filesystem_.rename(previous, path)) return false;
        if (!filesystem_.exists(path)) return true;
        auto file = filesystem_.file();
        if (!file || !file->open(path, files::Mode::read) || file->size() > maximum) return false;
        std::string text(file->size(), '\0');
        if (!text.empty() && !file->read(0, reinterpret_cast<uint8_t *>(text.data()), text.size())) return false;
        std::vector<Entry> parsed(entries_.size());
        if (!parse(text, parsed, settle_ms_)) return false;
        entries_.swap(parsed);
        return true;
    }

    ExpressionCalibration calibration(unsigned jack) const { return entries_[jack].calibration; }
    const char *name(unsigned jack) const { return entries_[jack].label.c_str(); }
    unsigned settle_ms() const { return settle_ms_; }
    void import(unsigned jack, ExpressionCalibration value) { entries_[jack].calibration = value; }

#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(calibration_file_save)
#endif
    bool save(const std::vector<CalibrationCapture> *captures, unsigned settle_ms,
              unsigned renamed = unsigned(-1), const char *label = nullptr) {
        if (!settle_ms || settle_ms > maximum_settle_ms) return false;
        std::string name, decoded;
        if (label) {
            if (renamed >= entries_.size()) return false;
            if (*label) {
                name = '"';
                for (const char *p = label; *p; ++p) {
                    if (uint8_t(*p) < 32 || uint8_t(*p) == 127) return false;
                    if (*p == '"' || *p == '\\') name += '\\';
                    name += *p;
                }
                name += '"';
                decoded = label;
            }
        }
        std::string text = "bits = 12\nsettle_ms = " + std::to_string(settle_ms) + "\n\n";
        for (unsigned i = 0; i < entries_.size(); ++i) {
            const auto &entry = entries_[i];
            const auto calibration = captures && (*captures)[i].toe_ready ? (*captures)[i].value() : entry.calibration;
            const auto &encoded = label && i == renamed ? name : entry.name;
            if (!calibration.valid() && encoded.empty()) continue;
            text += "[configurable-" + std::to_string(i + 1) + "]\n";
            if (!encoded.empty()) text += "name = " + encoded + '\n';
            if (calibration.valid()) {
                text += "heel = " + std::to_string(calibration.heel) + '\n';
                text += "toe = " + std::to_string(calibration.toe) + '\n';
            }
            text += '\n';
        }
        if (text.size() > maximum) return false;
        auto file = filesystem_.file();
        if (!file || !file->open(temporary, files::Mode::truncate)) return false;
        bool written = file->write(0, reinterpret_cast<const uint8_t *>(text.data()), text.size()) && file->sync();
        file->close();
        std::string verified(text.size(), '\0');
        written = written && file->open(temporary, files::Mode::read) && file->size() == text.size()
            && file->read(0, reinterpret_cast<uint8_t *>(verified.data()), verified.size()) && verified == text;
        file->close();
        if (!written) return false;
        if (filesystem_.exists(previous)) {
            if (!filesystem_.exists(path)) {
                if (!filesystem_.rename(previous, path)) return false;
            } else if (!filesystem_.remove(previous)) return false;
        }
        const bool existing = filesystem_.exists(path);
        if (existing && !filesystem_.rename(path, previous)) return false;
        if (!filesystem_.rename(temporary, path)) {
            if (existing) filesystem_.rename(previous, path);
            return false;
        }
        if (existing) filesystem_.remove(previous);
        if (captures)
            for (unsigned i = 0; i < entries_.size(); ++i)
                if ((*captures)[i].toe_ready) entries_[i].calibration = (*captures)[i].value();
        if (label) {
            entries_[renamed].name.swap(name);
            entries_[renamed].label.swap(decoded);
        }
        settle_ms_ = settle_ms;
        return true;
    }

private:
    struct Entry {
        ExpressionCalibration calibration{};
        // Keep the TOML string spelling, including escapes, when updating endpoints.
        std::string name;
        std::string label;
        unsigned fields = 0;
    };
    static constexpr const char *temporary = "/cntrl/calibration.tmp", *previous = "/cntrl/calibration.prev";
    static constexpr unsigned maximum = 4096;
    files::FileSystem &filesystem_;
    std::vector<Entry> entries_;
    unsigned settle_ms_ = 2000;

    static std::string_view trim(std::string_view text) {
        const auto first = text.find_first_not_of(" \t\r");
        if (first == text.npos) return {};
        return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
    }

    static bool number(std::string_view text, unsigned &value) {
        if (text.empty()) return false;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    }

    static bool parse(std::string_view text, std::vector<Entry> &entries, unsigned &settle_ms) {
        Entry *entry = nullptr;
        unsigned bits = 10, delay = 2000;
        bool have_bits = false, have_delay = false;
        while (!text.empty()) {
            const auto end = text.find('\n');
            auto line = trim(text.substr(0, end));
            text.remove_prefix(end == text.npos ? text.size() : end + 1);
            if (line.empty() || line.front() == '#') continue;
            if (line.front() == '[') {
                line = trim(line.substr(0, line.find('#')));
                constexpr std::string_view prefix = "[configurable-";
                unsigned port;
                if (line.substr(0, prefix.size()) != prefix || line.back() != ']'
                    || !number(line.substr(prefix.size(), line.size() - prefix.size() - 1), port)
                    || !port || port > entries.size() || entries[port - 1].fields) return false;
                entry = &entries[port - 1];
                entry->fields = 1;
                continue;
            }
            const auto equals = line.find('=');
            if (equals == line.npos) return false;
            const auto key = trim(line.substr(0, equals));
            const auto value = trim(line.substr(equals + 1));
            if (!entry) {
                unsigned raw;
                if (!number(trim(value.substr(0, value.find('#'))), raw)) return false;
                if (key == "bits" && !have_bits && (raw == 10 || raw == 12)) { bits = raw; have_bits = true; }
                else if (key == "settle_ms" && !have_delay && raw && raw <= maximum_settle_ms) { delay = raw; have_delay = true; }
                else return false;
                continue;
            }
            if (key == "name") {
                if (!entry->name.empty() || value.empty() || (value.front() != '"' && value.front() != '\'')) return false;
                const char quote = value.front();
                unsigned i = 1;
                for (; i < value.size() && value[i] != quote; ++i) {
                    if (quote == '"' && value[i] == '\\') {
                        if (++i == value.size()) return false;
                        if (value[i] != '"' && value[i] != '\\') entry->label += '\\';
                    }
                    entry->label += value[i];
                }
                if (i >= value.size()) return false;
                const auto tail = trim(value.substr(i + 1));
                if (!tail.empty() && tail.front() != '#') return false;
                entry->name = value.substr(0, i + 1);
                continue;
            }
            const unsigned field = key == "heel" ? 2 : key == "toe" ? 4 : 0;
            unsigned raw;
            if (!field || (entry->fields & field)
                || !number(trim(value.substr(0, value.find('#'))), raw) || raw >= (1u << bits)) return false;
            if (bits == 10) raw = ExpressionCalibration::from_10_bit(raw);
            if (field == 2) entry->calibration.heel = uint16_t(raw);
            else entry->calibration.toe = uint16_t(raw);
            entry->fields |= field;
        }
        for (const auto &value : entries) {
            const auto endpoints = value.fields & 6;
            if (endpoints && (endpoints != 6 || !value.calibration.valid())) return false;
        }
        settle_ms = delay;
        return true;
    }
};

} // namespace cntrl
