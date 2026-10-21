#pragma once

#include <stdint.h>

// Indices A through F in the parallel-input register. Bits 6 and 7 are unused.
constexpr uint8_t button_bits[] = {2, 1, 0, 3, 4, 5};

struct ButtonState {
    bool candidate = false;
    bool pressed = false;
    uint32_t since = 0;

    bool update(bool sample, uint32_t now) {
        if (sample != candidate) {
            candidate = sample;
            since = now;
        }
        if (candidate != pressed && uint32_t(now - since) >= 15) {
            pressed = candidate;
            return true;
        }
        return false;
    }
};
