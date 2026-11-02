#pragma once
#include <cstdint>

namespace cntrl {
struct RightPageSettings {
    enum Word : unsigned {
        refresh, clock_timeout, clocks_per_quarter, clock_beats,
        tap_intervals, minimum_taps, minimum_interval, reset_gap, flash_duration,
        diameter, annulus, title_height, tempo_height, padding, tempo_font, status_font,
        off_color, on_color, ring_color, digits, decimals, background, count
    };
    uint16_t words[count] = {};
    uint16_t operator[](Word word) const { return words[word]; }
};
} // namespace cntrl
