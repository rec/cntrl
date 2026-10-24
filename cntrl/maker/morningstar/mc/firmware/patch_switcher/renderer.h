#pragma once
#include <cntrl/hardware.h>
#include <cstring>
#include <cstdio>
#include "displays.h"
#include "show_graphics.h"
#include "panorama.h"

// One glyph/column per repaint poll; one three-panel column step per animation poll.
class DisplayRenderer {
public:
    bool ready() const {
        return !preparing_ && !animating_ && drawing_ < 0 && !dirty_[0] && !dirty_[1] && !dirty_[2];
    }
    void invalidate() {
        drawing_ = -1; rendering_ = false;
        preparing_ = animating_ = native_ready_ = false; ring_ = 0;
        for (unsigned i = 0; i < 3; ++i) { valid_[i] = false; dirty_[i] = true; }
    }
    void transition(bool undo, unsigned duration_ms) {
        if (!ready() || !valid_[0] || !valid_[1] || !valid_[2]) return;
        std::memcpy(previous_, cache_, sizeof(cache_));
        undo_ = undo; duration_ms_ = duration_ms; preparing_ = true;
        for (auto &dirty : dirty_) dirty = true;
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
    void draw(uint32_t now = 0) {
        if (!native_ready_) { initialize_display_panorama(); native_ready_ = true; }
        if (animating_) { animate(now); return; }
        if (drawing_ < 0) {
            for (unsigned i = 0; i < 3; ++i) if (dirty_[i]) {
                drawing_ = int(i); dirty_[i] = false;
                painting_ = requested_[i]; y_ = field_ = letter_ = 0;
                graphics_.configure(painting_.graphics); rendering_ = true; break;
            }
        }
        if (drawing_ < 0) {
            if (preparing_) {
                preparing_ = false; animating_ = true;
                progress_ = 0; started_ = now;
            }
            return;
        }
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
        uint8_t pixels[256];
        bool changed = !valid_[drawing_];
        for (unsigned y = 0; y < 128; ++y) {
            unsigned index = y * 160 + y_;
            uint16_t pixel = graphics_.getFrameBuffer()[index];
            pixels[2 * y] = uint8_t(pixel >> 8); pixels[2 * y + 1] = uint8_t(pixel);
            if (pixel != cache_[drawing_][index]) changed = true;
            cache_[drawing_][index] = pixel;
        }
        if (changed && !preparing_)
            write_display_column(unsigned(drawing_), (ring_ + y_) % 160, pixels);
        if (++y_ == 160) {
            valid_[drawing_] = true;
            drawing_ = -1;
        }
    }
private:
    uint16_t moving_pixel(unsigned panel, unsigned x, unsigned y, unsigned progress) const {
        auto sample = panorama_sample(panel, x, progress, undo_);
        if (sample.x >= 160) return 0; // Empty virtual space between displays.
        if (sample.pane == -1) return cache_[0][y * 160 + sample.x];
        if (sample.pane == 3) return cache_[2][y * 160 + sample.x];
        return previous_[sample.pane][y * 160 + sample.x];
    }
    void animate(uint32_t now) {
        unsigned goal = transition_progress(uint32_t(now - started_), duration_ms_);
        if (progress_ >= goal) return;
        ++progress_;
        ring_ = (ring_ + (undo_ ? 159 : 1)) % 160;
        unsigned x = undo_ ? 0 : 159;
        uint8_t pixels[256];
        for (unsigned panel = 0; panel < 3; ++panel) {
            for (unsigned y = 0; y < 128; ++y) {
                uint16_t pixel = moving_pixel(panel, x, y, progress_);
                pixels[2 * y] = uint8_t(pixel >> 8); pixels[2 * y + 1] = uint8_t(pixel);
            }
            write_display_column(panel, (ring_ + x) % 160, pixels);
        }
        if (progress_ == goal) scroll_display_panorama(ring_);
        if (progress_ != display_pitch) return;
        // The moved bitmaps may use a different pane's font/colors. Record what
        // is actually visible before normal rendering applies destination styles.
        for (unsigned panel = 0; panel < 3; ++panel)
            for (unsigned y = 0; y < 128; ++y)
                for (unsigned x = 0; x < 160; ++x)
                    cache_[panel][y * 160 + x] = moving_pixel(panel, x, y, display_pitch);
        animating_ = false;
        for (auto &dirty : dirty_) dirty = true;
    }
    cntrl::Pane requested_[3], painting_;
    ShowGraphics graphics_;
    uint16_t cache_[3][160 * 128] = {};
    uint16_t previous_[3][160 * 128] = {};
    bool preparing_ = false, animating_ = false, undo_ = false, native_ready_ = false;
    unsigned ring_ = 0, progress_ = 0;
    unsigned duration_ms_ = 1000;
    uint32_t started_ = 0;
    bool dirty_[3] = {true, true, true};
    bool valid_[3] = {};
    int drawing_ = -1;
    bool rendering_ = false;
    unsigned y_ = 0, field_ = 0, letter_ = 0;
};
