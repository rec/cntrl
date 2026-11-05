#pragma once
#include <cstdint>
#include <cstring>
#include "expression.h"
#include "keyboard.h"

namespace cntrl {
struct UtilityPageSettings {
    enum Word : unsigned {
        refresh, clock_timeout, clocks_per_quarter, clock_beats,
        tap_intervals, minimum_taps, minimum_interval, reset_gap, flash_fraction,
        diameter, annulus, title_height, tempo_height, padding, tempo_font, status_font,
        off_color, on_color, ring_color, digits, decimals, background, title_font, clock_outputs, clock_external, slot1, slot2, slot3, slot4, count
    };
    uint16_t words[count] = {};
    ExpressionAssignment expression[4] = {};
    const uint8_t *keyboard = nullptr;
    unsigned keyboard_count = 0;
    KeyboardShortcut keyboard_shortcut(unsigned index) const {
        KeyboardShortcut result;
        if (index < keyboard_count) {
            const uint8_t *p = keyboard + index * 39;
            std::memcpy(result.name, p, 32);
            result.modifiers = p[32];
            std::memcpy(result.keys, p + 33, 6);
        }
        return result;
    }
    uint16_t operator[](Word word) const { return words[word]; }
};
} // namespace cntrl
