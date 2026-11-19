#pragma once
#include <cstdint>
#include <cstring>

namespace cntrl::ui {
enum class StartsStatus { unavailable, ready, error };

struct PerformanceStatus {
    uint32_t ram_used[2] = {}, ram_free[2] = {};
    uint64_t disk_used = 0, disk_free = 0;
    bool disk_valid = false;
    uint64_t starts = 0;
    StartsStatus starts_status = StartsStatus::unavailable;
    char starts_error[32] = {};
    unsigned cycles = 0, patches = 0;
};

struct StatusSnapshot {
    static constexpr unsigned wire_size = 111;
    PerformanceStatus performance;
    uint64_t uptime_ms = 0;
    uint32_t git_commit = 0;
    uint32_t tap_period_ms = 0;
    uint64_t clock_period_us = 0;
    bool clock_active = false;

    void encode(uint8_t *bytes) const {
        std::memcpy(bytes, "CSS1", 4);
        unsigned offset = 4;
        auto put = [&](uint64_t value, unsigned size) {
            for (unsigned i = 0; i < size; ++i) bytes[offset++] = uint8_t(value >> (8 * i));
        };
        for (unsigned i = 0; i < 2; ++i) { put(performance.ram_used[i], 4); put(performance.ram_free[i], 4); }
        put(performance.disk_used, 8); put(performance.disk_free, 8);
        put(performance.starts, 8); put(uptime_ms, 8); put(git_commit, 4);
        put(performance.cycles, 4); put(performance.patches, 4);
        put(performance.disk_valid, 1); put(unsigned(performance.starts_status), 1);
        std::memcpy(bytes + offset, performance.starts_error, sizeof(performance.starts_error));
        offset += sizeof(performance.starts_error);
        put(tap_period_ms, 4); put(clock_period_us, 8); put(clock_active, 1);
    }

    bool decode(const uint8_t *bytes, unsigned length) {
        if (length != wire_size || std::memcmp(bytes, "CSS1", 4)
            || bytes[64] > 1 || bytes[65] > unsigned(StartsStatus::error) || bytes[97] || bytes[110] > 1) return false;
        unsigned offset = 4;
        auto get = [&](unsigned size) {
            uint64_t value = 0;
            for (unsigned i = 0; i < size; ++i) value |= uint64_t(bytes[offset++]) << (8 * i);
            return value;
        };
        for (unsigned i = 0; i < 2; ++i) { performance.ram_used[i] = uint32_t(get(4)); performance.ram_free[i] = uint32_t(get(4)); }
        performance.disk_used = get(8); performance.disk_free = get(8);
        performance.starts = get(8); uptime_ms = get(8); git_commit = uint32_t(get(4));
        performance.cycles = unsigned(get(4)); performance.patches = unsigned(get(4));
        performance.disk_valid = get(1); performance.starts_status = StartsStatus(get(1));
        std::memcpy(performance.starts_error, bytes + offset, sizeof(performance.starts_error));
        offset += sizeof(performance.starts_error);
        tap_period_ms = uint32_t(get(4)); clock_period_us = get(8); clock_active = get(1);
        return true;
    }
};
} // namespace cntrl::ui
