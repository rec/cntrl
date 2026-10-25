#pragma once
#include <cstdint>

struct GestureResult {
    uint8_t pressed;
    bool activate;
};

class SwitchGesture {
public:
    void cancel() {
        // Volatile stores keep this freestanding hook independent of libc.
        volatile uint8_t *history = history_;
        for (unsigned i = 0; i < 128; ++i) history[i] = 0;
        previous_ = candidate_ = stable_ = 0;
        initialized_ = armed_ = false;
    }

    GestureResult update(uint8_t pressed, uint32_t now) {
        // Delay the physical input stream, including releases and bounce, so
        // stock retains its own debounce and press/hold/release processing.
        if (!initialized_) {
            tick_ = now;
            initialized_ = true;
        }
        uint32_t elapsed = now - tick_;
        if (elapsed > 128) elapsed = 128;
        for (uint32_t i = elapsed; i; --i)
            history_[(now - i) & 127] = previous_;
        tick_ = now;
        previous_ = pressed;
        history_[now & 127] = pressed;

        for (unsigned i = 0; i < 2; ++i) {
            uint8_t bit = uint8_t(1u << i);
            if ((pressed ^ candidate_) & bit) {
                candidate_ ^= bit;
                since_[i] = now;
            }
            if (((candidate_ ^ stable_) & bit) && uint32_t(now - since_[i]) >= 15) {
                stable_ ^= bit;
                if (stable_ & bit) pressed_at_[i] = now;
            }
        }
        // Do not interpret buttons held during stock startup as a command.
        if (!pressed && !stable_) armed_ = true;
        bool activate = armed_ && stable_ == 3
            && uint32_t(now - pressed_at_[0]) <= 50
            && uint32_t(now - pressed_at_[1]) <= 50;
        if (activate) armed_ = false;
        return {history_[(now - 65) & 127], activate};
    }
private:
    uint8_t history_[128] = {};
    uint32_t tick_ = 0, since_[2] = {}, pressed_at_[2] = {};
    uint8_t previous_ = 0, candidate_ = 0, stable_ = 0;
    bool initialized_ = false, armed_ = false;
};
