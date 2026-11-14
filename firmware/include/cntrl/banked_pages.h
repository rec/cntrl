#pragma once
#include "page_layout.h"
#include "numeric.h"
#include "memory.h"

namespace cntrl {
struct BankedPages {
    const uint8_t *data = nullptr;
    unsigned count() const { return data ? u16(data + 8) : 0; }
    static const uint8_t *bank(const uint8_t *record, unsigned index) {
        const uint8_t *p = record + 8;
        while (index--) p += 2 + 2 * u16(p);
        return p;
    }
    static const uint8_t *next(const uint8_t *record) { return bank(record, u16(record + 2)) + 5 * u16(record + 6); }
    const uint8_t *find(unsigned page) const {
        if (!data) return nullptr;
        const uint8_t *p = data + 10;
        for (unsigned i = 0; i < count(); ++i) { if (u16(p) == page) return p; p = next(p); }
        return nullptr;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(banked_pages_valid)
#endif
    static bool valid(const uint8_t *data, unsigned available, PageLayout layout) {
        if (available < 10 || u32(data + 4) < 10 || u32(data + 4) > available) return false;
        const uint8_t *p = data + 10, *end = data + u32(data + 4);
        bool seen[256] = {};
        for (unsigned i = 0; i < u16(data + 8); ++i) {
            if (end - p < 8 || u16(p) >= layout.pages() || layout.page(u16(p))[0] != 15 || seen[u16(p)]
                || !u16(p + 2) || u16(p + 2) > 128 || u16(p + 4) >= u16(p + 2) || u16(p + 6) > 32) return false;
            seen[u16(p)] = true;
            unsigned banks = u16(p + 2), chords = u16(p + 6); p += 8;
            for (unsigned j = 0; j < banks; ++j) {
                if (end - p < 2 || !u16(p) || u16(p) > 4 || unsigned(end - p) < 2u + 2u * u16(p)) return false;
                p += 2 + 2 * u16(p);
            }
            if (unsigned(end - p) < 5 * chords) return false;
            for (unsigned j = 0; j < chords; ++j, p += 5)
                if ((p[0] != 9 && (p[0] < 14 || p[0] > 17)) || !u32(p + 1) || !(u32(p + 1) & (u32(p + 1) - 1))) return false;
        }
        for (unsigned i = 0; i < layout.pages(); ++i) if ((layout.page(i)[0] == 15) != seen[i]) return false;
        return p == end;
    }
};
}
