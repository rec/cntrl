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
        if (!parse(text, parsed)) return false;
        entries_.swap(parsed);
        return true;
    }

    ExpressionCalibration calibration(unsigned jack) const { return entries_[jack].calibration; }
    void import(unsigned jack, ExpressionCalibration value) { entries_[jack].calibration = value; }

    bool save(unsigned jack, ExpressionCalibration value) {
        std::string text;
        for (unsigned i = 0; i < entries_.size(); ++i) {
            const auto &entry = entries_[i];
            const auto calibration = i == jack ? value : entry.calibration;
            if (!calibration.valid() && entry.name.empty()) continue;
            text += "[configurable-" + std::to_string(i + 1) + "]\n";
            if (!entry.name.empty()) text += "name = " + entry.name + '\n';
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
        entries_[jack].calibration = value;
        return true;
    }

private:
    struct Entry {
        ExpressionCalibration calibration{};
        // Keep the TOML string spelling, including escapes, when updating endpoints.
        std::string name;
        unsigned fields = 0;
    };
    static constexpr const char *temporary = "/cntrl/calibration.tmp", *previous = "/cntrl/calibration.prev";
    static constexpr unsigned maximum = 4096;
    files::FileSystem &filesystem_;
    std::vector<Entry> entries_;

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

    static bool parse(std::string_view text, std::vector<Entry> &entries) {
        Entry *entry = nullptr;
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
            if (!entry || equals == line.npos) return false;
            const auto key = trim(line.substr(0, equals));
            const auto value = trim(line.substr(equals + 1));
            if (key == "name") {
                if (!entry->name.empty() || value.empty() || (value.front() != '"' && value.front() != '\'')) return false;
                const char quote = value.front();
                unsigned i = 1;
                for (; i < value.size() && value[i] != quote; ++i)
                    if (quote == '"' && value[i] == '\\') ++i;
                if (i >= value.size()) return false;
                const auto tail = trim(value.substr(i + 1));
                if (!tail.empty() && tail.front() != '#') return false;
                entry->name = value.substr(0, i + 1);
                continue;
            }
            const unsigned field = key == "heel" ? 2 : key == "toe" ? 4 : 0;
            unsigned raw;
            if (!field || (entry->fields & field)
                || !number(trim(value.substr(0, value.find('#'))), raw) || raw > 1023) return false;
            if (field == 2) entry->calibration.heel = uint16_t(raw);
            else entry->calibration.toe = uint16_t(raw);
            entry->fields |= field;
        }
        for (const auto &value : entries) {
            const auto endpoints = value.fields & 6;
            if (endpoints && (endpoints != 6 || !value.calibration.valid())) return false;
        }
        return true;
    }
};

} // namespace cntrl
