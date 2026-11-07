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
    char error[32] = {};

    template<class File>
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(startup_counter_start)
#endif
    void start(File &file) {
        count = 0; status = Status::error; std::strcpy(error, "open");
        if (!file) return;
        error[0] = 0;
        char record[record_size + 1] = {};
        int latest = -1;
        for (unsigned slot = 0; slot < 2; ++slot) {
            uint64_t saved = 0;
            if (!file.seekSet(slot * record_size)) {
                if (!error[0]) std::snprintf(error, sizeof(error), "seek-read s%u", slot);
                continue;
            }
            int read = file.read(record, record_size);
            if (read != record_size) {
                if (!error[0]) std::snprintf(error, sizeof(error), "read s%u %d/512", slot, read);
                continue;
            }
            if (!decode(record, slot, saved)) {
                if (!error[0]) std::snprintf(error, sizeof(error), "decode s%u", slot);
                continue;
            }
            if (latest < 0 || saved > count) { count = saved; latest = int(slot); }
        }
        if (latest < 0 && file.size()) return;
        if (count == INT64_MAX) { std::strcpy(error, "limit"); return; }
        unsigned slot = latest < 0 ? 0 : unsigned(latest) ^ 1;
        uint64_t next = count + 1;
        std::memset(record, ' ', record_size);
        int length = std::snprintf(record, sizeof(record), "[slot%u]\nstarts = %llu\ncheck = \"%016llx\"\n",
            slot, (unsigned long long)next, (unsigned long long)~next);
        if (length < 0 || unsigned(length) >= record_size) { std::strcpy(error, "format"); return; }
        record[length] = ' '; record[record_size - 1] = '\n';
        // Each copy occupies its own card sector. Never truncate the older one.
        char verify[record_size];
        std::snprintf(error, sizeof(error), "seek-write s%u", slot);
        if (!file.seekSet(slot * record_size)) return;
        unsigned written = file.write(record, record_size);
        if (written != record_size) { std::snprintf(error, sizeof(error), "write s%u %u/512", slot, written); return; }
        std::snprintf(error, sizeof(error), "sync s%u", slot);
        if (!file.sync()) return;
        std::snprintf(error, sizeof(error), "seek-verify s%u", slot);
        if (!file.seekSet(slot * record_size)) return;
        int read = file.read(verify, record_size);
        if (read != record_size) { std::snprintf(error, sizeof(error), "readback s%u %d/512", slot, read); return; }
        for (unsigned i = 0; i < record_size; ++i) if (record[i] != verify[i]) {
            std::snprintf(error, sizeof(error), "mismatch s%u @%u", slot, i); return;
        }
        count = next; status = Status::ready; error[0] = 0;
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
