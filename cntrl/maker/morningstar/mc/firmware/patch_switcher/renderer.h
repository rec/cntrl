#pragma once
#include <cntrl/hardware.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include "displays.h"
#include "show_graphics.h"
#include "panorama.h"
#include "target.h"

constexpr unsigned DEFAULT_TRANSITION_MS = 75;
constexpr unsigned DEFAULT_VERTICAL_TRANSITION_MS = 200;
constexpr unsigned VERTICAL_STEP_PIXELS = 32;
constexpr unsigned WINDOW_CORNER_RADIUS = 8;

// One glyph/column per repaint poll; up to eight columns per vertical animation poll.
class DisplayRenderer {
public:
    void target_brightness(uint8_t value) { requested_brightness_ = value; }
    bool ready() const {
        return !display_dma_pending() && !preparing_ && !animating_ && drawing_ < 0 && !dirty_[0] && !dirty_[1] && !dirty_[2];
    }
    void invalidate() {
        drawing_ = -1; rendering_ = false;
        preparing_ = animating_ = native_ready_ = false;
        for (auto &ring : rings_) ring = 0;
        for (unsigned i = 0; i < 3; ++i) { valid_[i] = false; dirty_[i] = true; }
    }
    void transition(bool undo, std::optional<unsigned> duration_ms, int pane = -1, bool keep_right = false, bool vertical = false, bool panorama = true) {
        if (preparing_ || animating_ || display_dma_pending() || !valid_[0] || !valid_[1] || !valid_[2]) return;
        // Browsing may still be repainting the candidate. Animate from the
        // pixels already on screen instead of silently dropping the transition.
        drawing_ = -1; rendering_ = false;
        std::memcpy(previous_, cache_, sizeof(cache_));
        undo_ = undo; animation_pane_ = pane; keep_right_ = keep_right;
        panorama_ = panorama;
        vertical_ = vertical; vertical_column_ = 0; vertical_panel_ = 0;
        duration_ms_ = duration_ms.value_or(vertical_ ? DEFAULT_VERTICAL_TRANSITION_MS : DEFAULT_TRANSITION_MS); preparing_ = true;
        for (auto &dirty : dirty_) dirty = true;
    }
    void show(unsigned pane, const cntrl::Pane &content) {
        if (pane >= 3 || !std::memcmp(&requested_[pane], &content, sizeof(content))) return;
        requested_[pane] = content;
        if (pane == 0 && pixel_brightness_ != requested_brightness_) {
            pixel_brightness_ = requested_brightness_; dim_column_ = 0;
        }
        dirty_[pane] = true;
    }
    void error(const char *message) {
        cntrl::Pane view;
        view.graphics = requested_[1].graphics;
        view.graphics.font = 0; view.graphics.size = 8; view.graphics.asset = 0;
        view.graphics.line_height = 8; view.graphics.top = 0;
        view.lines[0].graphics = view.graphics;
        view.lines[0].x = view.graphics.pad;
        std::snprintf(view.lines[0].text, sizeof(view.lines[0].text), "%s", message);
        show(1, view);
    }
    // now is a microsecond timestamp, sampled before this bounded poll.
    void draw(uint32_t now = 0) {
        poll_display_dma();
        // Completion releases the shared pins; a due switch scan gets them next.
        if (switch_scan_due()) return;
        if (take_display_dma_error()) {
            invalidate();
            error("Display DMA failed");
        }
        start_display_dma();
        if ((!animating_ || !vertical_) && display_dma_pending()) return;
        if (!native_ready_) { initialize_display_panorama(); native_ready_ = true; }
        if (animating_) { animate(now); start_display_dma(); return; }
        if (drawing_ < 0) {
            for (unsigned i = 0; i < 3; ++i) if (dirty_[i]) {
                drawing_ = int(i); dirty_[i] = false;
                painting_ = requested_[i]; y_ = field_ = letter_ = 0;
                graphics_.configure(painting_.graphics); backgrounds();
                for (const auto &c : painting_.circles) circle(c);
                toggle(); rendering_ = true; break;
            }
        }
        if (drawing_ < 0) {
            if (preparing_) {
                preparing_ = false; animating_ = true;
                progress_ = 0; started_ = now; anchor_ = waiting_ = false;
            } else dim_target();
            return;
        }
        if (rendering_) {
            const auto &line = painting_.lines[field_];
            const char *text = line.text;
            unsigned length = std::strlen(text);
            const auto &settings = line.graphics;
            bool sparse = settings.font == 255;
            bool painted = sparse && length && graphics_.sparse_letter(settings, text, letter_, line.x, text_y(field_, 0), line.centered);
            if (painted) { ++letter_; }
            else if (!sparse && letter_ < length) {
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
                    auto position = ShowGraphics::wrapped_position(line, letter_);
                    graphics_.setCursor(line.x + position.x, text_y(field_, position.row));
                }
                graphics_.setTextColor(line.color_mask & (uint64_t(1) << letter_) ? line.alternate_color : settings.foreground);
                graphics_.write(uint8_t(text[letter_++]));
            } else {
                letter_ = 0;
                if (++field_ == cntrl::max_lines) {
                    // Mask after composing all glyphs so italic overlaps keep
                    // the same checkerboard phase across the whole line.
                    backgrounds(true);
                    round_corners();
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
            write_column(unsigned(drawing_), (rings_[drawing_] + y_) % display_ram_columns, pixels);
        if (++y_ == 160) {
            valid_[drawing_] = true;
            drawing_ = -1;
        }
    }
private:
    bool moving_panel(unsigned panel) const {
        return animation_pane_ >= 0 ? panel == unsigned(animation_pane_) : !(keep_right_ && panel == 2);
    }
    void circle(const cntrl::Circle &c) {
        if (!c.diameter) return;
        const int span = c.diameter + 2 * c.annulus;
        const int left = int(c.x) - span / 2, top = int(c.y) - span / 2;
        for (int row = 0; row < span; ++row) for (int col = 0; col < span; ++col) {
            int x = left + col, y = top + row;
            if (x < 0 || x >= 160 || y < 0 || y >= 128) continue;
            int dx = 2 * col + 1 - span, dy = 2 * row + 1 - span;
            int distance = dx * dx + dy * dy;
            if (distance <= span * span)
                graphics_.getFrameBuffer()[y * 160 + x] = distance <= c.diameter * c.diameter ? c.color : c.ring_color;
        }
    }
    void toggle() {
        const auto &t = painting_.toggle;
        if (!t.visible) return;
        // A 26 by 14 capsule, with a white knob at the selected end.
        for (int row = 0; row < 14; ++row) for (int col = 0; col < 26; ++col) {
            int x = t.x + col, y = t.y + row;
            if (x >= 160 || y >= 128) continue;
            int dx = col < 7 ? 2 * col + 1 - 14 : col >= 19 ? 2 * col + 1 - 38 : 0;
            int dy = 2 * row + 1 - 14;
            if (dx * dx + dy * dy > 196) continue;
            int knob = 2 * col + 1 - (t.enabled ? 38 : 14);
            graphics_.getFrameBuffer()[y * 160 + x] = knob * knob + dy * dy <= 100
                ? (t.disabled ? 0xc618 : 0xffff) : t.disabled ? 0x8410 : t.enabled ? 0x07e0 : 0;
        }
    }
    uint16_t display_pixel(unsigned panel, uint16_t pixel) const {
        if (panel != 0 || pixel_brightness_ == 128) return pixel;
        const unsigned red = ((pixel >> 11) * pixel_brightness_ + 64) / 128;
        const unsigned green = (((pixel >> 5) & 63) * pixel_brightness_ + 64) / 128;
        const unsigned blue = ((pixel & 31) * pixel_brightness_ + 64) / 128;
        return uint16_t(red << 11 | green << 5 | blue);
    }
    void write_column(unsigned panel, unsigned column, uint8_t *pixels, unsigned first = 0, unsigned count = 128) {
        for (unsigned y = first; y < first + count; ++y) {
            const uint16_t pixel = display_pixel(panel, uint16_t(pixels[2 * y]) << 8 | pixels[2 * y + 1]);
            pixels[2 * y] = uint8_t(pixel >> 8); pixels[2 * y + 1] = uint8_t(pixel);
        }
        if (first == 0 && count == 128) write_display_column(panel, column, pixels);
        else write_display_column_segment(panel, column, first, pixels + 2 * first, count);
    }
    void dim_target() {
        if (!valid_[0]) return;
        if (dim_column_ == 160) {
            if (pixel_brightness_ == requested_brightness_) return;
            pixel_brightness_ = requested_brightness_; dim_column_ = 0;
        }
        uint8_t pixels[256];
        for (unsigned y = 0; y < 128; ++y) {
            const uint16_t pixel = cache_[0][y * 160 + dim_column_];
            pixels[2 * y] = uint8_t(pixel >> 8); pixels[2 * y + 1] = uint8_t(pixel);
        }
        write_column(0, (rings_[0] + dim_column_) % display_ram_columns, pixels);
        ++dim_column_;
    }
    void round_corners() {
        auto *pixels = graphics_.getFrameBuffer();
        constexpr unsigned diameter = 2 * WINDOW_CORNER_RADIUS;
        for (unsigned y = 0; y < WINDOW_CORNER_RADIUS; ++y)
            for (unsigned x = 0; x < WINDOW_CORNER_RADIUS; ++x) {
                unsigned dx = diameter - 2 * x - 1, dy = diameter - 2 * y - 1;
                if (dx * dx + dy * dy <= diameter * diameter) continue;
                // Mask after text and backgrounds so every window has the
                // same silhouette, including styled lines and error pages.
                pixels[y * 160 + x] = pixels[y * 160 + 159 - x] = 0;
                pixels[(127 - y) * 160 + x] = pixels[(127 - y) * 160 + 159 - x] = 0;
            }
    }
    unsigned text_y(unsigned field, unsigned row) const {
        const auto &positioned = painting_.lines[field];
        if (positioned.y >= 0) return unsigned(positioned.y) + row * positioned.graphics.line_height + positioned.graphics.top;
        if (painting_.three_bands) {
            const unsigned start = field * 128 / 3, end = (field + 1) * 128 / 3;
            const unsigned height = line_rows(positioned) * positioned.graphics.line_height;
            return start + (height < end - start ? (end - start - height) / 2 : 0)
                + row * positioned.graphics.line_height + positioned.graphics.top;
        }
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
        unsigned length = unsigned(std::strlen(line.text));
        return line.wrap && line.graphics.font == 0 && length
            ? ShowGraphics::wrapped_position(line, length - 1).row + 1 : 1;
    }
    void backgrounds(bool preserve_text = false) {
        if (painting_.three_bands) {
            for (unsigned i = 0; i < 3; ++i) {
                const auto &line = painting_.lines[i];
                if (!line.text[0]) continue;
                const bool heading = painting_.heading_band && i == 0;
                const unsigned inset = heading ? 0 : painting_.graphics.pad;
                for (unsigned y = i * 128 / 3; y < (i + 1) * 128 / 3; ++y)
                    for (unsigned x = inset; x < 160u - inset; ++x)
                        if (!preserve_text || (line.stippled && ((x + y) & 1)))
                            graphics_.getFrameBuffer()[y * 160 + x] = line.graphics.background;
            }
            return;
        }
        unsigned start = painting_.heading_band ? 0 : painting_.graphics.pad;
        for (unsigned i = 0; i < cntrl::max_lines; ++i) {
            const auto &line = painting_.lines[i];
            if (!line.text[0]) continue;
            if (preserve_text && line.y >= 0 && line.graphics.font != 255) {
                // Positioned fields can share a row. Stipple only this field's ink bounds.
                if (line.stippled) {
                    graphics_.select_font(line.graphics.font, line.graphics.size);
                    int16_t left, top; uint16_t width, height;
                    graphics_.getTextBounds(line.text, line.x, text_y(i, 0), &left, &top, &width, &height);
                    for (int y = std::max(0, int(top)); y < std::min(128, int(top) + height); ++y)
                        for (int x = std::max(0, int(left)); x < std::min(160, int(left) + width); ++x)
                            if ((x + y) & 1) graphics_.getFrameBuffer()[y * 160 + x] = line.graphics.background;
                }
                continue;
            }
            unsigned end = 128 - painting_.graphics.pad;
            for (unsigned j = i + 1; j < cntrl::max_lines; ++j) if (painting_.lines[j].text[0]) {
                unsigned bottom = text_y(i, line_rows(line) - 1) - line.graphics.top + line.graphics.line_height;
                end = (bottom + text_y(j, 0) - painting_.lines[j].graphics.top) / 2;
                break;
            }
            const bool heading = painting_.heading_band && i == 0;
            if (heading) end = text_y(i, 0) - line.graphics.top + line.graphics.line_height + 2;
            const unsigned inset = heading ? 0 : painting_.graphics.pad;
            for (unsigned y = start; y < end && y < 128u - inset; ++y)
                for (unsigned x = inset; x < 160u - inset; ++x)
                    if (!preserve_text || (line.stippled && ((x + y) & 1)))
                        graphics_.getFrameBuffer()[y * 160 + x] = line.graphics.background;
            start = end;
        }
    }
    uint16_t moving_pixel(unsigned panel, unsigned x, unsigned y, unsigned progress) const {
        if (animation_pane_ >= 0 || !panorama_) {
            int source = int(x) + (undo_ ? int(progress) : -int(progress));
            if (source >= 0 && source < 160) return previous_[panel][y * 160 + unsigned(source)];
            source += undo_ ? -160 : 160;
            return cache_[panel][y * 160 + unsigned(source)];
        }
        // Sample one continuous panorama, including the physical gaps.
        // Send carries candidate -> current -> history to the right.
        int source = int(panel * display_pitch + x)
            + (undo_ ? int(progress) : -int(progress));
        int pane = source / int(display_pitch);
        int column = source % int(display_pitch);
        if (column < 0) {
            column += display_pitch;
            --pane;
        }
        if (column >= 160) {
            return 0;
        }
        unsigned index = y * 160 + unsigned(column);
        if (keep_right_ && undo_ && pane == 2) return cache_[1][index];
        if (pane < 0) {
            return cache_[0][index];
        }
        if (pane > 2) {
            return cache_[2][index];
        }
        return previous_[pane][index];
    }
    uint16_t vertical_pixel(unsigned panel, unsigned x, unsigned y, unsigned progress) const {
        int source = int(y) + (undo_ ? -int(progress) : int(progress));
        bool incoming = source < 0 || source >= 128;
        source = (source + 128) % 128;
        return incoming ? cache_[panel][unsigned(source) * 160 + x]
                        : previous_[panel][unsigned(source) * 160 + x];
    }
    void animate_vertical(uint32_t now) {
        if (!vertical_column_ && !vertical_panel_) {
            if (display_dma_pending()) return;
            // Finish every small displacement, even when a full redraw runs
            // past its deadline. Skipping frames turns the movement into a wipe.
            constexpr unsigned step = VERTICAL_STEP_PIXELS;
            const uint64_t deadline = uint64_t(progress_ + step) * duration_ms_ * 1000 / 128;
            if (uint32_t(now - started_) < deadline) return;
            progress_ += step;
        }
        auto *band = acquire_display_band();
        if (!band) return;
        while (vertical_panel_ < 3 && !moving_panel(vertical_panel_)) ++vertical_panel_;
        const unsigned panel = vertical_panel_;
        const unsigned column = (rings_[panel] + vertical_column_) % display_ram_columns;
        // A DMA rectangle cannot wrap across the controller's RAM boundary.
        const unsigned width = std::min({8u, 160 - vertical_column_, display_ram_columns - (column + 1) % display_ram_columns});
        unsigned first = 128, end = 0;
        for (unsigned x = 0; x < width; ++x) for (unsigned y = 0; y < 128; ++y)
            if (vertical_pixel(panel, vertical_column_ + x, y, progress_)
                != vertical_pixel(panel, vertical_column_ + x, y, progress_ - VERTICAL_STEP_PIXELS)) {
                first = std::min(first, y); end = std::max(end, y + 1);
            }
        if (end > first) {
            band->panel = panel; band->column = column; band->columns = width;
            band->first = first; band->count = end - first;
            unsigned offset = 0;
            for (unsigned x = 0; x < width; ++x) for (unsigned y = first; y < end; ++y) {
                uint16_t pixel = display_pixel(panel, vertical_pixel(panel, vertical_column_ + x, y, progress_));
                band->pixels[offset++] = uint8_t(pixel >> 8);
                band->pixels[offset++] = uint8_t(pixel);
            }
            queue_display_band(*band);
        }
        vertical_column_ += width;
        if (vertical_column_ == 160) {
            vertical_column_ = 0;
            do { ++vertical_panel_; } while (vertical_panel_ < 3 && !moving_panel(vertical_panel_));
            if (vertical_panel_ < 3) return;
            vertical_panel_ = 0;
            if (progress_ == 128) {
                animating_ = false;
                for (auto &dirty : dirty_) dirty = true;
            }
        }
    }
    void animate(uint32_t now) {
        if (vertical_) { animate_vertical(now); return; }
        if (anchor_) {
            // Sample on the poll after the blocking scroll commands return.
            quiet_since_ = now;
            anchor_ = false;
            waiting_ = true;
            return;
        }
        if (waiting_) {
            if (uint32_t(now - quiet_since_) < 500) {
                return;
            }
            waiting_ = false;
        }
        const unsigned distance = animation_pane_ >= 0 || !panorama_ ? 160 : display_pitch;
        if (progress_ == distance) {
            // Do not reconcile the final image until the final offset settles.
            for (unsigned panel = 0; panel < 3; ++panel) if (moving_panel(panel)) {
                for (unsigned y = 0; y < 128; ++y) {
                    for (unsigned x = 0; x < 160; ++x) {
                        cache_[panel][y * 160 + x] = moving_pixel(panel, x, y, progress_);
                    }
                }
            }
            animating_ = false;
            for (auto &dirty : dirty_) {
                dirty = true;
            }
            return;
        }
        if (progress_ % 2 == 0) {
            const uint64_t deadline = uint64_t(progress_ / 2 + 1) * duration_ms_ * 1000 / (distance / 2);
            if (uint32_t(now - started_) < deadline) {
                return;
            }
        }
        ++progress_;
        unsigned x = undo_ ? 159 : 0;
        uint8_t pixels[256];
        for (unsigned panel = 0; panel < 3; ++panel) if (moving_panel(panel)) {
            rings_[panel] = (rings_[panel] + (undo_ ? 1 : display_ram_columns - 1)) % display_ram_columns;
            for (unsigned y = 0; y < 128; ++y) {
                uint16_t pixel = moving_pixel(panel, x, y, progress_);
                pixels[2 * y] = uint8_t(pixel >> 8);
                pixels[2 * y + 1] = uint8_t(pixel);
            }
            write_column(panel, (rings_[panel] + x) % display_ram_columns, pixels);
        }
        if (progress_ % 2 == 0) {
            // Both hidden columns are prepared before the two-column reveal.
            if (animation_pane_ < 0 && !keep_right_ && rings_[0] == rings_[1] && rings_[1] == rings_[2])
                scroll_display_panorama(rings_[0]);
            else for (unsigned panel = 0; panel < 3; ++panel) if (moving_panel(panel))
                scroll_display_panel(panel, rings_[panel]);
            anchor_ = true;
        }
    }
    cntrl::Pane requested_[3], painting_;
    ShowGraphics graphics_;
    uint16_t cache_[3][160 * 128] = {};
    uint16_t previous_[3][160 * 128] = {};
    bool preparing_ = false, animating_ = false, undo_ = false, native_ready_ = false;
    int animation_pane_ = -1;
    bool keep_right_ = false, vertical_ = false, panorama_ = true;
    unsigned vertical_column_ = 0, vertical_panel_ = 0;
    uint8_t pixel_brightness_ = 128, requested_brightness_ = 128;
    unsigned dim_column_ = 160;
    unsigned rings_[3] = {}, progress_ = 0;
    unsigned duration_ms_ = 0;
    uint32_t started_ = 0, quiet_since_ = 0;
    bool anchor_ = false, waiting_ = false;
    bool dirty_[3] = {true, true, true};
    bool valid_[3] = {};
    int drawing_ = -1;
    bool rendering_ = false;
    unsigned y_ = 0, field_ = 0, letter_ = 0;
};
