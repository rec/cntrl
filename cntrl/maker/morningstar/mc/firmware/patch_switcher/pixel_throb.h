#pragma once
#include <cstdint>

class PixelThrob {
public:
    void configure(unsigned period, uint8_t low, uint8_t high, unsigned delay) {
        period_ms = period; half_period_ms = period / 2;
        minimum = low; maximum = high; delay_ms = delay; patch_ = -1;
    }
    uint8_t brightness(int patch, uint32_t now, uint8_t normal, uint8_t held_buttons) {
        if (patch < 0 || held_buttons || !period_ms) { patch_ = -1; return normal; }
        if (patch != patch_) { patch_ = patch; started_ = now; waiting_ = true; }
        if (waiting_) {
            if (uint32_t(now - started_) < delay_ms) return maximum;
            started_ += delay_ms; waiting_ = false;
        }
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
    uint32_t minimum = 0, maximum = 0;
    uint32_t period_ms = 0, half_period_ms = 0, delay_ms = 0;
    static constexpr uint32_t precision = 1024;
    int patch_ = -1;
    uint32_t started_ = 0;
    bool waiting_ = false;
};
