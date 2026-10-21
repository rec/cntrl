#pragma once

#include <stdint.h>

// Indices A through F in the parallel-input register. Bits 6 and 7 are unused.
constexpr uint8_t button_bits[] = {2, 1, 0, 3, 4, 5};

enum ButtonEvent : uint8_t {
    no_event = 0,
    press_event = 1,
    release_event = 2,
    hold_event = 4,
    double_press_event = 8,
};

struct ButtonState {
    bool candidate = false;
    bool pressed = false;
    uint32_t since = 0;
    uint32_t pressed_at = 0;
    uint32_t released_at = 0;
    bool held = false;
    bool waiting_for_second = false;
    bool double_pressed = false;

    uint8_t update(bool sample, uint32_t now) {
        uint8_t events = no_event;
        if (waiting_for_second && uint32_t(now - released_at) > 300) {
            waiting_for_second = false;
        }
        if (sample != candidate) {
            candidate = sample;
            since = now;
        }
        if (candidate != pressed && uint32_t(now - since) >= 15) {
            pressed = candidate;
            if (pressed) {
                events |= press_event;
                double_pressed = waiting_for_second;
                if (double_pressed) {
                    events |= double_press_event;
                }
                waiting_for_second = false;
                held = false;
                pressed_at = now;
            } else {
                events |= release_event;
                waiting_for_second = !held && !double_pressed;
                released_at = now;
            }
        }
        if (pressed && candidate && !held && uint32_t(now - pressed_at) >= 500) {
            held = true;
            events |= hold_event;
        }
        return events;
    }
};
