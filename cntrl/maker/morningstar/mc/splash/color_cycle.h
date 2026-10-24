#pragma once
#include <cstdint>

namespace cntrl::splash {
struct Tint {
    unsigned red, green, blue; // Six-bit channels, 0..63.
};

inline Tint cycle_tint(uint32_t now) {
    unsigned phase = (now % 20000) * (6 * 63) / 20000;
    unsigned ramp = phase % 63;
    switch (phase / 63) {
    case 0: return {63, ramp, 0};
    case 1: return {63 - ramp, 63, 0};
    case 2: return {0, 63, ramp};
    case 3: return {0, 63 - ramp, 63};
    case 4: return {ramp, 0, 63};
    default: return {63, 0, 63 - ramp};
    }
}

inline uint16_t tint_white(uint16_t pixel, Tint tint) {
    unsigned red = pixel >> 11, green = (pixel >> 5) & 63, blue = pixel & 31;
    red = (red << 1) | (red >> 4);
    blue = (blue << 1) | (blue >> 4);
    unsigned white = red < green ? red : green;
    if (blue < white) white = blue;
    // Replace only the common white component. Black and saturated green stay
    // unchanged; grey/green edge blends retain their original coverage/residue.
    red = red - white + (white * tint.red + 31) / 63;
    green = green - white + (white * tint.green + 31) / 63;
    blue = blue - white + (white * tint.blue + 31) / 63;
    return uint16_t((red >> 1) << 11 | green << 5 | (blue >> 1));
}
} // namespace cntrl::splash
