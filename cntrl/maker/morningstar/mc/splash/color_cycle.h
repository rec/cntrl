#pragma once
#include <cstdint>

namespace cntrl::splash {
struct Tint {
    unsigned red, green, blue; // Six-bit channels, 0..63.
};

class ColorWalk {
public:
    explicit ColorWalk(uint32_t seed = 0x9e3779b9) : state_(seed | 1) {}
    Tint at(uint32_t now) {
        if (!started_) {
            state_ ^= now; state_ |= 1;
            started_ = true; last_ = speed_since_ = now;
            choose_target(); speed_target_ = next_speed(speed_from_);
        }
        uint32_t elapsed = now - last_; last_ = now;
        if (elapsed > 1000) elapsed = 0; // Do not replay time spent off-screen.
        unsigned speed_elapsed = now - speed_since_;
        if (speed_elapsed >= 5000) {
            speed_from_ = speed_target_;
            speed_target_ = next_speed(speed_from_);
            speed_since_ = now; speed_elapsed = 0;
        }
        unsigned speed = (speed_from_ * (5000 - speed_elapsed) + speed_target_ * speed_elapsed) / 5000;
        phase_ += elapsed * speed;
        // At most two boundaries per poll: elapsed <= 1000, speed <= 2000.
        while (phase_ >= 1000000) {
            phase_ -= 1000000; from_ = target_; choose_target();
        }
        return {blend(from_.red, target_.red, phase_),
                blend(from_.green, target_.green, phase_),
                blend(from_.blue, target_.blue, phase_)};
    }
private:
    static unsigned blend(unsigned from, unsigned to, unsigned elapsed) {
        return (from * (1000000 - elapsed) + to * elapsed + 500000) / 1000000;
    }
    uint32_t random() {
        state_ ^= state_ << 13; state_ ^= state_ >> 17; state_ ^= state_ << 5;
        return state_;
    }
    unsigned next_speed(unsigned value) {
        int next = int(value) + int(random() % 501) - 250;
        if (next < 500) next = 1000 - next;
        if (next > 2000) next = 4000 - next;
        return unsigned(next);
    }
    unsigned step(unsigned value) {
        int next = int(value) + int(random() % 41) - 20;
        // Reflect at the RGB cube walls instead of sticking/clipping there.
        if (next < 0) next = -next;
        if (next > 63) next = 126 - next;
        return unsigned(next);
    }
    void choose_target() {
        target_ = {step(from_.red), step(from_.green), step(from_.blue)};
    }
    Tint from_ = {63, 0, 0}, target_ = {63, 0, 0};
    uint32_t state_, last_ = 0, speed_since_ = 0, phase_ = 0;
    unsigned speed_from_ = 1000, speed_target_ = 1000; // Permille of base speed.
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
