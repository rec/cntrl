#pragma once
#include <cstdint>

namespace cntrl {
// Records are owned by the active document. Windows are numbered left to right.
struct AppLayout {
    static constexpr unsigned max_apps = 4;
    const uint8_t *records = nullptr;
    unsigned apps() const { return records ? records[4] : 0; }
    unsigned pages() const { return records ? records[5] : 0; }
    const char *app_name(unsigned index) const {
        return reinterpret_cast<const char *>(records + 6 + index * 35);
    }
    const uint8_t *page(unsigned index) const { return records + 6 + apps() * 35 + index * 2; }
    int app_window(unsigned app, unsigned logical, unsigned occupied) const {
        if (app >= apps() || logical > 2 || occupied > 3) return -1;
        const uint8_t *order = records + 6 + app * 35 + 32;
        unsigned removed = 0;
        for (unsigned i = 0; i < occupied; ++i) removed |= 1u << order[i];
        if (removed & (1u << logical)) return -1;
        unsigned physical = 0;
        for (unsigned i = 0; i < logical; ++i) if (!(removed & (1u << i))) ++physical;
        return int(physical);
    }
};
}
