#pragma once
#include <cntrl/hardware.h>
#include <algorithm>
#include <vector>

struct Memory : cntrl::Storage {
    std::vector<uint8_t> slots[2];
    unsigned selected = 0, maximum_read = 0;
    int remaining = -1;
    unsigned size(unsigned slot) override { return unsigned(slots[slot].size()); }
    bool read(unsigned slot, unsigned offset, uint8_t *data, unsigned length) override {
        maximum_read = std::max(maximum_read, length);
        if (offset > slots[slot].size() || length > slots[slot].size() - offset) return false;
        std::copy_n(slots[slot].data() + offset, length, data); return true;
    }
    bool begin(unsigned slot) override { selected = slot; slots[slot].clear(); return true; }
    bool write(const uint8_t *data, unsigned length) override {
        unsigned amount = remaining < 0 ? length : std::min(length, unsigned(remaining));
        slots[selected].insert(slots[selected].end(), data, data + amount);
        if (remaining >= 0) remaining -= int(amount);
        return amount == length;
    }
    bool finish() override { return true; }
};
