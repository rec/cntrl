#pragma once
#include "panorama.h"

// Two-column batches, spaced across a nominal 500 ms transition.
class ScrollTimingProbe {
public:
    bool active() const { return stage_ != 0; }
    void arm() { stage_ = 1; column_ = trial_ = ring_ = 0; running_ = false; }
    void stop() { stage_ = 0; }
    void start() { if (stage_ == 2) { running_ = true; stage_ = 3; } }
    void poll(uint32_t now) {
        if (!active()) return;
        if (stage_ == 1) {
            if (!column_) { initialize_display_panorama(); ring_ = 0; }
            unsigned panel = column_ / 162, x = column_ % 162;
            write_pattern(panel, x, false);
            if (++column_ == 3 * 162) { since_ = now; stage_ = running_ ? 7 : 2; }
        } else if (stage_ == 3) {
            since_ = batch_since_ = last_step_ = now;
            progress_ = batch_left_ = 0; stage_ = 4;
        } else if (stage_ == 4) {
            if (!batch_left_) {
                // 116 batches * 4311 us = 500076 us. Never catch up by
                // immediately issuing another batch after a scheduling delay.
                if (uint32_t(now - batch_since_) < 4311 || uint32_t(now - last_step_) < 1000) return;
                batch_since_ = now; batch_left_ = 2;
            }
            ring_ = (ring_ + (trial_ % 2 ? 161 : 1)) % 162;
            unsigned column = (ring_ + (trial_ % 2 ? 0 : 159)) % 162;
            for (unsigned panel = 0; panel < 3; ++panel) write_pattern(panel, column, true);
            scroll_display_panorama(ring_);
            --batch_left_; last_step_ = now;
            if (++progress_ == 232) { since_ = now; stage_ = 5; }
        } else if (stage_ == 5 && uint32_t(now - since_) >= 1000000) {
            if (++trial_ == 8) { running_ = false; trial_ = 0; }
            column_ = 0; stage_ = 1;
        } else if (stage_ == 7 && uint32_t(now - since_) >= 500000) {
            stage_ = 3;
        }
    }
private:
    static void write_pattern(unsigned panel, unsigned x, bool marker) {
        uint8_t pixels[256];
        for (unsigned y = 0; y < 128; ++y) {
            uint16_t pixel = y % 16 >= 8 ? 0x7bef : 0;
            if (marker) pixel = y % 16 < 8 ? 0xffff : 0;
            pixels[2 * y] = uint8_t(pixel >> 8); pixels[2 * y + 1] = uint8_t(pixel);
        }
        write_display_column(panel, x, pixels);
    }
    unsigned stage_ = 0, column_ = 0, trial_ = 0, ring_ = 0, progress_ = 0, batch_left_ = 0;
    uint32_t since_ = 0, batch_since_ = 0, last_step_ = 0;
    bool running_ = false;
};
