#pragma once
#include "filesystem.h"
#include "binary_format.h"
#include <cstring>
#include <cstdio>
#include <limits>

namespace cntrl {
// Sparse device preferences. Compiled setup bytes remain their authored fallback.
class GlobalSettings {
public:
    static constexpr unsigned count = 34;
    static constexpr uint64_t fields = ((uint64_t(1) << count) - 1) & ~(uint64_t(1) << 27);
    struct Values {
        uint64_t known = 0;
        uint32_t values[count] = {};
        char language[16] = {};
        char startup_set[64] = {};
    };
    explicit GlobalSettings(files::FileSystem &filesystem) : filesystem_(filesystem) {}
    const Values &values() const { return values_; }
    bool ready() const { return ready_; }
    static uint32_t resolve(const Values *values, uint64_t explicit_fields, unsigned field, uint32_t fallback) {
        return values && field < count && !(explicit_fields & (uint64_t(1) << field))
            && (values->known & (uint64_t(1) << field)) ? values->values[field] : fallback;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(global_settings_valid)
#endif
    static bool valid(unsigned field, uint32_t value) {
        if (field >= count || field == 27) return false;
        if (field == 0) return value <= 255;
        if (field == 1) return value <= 2147483;
        if (field == 7) return value <= 2;
        if (field == 11 || field == 28 || field == 32) return value <= 65535;
        if (field == 12 || field == 13 || field == 17 || field == 18) return value && value <= 65535;
        if (field >= 14 && field <= 16) return value <= 3;
        if (field >= 19 && field <= 22) return value <= 4;
        if (field == 29) return value >= 2 && value <= 65535;
        if (field == 30) return value <= 128;
        if (field == 31) return value && value <= 128;
        return value <= 1;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(global_settings_values_valid)
#endif
    static bool valid(const Values &values) {
        if (values.known & ~fields) return false;
        if (!std::memchr(values.language, 0, sizeof(values.language)) || !language_valid(values.language)) return false;
        if (!std::memchr(values.startup_set, 0, sizeof(values.startup_set))) return false;
        for (const char *p = values.startup_set; *p; ++p) if (*p < 32 || *p > 126) return false;
        for (unsigned i = 0; i < count; ++i)
            if ((values.known & (uint64_t(1) << i)) ? !valid(i, values.values[i]) : values.values[i] != 0) return false;
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(global_settings_language_valid)
#endif
    static bool language_valid(const char *tag) {
        if (!tag) return false;
        if (!tag[0]) return true;
        unsigned length = 0, part = 0; bool first = true;
        for (const char *p = tag; *p; ++p) {
            if (++length > 15) return false;
            if (*p == '-') {
                if (part < 2 || part > (first ? 3u : 8u)) return false;
                first = false; part = 0; continue;
            }
            const bool lower = *p >= 'a' && *p <= 'z';
            if (first ? !lower : !(lower || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9'))) return false;
            ++part;
        }
        return part >= 2 && part <= (first ? 3u : 8u);
    }
    static bool reload(unsigned field) { return field > 2; }
    bool candidate(unsigned field, uint32_t value, bool clear, Values &result) const {
        if (field >= count || (!clear && !valid(field, value))) return false;
        result = values_;
        if (clear) { result.known &= ~(uint64_t(1) << field); result.values[field] = 0; }
        else { result.known |= uint64_t(1) << field; result.values[field] = value; }
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(global_settings_boot)
#endif
    bool boot() {
        ready_ = false; values_ = Values(); sequence_ = 0; slot_ = -1;
        bool present = false;
        for (unsigned slot = 0; slot < 2; ++slot) {
            char path[32]; filename(slot, path);
            if (!filesystem_.exists(path)) continue;
            present = true; Values values; uint32_t sequence;
            if (!read(slot, values, sequence)) continue;
            if (slot_ < 0 || sequence > sequence_) { values_ = values; sequence_ = sequence; slot_ = int(slot); }
        }
        ready_ = !present || slot_ >= 0; return ready_;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(global_settings_commit)
#endif
    bool commit(const Values &values) {
        if (!ready_ || !valid(values) || sequence_ == std::numeric_limits<uint32_t>::max()) return false;
        if (!filesystem_.exists("/cntrl/global") && !filesystem_.make_directory("/cntrl/global")) return false;
        uint8_t bytes[record_size] = {};
        std::memcpy(bytes, "CGLO", 4); bytes[4] = 3;
        binary::write32(bytes + 8, sequence_ + 1);
        for (unsigned i = 0; i < 8; ++i) bytes[12 + i] = uint8_t(values.known >> (8 * i));
        for (unsigned i = 0; i < count; ++i) binary::write32(bytes + 20 + i * 4, values.values[i]);
        std::memcpy(bytes + language_offset, values.language, sizeof(values.language));
        std::memcpy(bytes + startup_offset, values.startup_set, sizeof(values.startup_set));
        binary::write32(bytes + record_size - 8, binary::crc(bytes, record_size - 8));
        std::memcpy(bytes + record_size - 4, "DONE", 4);
        const unsigned slot = slot_ == 0 ? 1 : 0; char path[32]; filename(slot, path);
        auto file = filesystem_.file();
        if (!file || !file->open(path, files::Mode::create) || !file->write(0, bytes, sizeof(bytes)) || !file->sync()) return false;
        file->close(); Values checked; uint32_t sequence;
        if (!read(slot, checked, sequence) || sequence != sequence_ + 1 || checked.known != values.known
            || std::memcmp(checked.values, values.values, sizeof(values.values)) || std::strcmp(checked.language, values.language)
            || std::strcmp(checked.startup_set, values.startup_set)) return false;
        values_ = values; ++sequence_; slot_ = int(slot); return true;
    }
    static constexpr unsigned language_offset = 20 + count * 4;
    static constexpr unsigned old_record_size = language_offset + 8;
    static constexpr unsigned language_record_size = language_offset + 16 + 8;
    static constexpr unsigned startup_offset = language_offset + 16;
    static constexpr unsigned record_size = startup_offset + 64 + 8;
private:
    friend class Documents;
    files::FileSystem &filesystem_;
    Values values_;
    uint32_t sequence_ = 0;
    int slot_ = -1;
    bool ready_ = false;
    static void filename(unsigned slot, char (&path)[32]) { std::snprintf(path, sizeof(path), "/cntrl/global/%u.bin", slot); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(global_settings_read)
#endif
    bool read(unsigned slot, Values &values, uint32_t &sequence) {
        char path[32]; filename(slot, path); auto file = filesystem_.file(); uint8_t bytes[record_size];
        if (!file || !file->open(path, files::Mode::read)) return false;
        const unsigned size = file->size();
        if ((size != old_record_size && size != language_record_size && size != record_size) || !file->read(0, bytes, size)) return false;
        file->close();
        if (std::memcmp(bytes, "CGLO", 4) || bytes[4] != (size == old_record_size ? 1 : size == language_record_size ? 2 : 3) || bytes[5] || bytes[6] || bytes[7]
            || std::memcmp(bytes + size - 4, "DONE", 4)
            || binary::read32(bytes + size - 8) != binary::crc(bytes, size - 8)) return false;
        sequence = binary::read32(bytes + 8); if (!sequence) return false;
        values = Values();
        for (unsigned i = 0; i < 8; ++i) values.known |= uint64_t(bytes[12 + i]) << (8 * i);
        for (unsigned i = 0; i < count; ++i) values.values[i] = binary::read32(bytes + 20 + i * 4);
        if (size != old_record_size) std::memcpy(values.language, bytes + language_offset, sizeof(values.language));
        if (size == record_size) std::memcpy(values.startup_set, bytes + startup_offset, sizeof(values.startup_set));
        return valid(values);
    }
};
} // namespace cntrl
