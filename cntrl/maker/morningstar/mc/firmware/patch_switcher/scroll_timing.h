#pragma once
#include "panorama.h"

// Preload all RAM, then issue only scroll commands until disarmed/rearmed.
class ScrollTimingProbe {
public:
    bool active() const { return stage_ != 0; }
    void arm() { stage_ = 1; column_ = trial_ = ring_ = 0; }
    void stop() { stage_ = 0; }
    void start() { if (stage_ == 2) stage_ = 3; }
    void poll(uint32_t now) {
        if (!active()) return;
        if (stage_ == 1) {
            if (!column_) initialize_display_panorama();
            unsigned panel = column_ / 162, x = column_ % 162;
            uint8_t pixels[256];
            for (unsigned y = 0; y < 128; ++y) {
                // Three broad zones, horizontal bands and nine narrow rulers.
                // The two hidden columns belong to the same defined pattern.
                uint16_t pixel = x % 18 < 2 ? 0
                    : (x / 54 + y / 16 + panel) % 4 < 2 ? 0xffff : 0x4208;
                pixels[2 * y] = uint8_t(pixel >> 8); pixels[2 * y + 1] = uint8_t(pixel);
            }
            write_display_column(panel, x, pixels);
            if (++column_ == 3 * 162) stage_ = 2;
        } else if (stage_ == 3) {
            since_ = last_step_ = now; progress_ = 0; stage_ = 4;
        } else if (stage_ == 4) {
            unsigned goal = transition_progress(uint32_t(now - since_) / 1000, 500);
            // Removing RAM transfers removes ~0.8 ms of wire time per step.
            // Keep a 1 ms minimum spacing, without pretending it is measured.
            if (progress_ >= goal || uint32_t(now - last_step_) < 1000) return;
            ring_ = (ring_ + (trial_ % 2 ? 161 : 1)) % 162;
            scroll_display_panorama(ring_);
            last_step_ = now;
            if (++progress_ == 232) { since_ = now; stage_ = 5; }
        } else if (stage_ == 5 && uint32_t(now - since_) >= 1500000) {
            if (++trial_ == 8) { stage_ = 2; trial_ = 0; }
            else stage_ = 3;
        }
    }
private:
    unsigned stage_ = 0, column_ = 0, trial_ = 0, ring_ = 0, progress_ = 0;
    uint32_t since_ = 0, last_step_ = 0;
};
