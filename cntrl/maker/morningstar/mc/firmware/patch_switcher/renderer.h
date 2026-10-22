#pragma once
#include <cntrl/layout.h>
#include "displays.h"

// One row of work per loop, with a monochrome cache of pixels actually sent.
class DisplayRenderer {
public:
    void show(unsigned pane, const cntrl::Pane &content) {
        if (pane >= 3 || !std::memcmp(&requested_[pane], &content, sizeof(content))) return;
        requested_[pane] = content;
        dirty_[pane] = true;
    }
    void draw(const unsigned char *font) {
        if (drawing_ < 0) {
            for (unsigned i = 0; i < 3; ++i) if (dirty_[i]) {
                drawing_ = int(i); dirty_[i] = false;
                painting_ = requested_[i]; y_ = 0; break;
            }
        }
        if (drawing_ < 0) return;
        uint8_t pixels[320], row[20] = {};
        uint8_t *previous = cache_[drawing_] + y_ * 20;
        unsigned first = 160, last = 0;
        for (unsigned x = 0; x < 160; ++x) {
            bool lit = cntrl::text_pixel(painting_, 160, 128, x, y_, font);
            pixels[2 * x] = pixels[2 * x + 1] = lit ? 255 : 0;
            if (lit) row[x / 8] |= 1u << (x % 8);
            if (!valid_[drawing_] || lit != bool(previous[x / 8] & (1u << (x % 8)))) {
                if (first == 160) first = x;
                last = x;
            }
        }
        if (first != 160) {
            write_display_segment(unsigned(drawing_), first, y_, pixels + 2 * first, last - first + 1);
            std::memcpy(previous, row, sizeof(row));
        }
        if (++y_ == 128) {
            valid_[drawing_] = true;
            drawing_ = -1;
        }
    }
private:
    cntrl::Pane requested_[3], painting_;
    uint8_t cache_[3][2560] = {};
    bool dirty_[3] = {true, true, true};
    bool valid_[3] = {};
    int drawing_ = -1;
    unsigned y_ = 0;
};
