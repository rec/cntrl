#pragma once
#include <cntrl/hardware.h>
#include <cstring>
#include "displays.h"
#include "graphics.h"

// One library glyph or one changed row per loop, never a whole blocking redraw.
class DisplayRenderer {
public:
    void show(unsigned pane, const cntrl::Pane &content) {
        if (pane >= 3 || !std::memcmp(&requested_[pane], &content, sizeof(content))) return;
        requested_[pane] = content;
        dirty_[pane] = true;
    }
    void draw() {
        if (drawing_ < 0) {
            for (unsigned i = 0; i < 3; ++i) if (dirty_[i]) {
                drawing_ = int(i); dirty_[i] = false;
                painting_ = requested_[i]; y_ = field_ = letter_ = top_ = 0;
                graphics_.fillScreen(0); rendering_ = true; break;
            }
        }
        if (drawing_ < 0) return;
        if (rendering_) {
            const char *fields[] = {painting_.title, painting_.instrument,
                                    painting_.category, painting_.patch};
            const char *text = fields[field_];
            unsigned scale = field_ == 3 ? 2 : 1;
            unsigned columns = 160 / (6 * scale);
            unsigned length = std::strlen(text);
            unsigned lines = length ? (length + columns - 1) / columns : 1;
            if (top_ + lines * 8 * scale > 128) {
                scale = 1; columns = 160 / 6;
                lines = length ? (length + columns - 1) / columns : 1;
            }
            if (letter_ < length) {
                graphics_.setTextSize(scale);
                graphics_.setCursor((letter_ % columns) * 6 * scale,
                                    top_ + (letter_ / columns) * 8 * scale);
                graphics_.write(uint8_t(text[letter_++]));
            } else {
                top_ += lines * 8 * scale + 2;
                letter_ = 0;
                if (++field_ == 4) rendering_ = false;
            }
            return;
        }
        uint8_t pixels[320];
        const uint16_t *row = graphics_.getFrameBuffer() + y_ * 160;
        uint16_t *previous = cache_[drawing_] + y_ * 160;
        unsigned first = 160, last = 0;
        for (unsigned x = 0; x < 160; ++x) {
            pixels[2 * x] = uint8_t(row[x] >> 8);
            pixels[2 * x + 1] = uint8_t(row[x]);
            if (!valid_[drawing_] || row[x] != previous[x]) {
                if (first == 160) first = x;
                last = x;
            }
        }
        if (first != 160) {
            write_display_segment(unsigned(drawing_), first, y_, pixels + 2 * first, last - first + 1);
            std::memcpy(previous, row, 160 * sizeof(uint16_t));
        }
        if (++y_ == 128) {
            valid_[drawing_] = true;
            drawing_ = -1;
        }
    }
private:
    cntrl::Pane requested_[3], painting_;
    Graphics graphics_;
    uint16_t cache_[3][160 * 128] = {};
    bool dirty_[3] = {true, true, true};
    bool valid_[3] = {};
    int drawing_ = -1;
    bool rendering_ = false;
    unsigned y_ = 0, field_ = 0, letter_ = 0, top_ = 0;
};
