#pragma once
#include "panorama.h"

// Alternate scroll-only pairs and pairs rewriting the existing RAM pixels.
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
            write_pattern(panel, x);
            if (++column_ == 3 * 162) stage_ = 2;
        } else if (stage_ == 3) {
            since_ = last_step_ = now; progress_ = 0; stage_ = 4;
        } else if (stage_ == 4) {
            unsigned goal = transition_progress(uint32_t(now - since_) / 1000, 500);
            if (progress_ >= goal) return;
            ring_ = (ring_ + (trial_ % 2 ? 161 : 1)) % 162;
            last_step_ = now;
            if ((trial_ / 2) % 2) {
                unsigned column = (ring_ + (trial_ % 2 ? 0 : 159)) % 162;
                for (unsigned panel = 0; panel < 3; ++panel) write_pattern(panel, column);
            }
            stage_ = 6;
        } else if (stage_ == 6) {
            // Both conditions reserve the same 1.5 ms before each SSA command.
            // A transfer taking longer will delay the command; this is a target,
            // not a claim of measured equality on the device.
            if (uint32_t(now - last_step_) < 1500) return;
            scroll_display_panorama(ring_);
            stage_ = 4;
            if (++progress_ == 232) { since_ = now; stage_ = 5; }
        } else if (stage_ == 5 && uint32_t(now - since_) >= 1500000) {
            if (++trial_ == 8) { stage_ = 2; trial_ = 0; }
            else stage_ = 3;
        }
    }
private:
    static void write_pattern(unsigned panel, unsigned x) {
        uint8_t pixels[256];
        for (unsigned y = 0; y < 128; ++y) {
            uint16_t pixel = x % 18 < 2 ? 0
                : (x / 54 + y / 16 + panel) % 4 < 2 ? 0xffff : 0x4208;
            pixels[2 * y] = uint8_t(pixel >> 8); pixels[2 * y + 1] = uint8_t(pixel);
        }
        write_display_column(panel, x, pixels);
    }
    unsigned stage_ = 0, column_ = 0, trial_ = 0, ring_ = 0, progress_ = 0;
    uint32_t since_ = 0, last_step_ = 0;
};
