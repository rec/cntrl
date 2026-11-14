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
    const uint8_t *find(unsigned page) const {
        if (!data) return nullptr;
        for (unsigned i = 0; i < count(); ++i) if (u16(data + 10 + i * 4) == page) return data + 10 + i * 4;
        return nullptr;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(banked_pages_valid)
#endif
    static bool valid(const uint8_t *data, unsigned available, PageLayout layout) {
        if (available < 10 || u32(data + 4) != 10u + 4u * u16(data + 8) || u32(data + 4) > available) return false;
        const uint8_t *p = data + 10;
        bool seen[256] = {};
        for (unsigned i = 0; i < u16(data + 8); ++i, p += 4) {
            if (u16(p) >= layout.pages() || layout.page(u16(p))[0] != PageLayout::banked || seen[u16(p)]) return false;
            seen[u16(p)] = true;
            for (unsigned j = 0; j < i; ++j) if (u16(data + 12 + j * 4) == u16(p + 2)) return false;
        }
        for (unsigned i = 0; i < layout.pages(); ++i) if ((layout.page(i)[0] == 15) != seen[i]) return false;
        return true;
    }
};
}
