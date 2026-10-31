#pragma once
#include "panorama.h"
#include <cstdio>

extern "C" const unsigned char glcdfont[];
void timing_scroll_panel(unsigned panel, unsigned offset);
void timing_backlight(unsigned value);

// Explicit optical diagnostic. It never changes saved settings or sends MIDI.
class ScrollTimingProbe {
public:
    bool active() const { return active_; }
    void arm() { active_ = true; stage_ = 0; column_ = 0; trial_ = 0; }
    void stop() { active_ = false; }
    void start() {
        if (stage_ != 1) return;
        trial_ = 0; column_ = 0; stage_ = 2;
    }
    void poll(uint32_t now) {
        if (!active_) return;
        if (stage_ == 0 || stage_ == 2) {
            if (!column_) {
                initialize_display_panorama();
                timing_backlight(96);
            }
            unsigned panel = column_ / 162, x = column_ % 162;
            uint8_t pixels[256];
            char label[24];
            if (stage_ == 0) std::snprintf(label, sizeof(label), "PRESS C");
            else if (panel == 0) std::snprintf(label, sizeof(label), "%c %uMS R%u", trial_ < 16 ? 'L' : 'R', delay_ms(), trial_ % 16 / 2 + 1);
            else std::snprintf(label, sizeof(label), "F EXITS");
            for (unsigned y = 0; y < 128; ++y) {
                uint16_t pixel = panel == 1 ? baseline(x, y) : text_pixel(label, x, y);
                pixels[2 * y] = uint8_t(pixel >> 8); pixels[2 * y + 1] = uint8_t(pixel);
            }
            write_display_column(panel, x, pixels);
            if (++column_ == 486) { column_ = 0; since_ = now; stage_ = stage_ == 0 ? 1 : 3; }
            return;
        }
        if (stage_ == 3 && uint32_t(now - since_) >= 800000 + trial_ * 1999) {
            // The backlight step is an optical command-time reference. Its
            // circuit/camera latency is unknown; it is not a TE acknowledgement.
            timing_backlight(192);
            timing_scroll_panel(1, trial_ < 16 ? 3 : 159);
            stage_ = 4; since_ = now;
            if (delay_ms()) return;
        }
        if (stage_ == 4 && uint32_t(now - since_) >= delay_ms() * 1000) {
            uint8_t pixels[256];
            for (unsigned y = 0; y < 128; ++y) {
                uint16_t pixel = y % 16 < 12 ? 0xffff : 0;
                pixels[2 * y] = uint8_t(pixel >> 8); pixels[2 * y + 1] = uint8_t(pixel);
            }
            write_display_column(1, trial_ < 16 ? 0 : 159, pixels);
            stage_ = 5;
        }
        if (stage_ == 5 && uint32_t(now - since_) >= 700000) {
            if (++trial_ == 32) { stage_ = 0; trial_ = 0; }
            else stage_ = 2;
        }
    }
private:
    unsigned delay_ms() const {
        return trial_ % 2 ? 5 : 2;
    }
    static uint16_t baseline(unsigned x, unsigned y) {
        // Dark reused edges and two black hidden columns. Interior ticks
        // distinguish a genuine three-column shift from a stationary image.
        return x < 160 && x > 0 && x < 159 && x % 16 == 8 && y >= 48 && y < 80 ? 0x7bef : 0;
    }
    static uint16_t text_pixel(const char *text, unsigned x, unsigned y) {
        if (x < 8 || y < 40 || y >= 61) return 0;
        unsigned index = (x - 8) / 12, col = ((x - 8) % 12) / 2;
        for (unsigned i = 0; i <= index; ++i) if (!text[i]) return 0;
        return col < 5 && (glcdfont[unsigned(text[index]) * 5 + col] & (1u << ((y - 40) / 3))) ? 0xffff : 0;
    }
    bool active_ = false;
    unsigned stage_ = 0, column_ = 0, trial_ = 0;
    uint32_t since_ = 0;
};
