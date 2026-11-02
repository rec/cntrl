#pragma once
#include <cstdint>

class PixelThrob {
public:
    uint8_t brightness(int patch, uint32_t now, uint8_t normal) {
        if (patch < 0) { patch_ = -1; return normal; }
        if (patch != patch_) { patch_ = patch; started_ = now; }
        uint32_t elapsed = now - started_;
        started_ += elapsed / period_ms * period_ms;
        elapsed %= period_ms;
        const uint32_t ramp = elapsed <= half_period_ms ? elapsed : period_ms - elapsed;
        const uint32_t t = ramp * precision / half_period_ms;
        // Smoothstep 3t^2 - 2t^3 has zero slope at both ends of each half-cycle.
        // Q10 arithmetic keeps every intermediate within 32 bits.
        const uint32_t eased = t * t * (3 * precision - 2 * t) / (precision * precision);
        return maximum - ((maximum - minimum) * eased + precision / 2) / precision;
    }
private:
    static constexpr uint32_t minimum = 64, maximum = 128;
    static constexpr uint32_t period_ms = 4000, half_period_ms = period_ms / 2;
    static constexpr uint32_t precision = 1024;
    int patch_ = -1;
    uint32_t started_ = 0;
};
