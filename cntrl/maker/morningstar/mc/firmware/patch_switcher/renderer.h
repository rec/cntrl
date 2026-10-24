#pragma once
#include <cntrl/hardware.h>
#include <cstring>
#include <cstdio>
#include "displays.h"
#include "show_graphics.h"

// One library glyph or one changed row per loop, never a whole blocking redraw.
class DisplayRenderer {
public:
    bool ready() const {
        return drawing_ < 0 && !dirty_[0] && !dirty_[1] && !dirty_[2];
    }
    void invalidate() {
        drawing_ = -1; rendering_ = false;
        for (unsigned i = 0; i < 3; ++i) { valid_[i] = false; dirty_[i] = true; }
    }
    void show(unsigned pane, const cntrl::Pane &content) {
        if (pane >= 3 || !std::memcmp(&requested_[pane], &content, sizeof(content))) return;
        requested_[pane] = content;
        dirty_[pane] = true;
    }
    void error(const char *message) {
        cntrl::Pane view = requested_[1];
        view.category[0] = 0;
        std::snprintf(view.patch, sizeof(view.patch), "%s", message);
        view.patch_x = view.graphics.pad;
        view.patch_color = view.graphics.foreground;
        show(1, view);
    }
    void draw() {
        if (drawing_ < 0) {
            for (unsigned i = 0; i < 3; ++i) if (dirty_[i]) {
                drawing_ = int(i); dirty_[i] = false;
                painting_ = requested_[i]; y_ = field_ = letter_ = 0;
                graphics_.configure(painting_.graphics); rendering_ = true; break;
            }
        }
        if (drawing_ < 0) return;
        if (rendering_) {
            const char *fields[] = {painting_.category, painting_.patch};
            const char *text = fields[field_];
            unsigned length = std::strlen(text);
            const auto &settings = painting_.graphics;
            if (letter_ < length) {
                if (!letter_) {
                    graphics_.setTextColor(field_ ? painting_.patch_color : settings.foreground, settings.background);
                    if (painting_.centered) {
                        int16_t x, y; uint16_t w, h;
                        graphics_.getTextBounds(text, 0, 0, &x, &y, &w, &h);
                        graphics_.setCursor((160 - int(w)) / 2 - x, (128 - int(h)) / 2 - y);
                    } else graphics_.setCursor(field_ ? painting_.patch_x : painting_.category_x,
                        settings.pad + settings.top + field_ * (settings.line_height + 2));
                }
                if (field_ && painting_.wrap_patch && settings.font == 0) {
                    unsigned columns = (160 - 2 * settings.pad) / (6 * (settings.size / 8));
                    if (columns && letter_ % columns == 0)
                        graphics_.setCursor(painting_.patch_x, settings.pad + settings.top
                            + settings.line_height + 2 + (letter_ / columns) * settings.line_height);
                }
                graphics_.write(uint8_t(text[letter_++]));
            } else {
                letter_ = 0;
                if (++field_ == 2) rendering_ = false;
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
    ShowGraphics graphics_;
    uint16_t cache_[3][160 * 128] = {};
    bool dirty_[3] = {true, true, true};
    bool valid_[3] = {};
    int drawing_ = -1;
    bool rendering_ = false;
    unsigned y_ = 0, field_ = 0, letter_ = 0;
};
