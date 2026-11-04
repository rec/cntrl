#pragma once
#include <cstdint>

namespace cntrl {
struct UtilityPageSettings {
    enum Word : unsigned {
        refresh, clock_timeout, clocks_per_quarter, clock_beats,
        tap_intervals, minimum_taps, minimum_interval, reset_gap, flash_fraction,
        diameter, annulus, title_height, tempo_height, padding, tempo_font, status_font,
        off_color, on_color, ring_color, digits, decimals, background, title_font, count
    };
    uint16_t words[count] = {};
    uint16_t operator[](Word word) const { return words[word]; }
};
} // namespace cntrl
