#pragma once
#include "hardware.h"
#include <cstring>

namespace cntrl {
// Layout is portable. The target supplies panel dimensions and font pixels.
inline bool text_pixel(const Pane &pane, unsigned width, unsigned height,
                       unsigned x, unsigned y, const unsigned char *font) {
    if (width < 12 || !height || x >= width || y >= height) return false;
    const char *fields[] = {pane.title, pane.instrument, pane.category, pane.patch};
    unsigned top = 0;
    for (unsigned f = 0; f < 4; ++f) {
        unsigned scale = f == 3 ? 2 : 1;
        unsigned columns = width / (6 * scale);
        unsigned length = std::strlen(fields[f]);
        unsigned lines = length ? (length + columns - 1) / columns : 1;
        if (top + lines * 8 * scale > height) {
            scale = 1; columns = width / 6;
            lines = length ? (length + columns - 1) / columns : 1;
        }
        if (y >= top && y < top + lines * 8 * scale) {
            unsigned line = (y - top) / (8 * scale);
            unsigned index = line * columns + x / (6 * scale);
            unsigned column = (x / scale) % 6, row = ((y - top) / scale) % 8;
            return index < length && x / (6 * scale) < columns && column < 5 && row < 7
                && (font[uint8_t(fields[f][index]) * 5 + column] & (1u << row));
        }
        top += lines * 8 * scale + 2;
    }
    return false;
}
} // namespace cntrl
