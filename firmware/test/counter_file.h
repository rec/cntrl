#pragma once
#include <cntrl/startup_counter.h>
#include <algorithm>
#include <vector>

struct CounterFile : cntrl::files::File {
    std::vector<char> data;
    unsigned position = 0, writes = 0, limit = cntrl::StartupCounter::record_size;
    unsigned seeks = 0, fail_seek = 0, mismatch = cntrl::StartupCounter::record_size;
    int read_limit = cntrl::StartupCounter::record_size, verify_limit = cntrl::StartupCounter::record_size;
    bool active = true, writable = true, synced = true, truncatable = true;
    bool open(const char *, cntrl::files::Mode) override { active = true; return true; }
    void close() override { active = false; }
    bool opened() const override { return active; }
    uint32_t size() const override { return unsigned(data.size()); }
    int read_some(uint32_t offset, uint8_t *bytes, unsigned length) override {
        if (!position_at(offset)) return cntrl::files::position_error;
        int maximum = writes ? verify_limit : read_limit;
        if (maximum < 0) return maximum;
        length = std::min(length, unsigned(maximum));
        unsigned n = position < data.size() ? std::min(length, unsigned(data.size()) - position) : 0;
        if (n) std::memcpy(bytes, data.data() + position, n);
        if (writes && mismatch < n) bytes[mismatch] ^= 1;
        position += n; return int(n);
    }
    bool read(uint32_t offset, uint8_t *bytes, unsigned length) override { return read_some(offset, bytes, length) == int(length); }
    int write_some(uint32_t offset, const uint8_t *bytes, unsigned length) override {
        if (!position_at(offset)) return cntrl::files::position_error;
        ++writes; if (!writable) return 0;
        unsigned n = std::min(length, limit);
        if (position + n > data.size()) data.resize(position + n);
        std::memcpy(data.data() + position, bytes, n); position += n; return int(n);
    }
    bool write(uint32_t offset, const uint8_t *bytes, unsigned length) override { return write_some(offset, bytes, length) == int(length); }
    bool sync() override { return synced; }
    bool resize(uint32_t length) override {
        if (!truncatable) return false;
        data.resize(length); return true;
    }
private:
    bool position_at(unsigned offset) { position = offset; return active && ++seeks != fail_seek; }
};
