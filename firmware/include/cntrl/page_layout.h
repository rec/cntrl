#pragma once
#include <cstdint>
#include <algorithm>

namespace cntrl {
// Named page records belong to the active document. Display IDs are in physical order.
struct PageLayout {
    static constexpr unsigned max_controllers = 4, max_pages = 36, patch_switcher = 11, midi_monitor = 12, midi_looper = 13;
    const uint8_t *records = nullptr;
    unsigned pages() const { return records ? records[4] : 0; }
    unsigned windows() const { return records ? records[5] : 0; }
    unsigned length() const { return 8 + windows() * 32 + pages() * (34 + windows()); }
    const char *window_name(unsigned index) const { return reinterpret_cast<const char *>(records + 8 + index * 32); }
    const uint8_t *record(unsigned index) const { return records + 8 + windows() * 32 + index * (34 + windows()); }
    const char *name(unsigned index) const { return reinterpret_cast<const char *>(record(index)); }
    const uint8_t *page(unsigned index) const { return record(index) + 32; }
    unsigned controllers() const {
        unsigned count = 0;
        for (unsigned i = 0; i < pages(); ++i) count += page(i)[0] == patch_switcher;
        return count;
    }
    unsigned controller_page(unsigned slot) const {
        for (unsigned i = 0; i < pages(); ++i) if (page(i)[0] == patch_switcher && !slot--) return i;
        return pages();
    }
    unsigned controller_slot(unsigned index) const {
        unsigned slot = 0;
        for (unsigned i = 0; i < index; ++i) slot += page(i)[0] == patch_switcher;
        return slot;
    }
    int controller_window(unsigned slot, unsigned logical, unsigned occupied) const {
        const unsigned index = controller_page(slot);
        if (index == pages() || logical >= windows() || occupied > windows()) return -1;
        const auto *p = page(index);
        unsigned visible = std::min(unsigned(p[1]), windows() - occupied);
        bool removed[256] = {};
        for (unsigned i = 0; i < windows() - visible; ++i) removed[p[2 + i]] = true;
        if (removed[logical]) return -1;
        unsigned physical = 0;
        for (unsigned i = 0; i < logical; ++i) if (!removed[i]) ++physical;
        return int(physical);
    }
};
}
