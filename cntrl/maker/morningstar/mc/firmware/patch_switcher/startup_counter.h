#pragma once
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>

class StartupCounter {
public:
    enum class Status { unavailable, ready, error };
    static constexpr const char *path = "/cntrl/starts.toml";
    static constexpr unsigned record_size = 512;
    uint64_t count = 0;
    Status status = Status::unavailable;

    template<class File>
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(startup_counter_start)
#endif
    void start(File &file) {
        count = 0; status = Status::error;
        if (!file) return;
        char record[record_size + 1] = {};
        int latest = -1;
        for (unsigned slot = 0; slot < 2; ++slot) {
            uint64_t saved = 0;
            if (!file.seekSet(slot * record_size) || file.read(record, record_size) != record_size
                || !decode(record, slot, saved)) continue;
            if (latest < 0 || saved > count) { count = saved; latest = int(slot); }
        }
        if ((latest < 0 && file.size()) || count == INT64_MAX) return;
        unsigned slot = latest < 0 ? 0 : unsigned(latest) ^ 1;
        uint64_t next = count + 1;
        std::memset(record, ' ', record_size);
        int length = std::snprintf(record, sizeof(record), "[slot%u]\nstarts = %llu\ncheck = \"%016llx\"\n",
            slot, (unsigned long long)next, (unsigned long long)~next);
        if (length < 0 || unsigned(length) >= record_size) return;
        record[length] = ' '; record[record_size - 1] = '\n';
        // Each copy occupies its own card sector. Never truncate the older one.
        char verify[record_size];
        if (!file.seekSet(slot * record_size) || file.write(record, record_size) != record_size
            || !file.sync() || !file.seekSet(slot * record_size)
            || file.read(verify, record_size) != record_size
            || std::memcmp(record, verify, record_size)) return;
        count = next; status = Status::ready;
    }
private:
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(startup_counter_decode)
#endif
    static bool decode(const char *record, unsigned slot, uint64_t &count) {
        char heading[32];
        int length = std::snprintf(heading, sizeof(heading), "[slot%u]\nstarts = ", slot);
        if (std::memcmp(record, heading, unsigned(length))) return false;
        const char *end = record + record_size;
        auto number = std::from_chars(record + length, end, count);
        constexpr char separator[] = "\ncheck = \"";
        if (number.ec != std::errc{} || !count || count > INT64_MAX
            || end - number.ptr < int(sizeof(separator) - 1 + 18)
            || std::memcmp(number.ptr, separator, sizeof(separator) - 1)) return false;
        const char *hex = number.ptr + sizeof(separator) - 1;
        uint64_t check = 0;
        auto parsed = std::from_chars(hex, hex + 16, check, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != hex + 16 || check != ~count
            || hex[16] != '"' || hex[17] != '\n') return false;
        for (const char *p = hex + 18; p < end; ++p) if (*p != ' ' && *p != '\n') return false;
        return true;
    }
};
