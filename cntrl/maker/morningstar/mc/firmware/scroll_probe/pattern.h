#pragma once
#include <cstdint>

#ifndef CNTRL_SCROLL_GAP
#define CNTRL_SCROLL_GAP 72
#endif

extern "C" const unsigned char glcdfont[];

namespace scroll_probe {
constexpr unsigned panel_width = 160, height = 128, image_width = 1280;
constexpr unsigned gap_width = CNTRL_SCROLL_GAP;

constexpr unsigned panel_index(unsigned slot, bool reverse) {
    return reverse ? 2 - slot : slot;
}

constexpr unsigned panel_origin(unsigned panel, unsigned gap) {
    return panel * (panel_width + gap);
}

constexpr unsigned next_stride(unsigned stride) {
    return stride == 32 ? 1 : stride * 2;
}

constexpr unsigned next_rate(unsigned rate) {
    return rate == 30 ? 60 : rate - 10;
}

constexpr unsigned update_interval(unsigned rate) {
    return (1000000 + rate - 1) / rate;
}

constexpr unsigned start_line(unsigned offset) {
    return 1 + (panel_width - offset % panel_width) % panel_width;
}
constexpr unsigned entering_world_x(unsigned panel, unsigned offset, unsigned gap) {
    return (panel_origin(panel, gap) + offset + panel_width - 1) % image_width;
}

inline uint16_t pixel(unsigned world_x, unsigned y) {
    world_x %= image_width;
    unsigned tile = world_x / 32, x = world_x % 32;
    if (x == 0 || y % 32 == 0) return 0xffff;
    // Use the installed library's classic font, enlarged twofold: tiles 00..39.
    if (x >= 4 && x < 28 && y % 64 >= 24 && y % 64 < 40) {
        unsigned character = (x - 4) / 12;
        unsigned column = ((x - 4) % 12) / 2;
        unsigned digit = character ? tile % 10 : tile / 10;
        if (column < 5 && (glcdfont[('0' + digit) * 5 + column]
            & (1u << ((y % 64 - 24) / 2)))) return 0xffff;
    }
    if ((world_x + y) % 32 < 2) return 0;
    constexpr uint16_t colors[] = {0x8000, 0x0400, 0x0010, 0x8400, 0x8010, 0x0410};
    return colors[tile % 6];
}

inline void column(unsigned world_x, uint8_t *pixels) {
    for (unsigned y = 0; y < height; ++y) {
        uint16_t color = pixel(world_x, y);
        pixels[2 * y] = uint8_t(color >> 8);
        pixels[2 * y + 1] = uint8_t(color);
    }
}
} // namespace scroll_probe
