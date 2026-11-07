#pragma once
#include <cstdint>

namespace cntrl {
// Records are owned by the active document. Windows are numbered left to right.
struct AppLayout {
    static constexpr unsigned max_apps = 4;
    const uint8_t *records = nullptr;
    unsigned apps() const { return records ? records[4] : 0; }
    unsigned pages() const { return records ? records[5] : 0; }
    unsigned windows() const { return records ? records[6] : 0; }
    unsigned length() const { return 8 + windows() * 32 + apps() * (32 + windows()) + pages() * 2; }
    const char *window_name(unsigned index) const { return reinterpret_cast<const char *>(records + 8 + index * 32); }
    const char *app_name(unsigned index) const {
        return reinterpret_cast<const char *>(records + 8 + windows() * 32 + index * (32 + windows()));
    }
    const uint8_t *page(unsigned index) const { return reinterpret_cast<const uint8_t *>(app_name(apps())) + index * 2; }
    int app_window(unsigned app, unsigned logical, unsigned occupied) const {
        if (app >= apps() || logical >= windows() || occupied > windows()) return -1;
        const auto *order = reinterpret_cast<const uint8_t *>(app_name(app)) + 32;
        bool removed[256] = {};
        for (unsigned i = 0; i < occupied; ++i) removed[order[i]] = true;
        if (removed[logical]) return -1;
        unsigned physical = 0;
        for (unsigned i = 0; i < logical; ++i) if (!removed[i]) ++physical;
        return int(physical);
    }
};
}
