#pragma once
#include <cntrl/hardware.h>
#include <cstring>
#include <cstdio>
#include "displays.h"
#include "show_graphics.h"
#include "panorama.h"

constexpr unsigned DEFAULT_TRANSITION_MS = 75;
constexpr unsigned WINDOW_CORNER_RADIUS = 8;

// One glyph/column per repaint poll; one three-panel column step per animation poll.
class DisplayRenderer {
public:
    bool ready() const {
        return !preparing_ && !animating_ && drawing_ < 0 && !dirty_[0] && !dirty_[1] && !dirty_[2];
    }
    void invalidate() {
        drawing_ = -1; rendering_ = false;
        preparing_ = animating_ = native_ready_ = false;
        for (auto &ring : rings_) ring = 0;
        for (unsigned i = 0; i < 3; ++i) { valid_[i] = false; dirty_[i] = true; }
    }
    void transition(bool undo, std::optional<unsigned> duration_ms, bool target_only = false) {
        if (preparing_ || animating_ || !valid_[0] || !valid_[1] || !valid_[2]) return;
        // Browsing may still be repainting the candidate. Animate from the
        // pixels already on screen instead of silently dropping the transition.
        drawing_ = -1; rendering_ = false;
        std::memcpy(previous_, cache_, sizeof(cache_));
        undo_ = undo; target_only_ = target_only;
        duration_ms_ = duration_ms.value_or(DEFAULT_TRANSITION_MS); preparing_ = true;
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
        view.graphics.font = 0; view.graphics.size = 8; view.graphics.asset = 0;
        view.graphics.line_height = 8; view.graphics.top = 0;
        view.lines[0].graphics = view.graphics;
        view.lines[0].x = view.graphics.pad;
        std::snprintf(view.lines[0].text, sizeof(view.lines[0].text), "%s", message);
        show(1, view);
    }
    // now is a microsecond timestamp, sampled before this bounded poll.
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
                progress_ = 0; started_ = now; anchor_ = waiting_ = false;
            }
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
            write_display_column(unsigned(drawing_), (rings_[drawing_] + y_) % display_ram_columns, pixels);
        if (++y_ == 160) {
            valid_[drawing_] = true;
            drawing_ = -1;
        }
    }
private:
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
        unsigned start = painting_.heading_band ? 0 : painting_.graphics.pad;
        for (unsigned i = 0; i < cntrl::max_lines; ++i) {
            const auto &line = painting_.lines[i];
            if (!line.text[0]) continue;
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
        if (target_only_) {
            int source = int(x) + (undo_ ? int(progress) : -int(progress));
            if (source >= 0 && source < 160) return previous_[0][y * 160 + unsigned(source)];
            source += undo_ ? -160 : 160;
            return cache_[0][y * 160 + unsigned(source)];
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
        if (pane < 0) {
            return cache_[0][index];
        }
        if (pane > 2) {
            return cache_[2][index];
        }
        return previous_[pane][index];
    }
    void animate(uint32_t now) {
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
        const unsigned distance = target_only_ ? 160 : display_pitch;
        if (progress_ == distance) {
            // Do not reconcile the final image until the final offset settles.
            for (unsigned panel = 0; panel < (target_only_ ? 1u : 3u); ++panel) {
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
        for (unsigned panel = 0; panel < (target_only_ ? 1u : 3u); ++panel) {
            rings_[panel] = (rings_[panel] + (undo_ ? 1 : display_ram_columns - 1)) % display_ram_columns;
            for (unsigned y = 0; y < 128; ++y) {
                uint16_t pixel = moving_pixel(panel, x, y, progress_);
                pixels[2 * y] = uint8_t(pixel >> 8);
                pixels[2 * y + 1] = uint8_t(pixel);
            }
            write_display_column(panel, (rings_[panel] + x) % display_ram_columns, pixels);
        }
        if (progress_ % 2 == 0) {
            // Both hidden columns are prepared before the two-column reveal.
            if (!target_only_ && rings_[0] == rings_[1] && rings_[1] == rings_[2])
                scroll_display_panorama(rings_[0]);
            else for (unsigned panel = 0; panel < (target_only_ ? 1u : 3u); ++panel)
                scroll_display_panel(panel, rings_[panel]);
            anchor_ = true;
        }
    }
    cntrl::Pane requested_[3], painting_;
    ShowGraphics graphics_;
    uint16_t cache_[3][160 * 128] = {};
    uint16_t previous_[3][160 * 128] = {};
    bool preparing_ = false, animating_ = false, undo_ = false, native_ready_ = false;
    bool target_only_ = false;
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
