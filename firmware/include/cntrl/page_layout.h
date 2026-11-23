#pragma once
#include <cstdint>
#include <algorithm>
#include "checked_array.h"

namespace cntrl {
// Named page records belong to the active document. Display IDs are in physical order.
struct PageLayout {
    // The layout header stores page counts in one byte.
    static constexpr unsigned max_pages = UINT8_MAX, max_controllers = max_pages, patch_chooser = 11, midi_monitor = 12, midi_looper = 13, arpeggiator = 14, banked = 15, pong = 16, language = 17, time = 19, calibrate = 20, doom = 21, setups = 22, songs = 23, switches = 24;
    const uint8_t *records = nullptr;
    unsigned pages() const { return records ? records[4] : 0; }
    unsigned displays() const { return records ? records[5] : 0; }
    unsigned regions() const { return records ? records[7] : 0; }
    bool left_wins() const { return records && records[8]; }
    unsigned initial_focus() const { return records ? records[9] : 0; }
    unsigned length() const { return 12 + displays() * 32 + pages() * (35 + displays()) + regions() * 34 + (records ? records[10] : 0); }
    const uint8_t *navigation() const { return records && records[10] ? records + length() - 4 : nullptr; }
    const char *display_name(unsigned index) const { return reinterpret_cast<const char *>(records + 12 + index * 32); }
    const uint8_t *record(unsigned index) const { return records + 12 + displays() * 32 + index * (34 + displays()); }
    const char *name(unsigned index) const { return reinterpret_cast<const char *>(record(index)); }
    const uint8_t *page(unsigned index) const { return record(index) + 32; }
    unsigned controllers() const {
        unsigned count = 0;
        for (unsigned i = 0; i < pages(); ++i) count += page(i)[0] == patch_chooser;
        return count;
    }
    unsigned controller_page(unsigned slot) const {
        for (unsigned i = 0; i < pages(); ++i) if (page(i)[0] == patch_chooser && !slot--) return i;
        return pages();
    }
    unsigned controller_slot(unsigned index) const {
        unsigned slot = 0;
        for (unsigned i = 0; i < index; ++i) slot += page(i)[0] == patch_chooser;
        return slot;
    }
    const uint8_t *region(unsigned index) const { return records + 12 + displays() * 32 + pages() * (34 + displays()) + index * 34; }
    const char *region_name(unsigned index) const { return reinterpret_cast<const char *>(region(index)); }
    unsigned region_size(unsigned index) const { return region(index)[33]; }
    unsigned region_page(unsigned index,unsigned position) const { return region(regions())[region(index)[32] + position]; }
    unsigned ordered_page(unsigned position) const { return region(regions())[position]; }
    unsigned page_region(unsigned index) const {
        for (unsigned r = 0; r < regions(); ++r) for (unsigned p = 0; p < region_size(r); ++p)
            if (region_page(r,p) == index) return r;
        return regions();
    }
    static unsigned maximum_displays(unsigned type,unsigned available) {
        return type == 10 ? 0 : type == patch_chooser ? available : type == banked || type == pong ? std::min(3u, available) : std::min(type == language || type == time ? 2u : 1u,available);
    }
    unsigned minimum_displays(unsigned index) const {
        const auto *p = page(index);
        // Preserve stored records while overriding old tool-page display claims.
        return p[0] == 10 ? 0 : p[0] == language || p[0] == time ? 2 : p[0] == patch_chooser || p[0] == banked || p[0] == pong ? p[1] : 1;
    }
    static bool valid_name(const uint8_t *name) {
        if (!name[0]) return false;
        for (unsigned i = 0; i < 32; ++i) {
            if (!name[i]) return true;
            if (name[i] < 32 || name[i] > 126) return false;
        }
        return false;
    }
};

class PageAllocation {
public:
    struct Region { unsigned selected = 0, first = 0, count = 0; };
    PageLayout layout;
    bool prepare(PageLayout value) { pending_.release(); return pending_.resize(value.regions()); }
    void finish(bool commit) { if (commit) regions_.swap(pending_); pending_.release(); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_allocation_configure)
#endif
    void configure(PageLayout value) {
        layout = value; focus_ = layout.initial_focus();
        if (!layout.regions()) regions_.release();
        for (unsigned r = 0; r < layout.regions(); ++r) regions_[r].selected = layout.region_page(r,0);
        allocate();
    }
    const Region &region(unsigned index) const { return regions_[index]; }
    unsigned focus() const { return focus_; }
    unsigned selected() const { return regions_.empty() ? layout.pages() : regions_[focus_].selected; }
    void focus(unsigned index) { if (index < regions_.size()) focus_ = index; }
    void select(unsigned page) {
        unsigned r = layout.page_region(page);
        if (r == layout.regions()) return;
        focus_ = r; regions_[r].selected = page; allocate();
    }
    bool controller_visible(unsigned slot) const {
        unsigned page = layout.controller_page(slot);
        for (const auto &r : regions_) if (r.selected == page && r.count) return true;
        return false;
    }
    int controller_display(unsigned slot,unsigned logical) const {
        unsigned index = layout.controller_page(slot), r = layout.page_region(index);
        if (index == layout.pages() || r == layout.regions() || regions_[r].selected != index) return -1;
        const auto &region = regions_[r];
        unsigned visible = region.count;
        // The sent-patch pane is the sole view when only one screen is available.
        if (visible == 1) return logical == 1 ? int(region.first) : -1;
        if (logical >= layout.displays()) return -1;
        const auto *p = layout.page(index);
        bool removed[256] = {};
        for (unsigned i = 0; i < layout.displays() - visible; ++i) removed[p[2 + i]] = true;
        if (removed[logical]) return -1;
        unsigned physical = region.first;
        for (unsigned i = 0; i < logical; ++i) if (!removed[i]) ++physical;
        return int(physical);
    }
private:
    CheckedArray<Region> regions_, pending_;
    unsigned focus_ = 0;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_allocation_allocate)
#endif
    void allocate() {
        unsigned spare = layout.displays();
        for (auto &r : regions_) { r.count = layout.minimum_displays(r.selected); spare -= r.count; }
        for (unsigned i = 0; i < regions_.size(); ++i) {
            auto &r = regions_[layout.left_wins() ? i : regions_.size() - i - 1];
            unsigned extra = std::min(spare,PageLayout::maximum_displays(layout.page(r.selected)[0],layout.displays()) - r.count);
            r.count += extra; spare -= extra;
        }
        unsigned first = 0;
        for (auto &r : regions_) { r.first = first; first += r.count; }
    }
};
}
