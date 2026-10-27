#pragma once
#include <cntrl/hardware.h>
#include <cstring>
#include <cstdio>
#include "displays.h"
#include "show_graphics.h"
#include "panorama.h"

constexpr unsigned DEFAULT_TRANSITION_MS = 500;

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
    void transition(bool undo, std::optional<unsigned> duration_ms) {
        if (!ready() || !valid_[0] || !valid_[1] || !valid_[2]) return;
        std::memcpy(previous_, cache_, sizeof(cache_));
        undo_ = undo; duration_ms_ = duration_ms.value_or(DEFAULT_TRANSITION_MS); preparing_ = true;
        for (auto &dirty : dirty_) dirty = true;
    }
    void show(unsigned pane, const cntrl::Pane &content) {
        if (pane >= 3 || !std::memcmp(&requested_[pane], &content, sizeof(content))) return;
        requested_[pane] = content;
        dirty_[pane] = true;
    }
    void error(const char *message) {
        cntrl::Pane view;
        view.graphics = requested_[1].graphics;
        view.lines[0].graphics = view.graphics;
        view.lines[0].x = view.graphics.pad;
        std::snprintf(view.lines[0].text, sizeof(view.lines[0].text), "%s", message);
        show(1, view);
    }
    void draw(uint32_t now = 0) {
        if (!native_ready_) { initialize_display_panorama(); native_ready_ = true; }
        if (animating_) { animate(now); return; }
        if (drawing_ < 0) {
            for (unsigned i = 0; i < 3; ++i) if (dirty_[i]) {
                drawing_ = int(i); dirty_[i] = false;
                painting_ = requested_[i]; y_ = field_ = letter_ = 0;
                graphics_.configure(painting_.graphics); backgrounds(); rendering_ = true; break;
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
            const auto &line = painting_.lines[field_];
            const char *text = line.text;
            unsigned length = std::strlen(text);
            const auto &settings = line.graphics;
            if (letter_ < length) {
                if (!letter_) {
                    graphics_.select_font(settings.font, settings.size, settings.asset);
                    // Background bands are already painted. Preserve neighboring
                    // glyphs where italic bearings overlap their advances.
                    graphics_.setTextColor(settings.foreground);
                    if (line.centered) {
                        int16_t x, y; uint16_t w, h;
                        graphics_.getTextBounds(text, 0, 0, &x, &y, &w, &h);
                        graphics_.setCursor((160 - int(w)) / 2 - x, text_y(field_, 0));
                    } else graphics_.setCursor(line.x,
                        text_y(field_, 0));
                }
                if (line.wrap && settings.font == 0) {
                    unsigned columns = (160 - 2 * settings.pad) / (6 * (settings.size / 8));
                    if (columns && letter_ % columns == 0)
                        graphics_.setCursor(line.x, text_y(field_, letter_ / columns));
                }
                graphics_.write(uint8_t(text[letter_++]));
            } else {
                letter_ = 0;
                if (++field_ == cntrl::max_lines) {
                    // Mask after composing all glyphs so italic overlaps keep
                    // the same checkerboard phase across the whole line.
                    backgrounds(true);
                    rendering_ = false;
                }
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
    unsigned text_y(unsigned field, unsigned row) const {
        const auto &positioned = painting_.lines[field];
        if (positioned.y >= 0) return unsigned(positioned.y) + row * positioned.graphics.line_height + positioned.graphics.top;
        const auto &settings = painting_.graphics;
        unsigned count = 0, height = 0, preceding = 0, index = 0;
        for (unsigned i = 0; i < cntrl::max_lines; ++i) {
            unsigned length = std::strlen(painting_.lines[i].text);
            if (!length) continue;
            const auto &line = painting_.lines[i];
            unsigned line_height = line.graphics.line_height;
            unsigned rows = line_rows(line);
            if (i < field) { preceding += rows * line_height; index += rows; }
            if (i == field) { preceding += row * line_height; index += row; }
            count += rows; height += rows * line_height;
        }
        unsigned available = 128 - 2 * settings.pad;
        unsigned remaining = height < available ? available - height : 0;
        const auto &line = painting_.lines[field];
        return settings.pad + preceding + remaining * (2 * index + 1) / (2 * count)
            + line.graphics.top;
    }
    unsigned line_rows(const cntrl::TextLine &line) const {
        const auto &g = line.graphics;
        unsigned columns = line.wrap && g.font == 0 ? (160 - 2 * painting_.graphics.pad) / (6 * (g.size / 8)) : 0;
        return columns ? (std::strlen(line.text) + columns - 1) / columns : 1;
    }
    void backgrounds(bool preserve_text = false) {
        unsigned start = painting_.graphics.pad;
        for (unsigned i = 0; i < cntrl::max_lines; ++i) {
            const auto &line = painting_.lines[i];
            if (!line.text[0]) continue;
            unsigned end = 128 - painting_.graphics.pad;
            for (unsigned j = i + 1; j < cntrl::max_lines; ++j) if (painting_.lines[j].text[0]) {
                unsigned bottom = text_y(i, line_rows(line) - 1) - line.graphics.top + line.graphics.line_height;
                end = (bottom + text_y(j, 0) - painting_.lines[j].graphics.top) / 2;
                break;
            }
            for (unsigned y = start; y < end && y < 128u - painting_.graphics.pad; ++y)
                for (unsigned x = painting_.graphics.pad; x < 160u - painting_.graphics.pad; ++x)
                    if (!preserve_text || (line.stippled && ((x + y) & 1)))
                        graphics_.getFrameBuffer()[y * 160 + x] = line.graphics.background;
            start = end;
        }
    }
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
    unsigned duration_ms_ = 0;
    uint32_t started_ = 0;
    bool dirty_[3] = {true, true, true};
    bool valid_[3] = {};
    int drawing_ = -1;
    bool rendering_ = false;
    unsigned y_ = 0, field_ = 0, letter_ = 0;
};
