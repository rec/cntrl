#pragma once
#include <cstdint>

namespace cntrl::splash {
struct Tint {
    unsigned red, green, blue; // Six-bit channels, 0..63.
};

class ColorWalk {
public:
    Tint at(uint32_t now) {
        if (!started_) {
            state_ ^= now; state_ |= 1;
            started_ = true; since_ = now; choose_target();
        }
        uint32_t elapsed = now - since_;
        if (elapsed >= 1000) {
            from_ = target_; choose_target(); since_ = now; elapsed = 0;
        }
        return {blend(from_.red, target_.red, elapsed),
                blend(from_.green, target_.green, elapsed),
                blend(from_.blue, target_.blue, elapsed)};
    }
private:
    static unsigned blend(unsigned from, unsigned to, unsigned elapsed) {
        return (from * (1000 - elapsed) + to * elapsed + 500) / 1000;
    }
    unsigned step(unsigned value) {
        state_ ^= state_ << 13; state_ ^= state_ >> 17; state_ ^= state_ << 5;
        int next = int(value) + int(state_ % 41) - 20;
        // Reflect at the RGB cube walls instead of sticking/clipping there.
        if (next < 0) next = -next;
        if (next > 63) next = 126 - next;
        return unsigned(next);
    }
    void choose_target() {
        target_ = {step(from_.red), step(from_.green), step(from_.blue)};
    }
    Tint from_ = {63, 0, 0}, target_ = {63, 0, 0};
    uint32_t state_ = 0x9e3779b9, since_ = 0;
    bool started_ = false;
};

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
