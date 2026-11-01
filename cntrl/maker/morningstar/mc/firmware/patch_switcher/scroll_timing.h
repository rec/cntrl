#pragma once
#include "panorama.h"

// Compare preloaded scrolling with conspicuous changed-column writes.
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
            since_ = last_step_ = now; progress_ = 0; stage_ = 4;
        } else if (stage_ == 4) {
            unsigned goal = transition_progress(uint32_t(now - since_) / 1000, 500);
            if (progress_ >= goal) return;
            ring_ = (ring_ + (trial_ % 2 ? 161 : 1)) % 162;
            last_step_ = now;
            if ((trial_ / 2) % 2) {
                unsigned column = (ring_ + (trial_ % 2 ? 0 : 159)) % 162;
                for (unsigned panel = 0; panel < 3; ++panel) write_pattern(panel, column, true);
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
            if (++trial_ == 8) { running_ = false; trial_ = 0; }
            column_ = 0; stage_ = 1;
        } else if (stage_ == 7 && uint32_t(now - since_) >= 700000) {
            stage_ = 3;
        }
    }
private:
    static void write_pattern(unsigned panel, unsigned x, bool marker) {
        uint8_t pixels[256];
        for (unsigned y = 0; y < 128; ++y) {
            uint16_t pixel = x % 18 < 2 ? 0
                : (x / 54 + y / 16 + panel) % 4 < 2 ? 0xffff : 0x4208;
            if (marker) pixel = y % 16 < 8 ? 0xffff : 0;
            pixels[2 * y] = uint8_t(pixel >> 8); pixels[2 * y + 1] = uint8_t(pixel);
        }
        write_display_column(panel, x, pixels);
    }
    unsigned stage_ = 0, column_ = 0, trial_ = 0, ring_ = 0, progress_ = 0;
    uint32_t since_ = 0, last_step_ = 0;
    bool running_ = false;
};
