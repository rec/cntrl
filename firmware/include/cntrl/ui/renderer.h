#pragma once
#include "raster.h"
#include <optional>
#include <vector>
#include <cstdio>

namespace cntrl::ui {
// One glyph or a bounded region per poll. Pixel storage belongs to the target,
// so memory placement does not leak into layout or rendering policy.
class Renderer {
public:
    Renderer(Display &display, const Fonts &fonts, uint16_t *work, unsigned work_capacity,
             uint16_t *cache, uint16_t *previous, unsigned storage_capacity)
        : display_(display), fonts_(fonts), raster_(fonts, work), cache_(cache), previous_(previous) {
        unsigned offset = 0, maximum = 0;
        for (unsigned i = 0; i < display.count(); ++i) {
            const auto v = display.viewport(i); const unsigned area = v.width * v.height;
            screens_.push_back({v, offset, Pane{}, true, false, 0}); offset += area; maximum = std::max(maximum, area);
            transfer_.resize(std::max(transfer_.size(), size_t(v.height * 8 * 2)));
        }
        usable_ = maximum <= work_capacity && offset <= storage_capacity && !screens_.empty();
    }
    void target_brightness(uint8_t value) { requested_brightness_ = std::min(value, uint8_t(128)); }
    bool ready() const {
        if (!usable_ || !display_.ready() || preparing_ || animating_ || drawing_ >= 0) return false;
        for (const auto &screen : screens_) if (screen.dirty) return false;
        return true;
    }
    void invalidate() {
        drawing_ = -1; preparing_ = animating_ = false;
        for (auto &screen : screens_) { screen.dirty = true; screen.valid = false; screen.ring = 0; }
        reset_scroll_ = true;
    }
    void show(unsigned pane, const Pane &content) {
        if (pane >= screens_.size()) return;
        auto &screen = screens_[pane];
        if (!std::memcmp(&screen.requested, &content, sizeof(content))) return;
        screen.requested = content; screen.dirty = true;
    }
    void error(const char *message) {
        if (screens_.empty()) return;
        const unsigned screen = screens_.size() / 2;
        Pane pane; pane.graphics = screens_[screen].requested.graphics;
        pane.graphics.font = 0; pane.graphics.size = 8; pane.graphics.asset = 0;
        pane.lines[0].graphics = pane.graphics;
        std::snprintf(pane.lines[0].text, sizeof(pane.lines[0].text), "%s", message); show(screen, pane);
    }
    void transition(bool undo, std::optional<unsigned> duration, int pane = -1, unsigned reserved = 0, bool vertical = false, bool panorama = true) {
        if (!usable_ || preparing_ || animating_ || !display_.ready()) return;
        for (const auto &screen : screens_) if (!screen.valid) return;
        for (const auto &screen : screens_) std::copy(cache_ + screen.offset, cache_ + screen.offset + screen.viewport.width * screen.viewport.height, previous_ + screen.offset);
        drawing_ = -1; undo_ = undo; selected_ = pane; reserved_ = std::min(reserved, unsigned(screens_.size())); vertical_ = vertical; panorama_ = panorama;
        duration_ = duration.value_or(vertical ? 200 : 75); preparing_ = true;
        for (auto &screen : screens_) screen.dirty = true;
    }
    void draw(uint32_t now = 0) {
        display_.poll();
        if (!usable_ || display_.priority_due()) return;
        if (display_.failed()) { invalidate(); error("Display transfer failed"); }
        if (!display_.ready()) return;
        if (reset_scroll_) { display_.reset_scroll(); reset_scroll_ = false; }
        if (animating_) { animate(now); return; }
        if (drawing_ < 0) {
            for (unsigned i = 0; i < screens_.size(); ++i) if (screens_[i].dirty) {
                drawing_ = int(i); auto &screen = screens_[i]; screen.dirty = false;
                painting_ = screen.requested; column_ = field_ = letter_ = 0;
                raster_.configure(screen.viewport, painting_.graphics); backgrounds();
                for (const auto &c : painting_.circles) raster_.circle(c);
                raster_.toggle(painting_.toggle); rendering_ = true; break;
            }
        }
        if (drawing_ < 0) {
            if (preparing_) { preparing_ = false; animating_ = true; started_ = now; progress_ = 0; animation_screen_ = animation_column_ = 0;
                ring_scroll_ = !vertical_; ring_step_ = false; reveal_ = settling_ = settle_anchor_ = false;
                for (unsigned i = 0; i < screens_.size(); ++i) if (moving(i) && (display_.scroll_columns(i) <= screens_[i].viewport.width || screens_[i].viewport.width != screens_[0].viewport.width || screens_[i].viewport.gap != screens_[0].viewport.gap)) ring_scroll_ = false; }
            else dim();
            return;
        }
        auto &screen = screens_[unsigned(drawing_)];
        if (rendering_) {
            const auto &line = painting_.lines[field_];
            if (raster_.letter(line, letter_, text_y(field_))) ++letter_;
            else { letter_ = 0; if (++field_ == max_lines) { backgrounds(true); raster_.corners(); rendering_ = false; } }
            return;
        }
        const auto v = screen.viewport; bool changed = !screen.valid;
        for (unsigned y = 0; y < v.height; ++y) if (raster_.pixels()[y * v.width + column_] != cache_[screen.offset + y * v.width + column_]) changed = true;
        if (changed && !preparing_) {
            for (unsigned y = 0; y < v.height; ++y) encode(y, dim_pixel(unsigned(drawing_), raster_.pixels()[y * v.width + column_]));
            if (!submit_column(unsigned(drawing_), column_, v.height)) return;
        }
        for (unsigned y = 0; y < v.height; ++y) cache_[screen.offset + y * v.width + column_] = raster_.pixels()[y * v.width + column_];
        if (++column_ == v.width) { screen.valid = true; drawing_ = -1; }
    }
private:
    struct Screen { Viewport viewport; unsigned offset; Pane requested; bool dirty = true, valid = false; unsigned ring = 0; };
    Display &display_;
    const Fonts &fonts_;
    Raster raster_;
    uint16_t *cache_, *previous_;
    std::vector<Screen> screens_;
    std::vector<uint8_t> transfer_;
    Pane painting_;
    bool usable_ = false, preparing_ = false, animating_ = false, rendering_ = false, undo_ = false, vertical_ = false, panorama_ = true;
    bool reset_scroll_ = true, ring_scroll_ = false, ring_step_ = false, reveal_ = false, settling_ = false, settle_anchor_ = false;
    unsigned reveal_screen_ = 0;
    uint32_t settled_ = 0;
    unsigned reserved_ = 0;
    int drawing_ = -1, selected_ = -1;
    unsigned column_ = 0, field_ = 0, letter_ = 0, progress_ = 0, duration_ = 0, animation_screen_ = 0, animation_column_ = 0, dim_column_ = 0;
    uint32_t started_ = 0;
    uint8_t brightness_ = 128, requested_brightness_ = 128;
    void encode(unsigned index, uint16_t pixel) { transfer_[2 * index] = uint8_t(pixel >> 8); transfer_[2 * index + 1] = uint8_t(pixel); }
    uint16_t dim_pixel(unsigned screen, uint16_t pixel) const {
        if (screen || brightness_ == 128) return pixel;
        return uint16_t((((pixel >> 11) * brightness_ + 64) / 128) << 11 |
            (((((pixel >> 5) & 63) * brightness_ + 64) / 128) << 5) | (((pixel & 31) * brightness_ + 64) / 128));
    }
    void dim() {
        auto &screen = screens_[0]; if (!screen.valid) return;
        if (!dim_column_) { if (brightness_ == requested_brightness_) return; brightness_ = requested_brightness_; }
        for (unsigned y = 0; y < screen.viewport.height; ++y) encode(y, dim_pixel(0, cache_[screen.offset + y * screen.viewport.width + dim_column_]));
        if (submit_column(0, dim_column_, screen.viewport.height)) dim_column_ = (dim_column_ + 1) % screen.viewport.width;
    }
    int text_y(unsigned field) const {
        const auto &line = painting_.lines[field];
        if (line.y >= 0) return line.y + line.graphics.top;
        const unsigned height = screens_[unsigned(drawing_)].viewport.height;
        if (painting_.three_bands) {
            const unsigned start = field * height / 3, end = (field + 1) * height / 3, used = raster_.rows(line) * line.graphics.line_height;
            return int(start + (used < end - start ? (end - start - used) / 2 : 0) + line.graphics.top);
        }
        unsigned count = 0, used = 0, preceding = 0, index = 0;
        for (unsigned i = 0; i < max_lines; ++i) if (painting_.lines[i].text[0]) {
            const auto &item = painting_.lines[i]; unsigned rows = raster_.rows(item);
            if (i < field) { preceding += rows * item.graphics.line_height; index += rows; }
            count += rows; used += rows * item.graphics.line_height;
        }
        const unsigned pad = painting_.graphics.pad, available = height > 2 * pad ? height - 2 * pad : 0;
        return int(pad + preceding + (used < available && count ? (available - used) * (2 * index + 1) / (2 * count) : 0) + line.graphics.top);
    }
    void backgrounds(bool preserve = false) {
        const auto v = screens_[unsigned(drawing_)].viewport;
        unsigned start = painting_.heading_band ? 0 : painting_.graphics.pad;
        for (unsigned i = 0; i < max_lines; ++i) {
            const auto &line = painting_.lines[i]; if (!line.text[0]) continue;
            if (preserve && line.y >= 0) {
                if (line.stippled) {
                    auto b = fonts_.bounds(line.graphics.font, line.graphics.size, line.graphics.asset, line.text);
                    const int x = line.centered ? (int(v.width) - b.width) / 2 : line.x + b.left;
                    raster_.rectangle(x, text_y(i) + b.top, b.width, b.height, line.graphics.background, true);
                }
                continue;
            }
            unsigned end = v.height > painting_.graphics.pad ? v.height - painting_.graphics.pad : 0;
            if (painting_.three_bands) { start = i * v.height / 3; end = (i + 1) * v.height / 3; }
            else for (unsigned j = i + 1; j < max_lines; ++j) if (painting_.lines[j].text[0]) {
                const int bottom = text_y(i) - line.graphics.top + raster_.rows(line) * line.graphics.line_height;
                end = unsigned(std::max(0, (bottom + text_y(j) - painting_.lines[j].graphics.top) / 2)); break;
            }
            const bool heading = painting_.heading_band && i == 0;
            if (heading && !painting_.three_bands) end = unsigned(std::max(0, text_y(i) - line.graphics.top + line.graphics.line_height + 2));
            const unsigned inset = heading ? 0 : painting_.graphics.pad;
            end = std::min(end, v.height > inset ? v.height - inset : 0);
            if ((!preserve || line.stippled) && end > start && v.width > 2 * inset)
                raster_.rectangle(inset, start, v.width - 2 * inset, end - start, line.graphics.background, preserve);
            start = end;
        }
    }
    bool moving(unsigned screen) const { return selected_ >= 0 ? screen == unsigned(selected_) : screen + reserved_ < screens_.size(); }
    unsigned distance() const {
        unsigned result = 1;
        for (unsigned i = 0; i < screens_.size(); ++i) if (moving(i)) result = std::max(result, vertical_ ? screens_[i].viewport.height : screens_[i].viewport.width + (panorama_ && selected_ < 0 ? screens_[i].viewport.gap : 0));
        return result;
    }
    uint16_t sample(unsigned screen, unsigned x, unsigned y) const {
        const auto &s = screens_[screen]; const auto v = s.viewport;
        const unsigned shift = progress_ * (vertical_ ? v.height : v.width + (panorama_ && selected_ < 0 ? v.gap : 0)) / distance();
        if (vertical_) {
            int source = int(y) + (undo_ ? -int(shift) : int(shift)); const bool incoming = source < 0 || source >= int(v.height);
            source = (source + int(v.height)) % int(v.height);
            return (incoming ? cache_ : previous_)[s.offset + unsigned(source) * v.width + x];
        }
        int source = int(x) + (undo_ ? int(shift) : -int(shift));
        if (selected_ >= 0 || !panorama_) {
            const bool incoming = source < 0 || source >= int(v.width); source = (source + int(v.width)) % int(v.width);
            return (incoming ? cache_ : previous_)[s.offset + y * v.width + unsigned(source)];
        }
        int pane = int(screen);
        while (source < 0 && pane > 0) { --pane; source += int(screens_[unsigned(pane)].viewport.width + screens_[unsigned(pane)].viewport.gap); }
        while (pane + 1 < int(screens_.size()) && source >= int(screens_[unsigned(pane)].viewport.width + screens_[unsigned(pane)].viewport.gap)) { source -= int(screens_[unsigned(pane)].viewport.width + screens_[unsigned(pane)].viewport.gap); ++pane; }
        const auto &p = screens_[unsigned(pane)];
        if (source < 0 || (pane + 1 == int(screens_.size()) && source >= int(p.viewport.width + p.viewport.gap))) {
            source += source < 0 ? int(p.viewport.width + p.viewport.gap) : -int(p.viewport.width + p.viewport.gap);
            if (source < 0 || source >= int(p.viewport.width)) return 0;
            return cache_[p.offset + std::min(y, p.viewport.height - 1) * p.viewport.width + unsigned(source)];
        }
        if (source >= int(p.viewport.width) || y >= p.viewport.height) return 0;
        if (reserved_ && undo_ && unsigned(pane) >= screens_.size() - reserved_ && screens_.size() > reserved_) { const auto &left = screens_[screens_.size() - reserved_ - 1]; return cache_[left.offset + std::min(y, left.viewport.height - 1) * left.viewport.width + std::min(unsigned(source), left.viewport.width - 1)]; }
        return previous_[p.offset + y * p.viewport.width + unsigned(source)];
    }
    bool submit_column(unsigned screen, unsigned column, unsigned height) {
        const unsigned columns = display_.scroll_columns(screen);
        return columns ? display_.ring_column(screen, (screens_[screen].ring + column) % columns, transfer_.data(), height)
            : display_.submit(screen, column, 0, 1, height, transfer_.data());
    }
    void animate_ring(uint32_t now) {
        if (settle_anchor_) {
            settled_ = now; settle_anchor_ = false; settling_ = true; return;
        }
        if (settling_) {
            if (uint32_t(now - settled_) < display_.scroll_settle_us()) return;
            settling_ = false;
        }
        if (reveal_) {
            if (!reveal_screen_ && selected_ < 0 && !reserved_) {
                bool same = true;
                for (const auto &s : screens_) if (s.ring != screens_[0].ring) same = false;
                if (same && display_.scroll_all(screens_[0].ring)) { reveal_ = false; settle_anchor_ = true; return; }
            }
            while (reveal_screen_ < screens_.size() && !moving(reveal_screen_)) ++reveal_screen_;
            if (reveal_screen_ < screens_.size()) { display_.scroll(reveal_screen_, screens_[reveal_screen_].ring); ++reveal_screen_; return; }
            reveal_ = false; settle_anchor_ = true; return;
        }
        if (!ring_step_) {
            if (progress_ == distance()) {
                for (unsigned i = 0; i < screens_.size(); ++i) if (moving(i)) {
                    const auto &s = screens_[i];
                    for (unsigned y = 0; y < s.viewport.height; ++y) for (unsigned x = 0; x < s.viewport.width; ++x)
                        raster_.pixels()[y * s.viewport.width + x] = sample(i, x, y);
                    std::copy(raster_.pixels(), raster_.pixels() + s.viewport.width * s.viewport.height, cache_ + s.offset);
                }
                animating_ = false; for (auto &s : screens_) s.dirty = true; return;
            }
            unsigned quantum = 1;
            for (unsigned i = 0; i < screens_.size(); ++i) if (moving(i)) quantum = std::max(quantum, display_.scroll_quantum(i));
            const unsigned deadline = std::min(distance(), (progress_ / quantum + 1) * quantum);
            if (uint64_t(uint32_t(now - started_)) < uint64_t(deadline) * duration_ * 1000 / distance()) return;
            ++progress_; ring_step_ = true;
        }
        while (animation_screen_ < screens_.size() && !moving(animation_screen_)) ++animation_screen_;
        if (animation_screen_ == screens_.size()) { animating_ = false; return; }
        auto &screen = screens_[animation_screen_];
        const auto v = screen.viewport; const unsigned columns = display_.scroll_columns(animation_screen_);
        const unsigned ring = (screen.ring + (undo_ ? 1 : columns - 1)) % columns;
        const unsigned x = undo_ ? v.width - 1 : 0;
        for (unsigned y = 0; y < v.height; ++y) encode(y, dim_pixel(animation_screen_, sample(animation_screen_, x, y)));
        if (!display_.ring_column(animation_screen_, (ring + x) % columns, transfer_.data(), v.height)) return;
        screen.ring = ring;
        do { ++animation_screen_; } while (animation_screen_ < screens_.size() && !moving(animation_screen_));
        if (animation_screen_ == screens_.size()) {
            animation_screen_ = 0; ring_step_ = false; unsigned quantum = 1;
            for (unsigned i = 0; i < screens_.size(); ++i) if (moving(i)) quantum = std::max(quantum, display_.scroll_quantum(i));
            if (!(progress_ % quantum) || progress_ == distance()) { reveal_ = true; reveal_screen_ = 0; }
        }
    }
    void animate(uint32_t now) {
        if (ring_scroll_) { animate_ring(now); return; }
        if (!animation_screen_ && !animation_column_) {
            const unsigned next = std::min(distance(), progress_ + std::max(1u, distance() / 4));
            if (uint64_t(uint32_t(now - started_)) < uint64_t(next) * duration_ * 1000 / distance()) return;
            progress_ = next;
        }
        while (animation_screen_ < screens_.size() && !moving(animation_screen_)) ++animation_screen_;
        if (animation_screen_ == screens_.size()) { animating_ = false; return; }
        auto &screen = screens_[animation_screen_]; const auto v = screen.viewport;
        const unsigned width = display_.scroll_columns(animation_screen_) ? 1u : std::min(8u, v.width - animation_column_);
        for (unsigned x = 0; x < width; ++x) for (unsigned y = 0; y < v.height; ++y) encode(x * v.height + y, dim_pixel(animation_screen_, sample(animation_screen_, animation_column_ + x, y)));
        if (!(width == 1 ? submit_column(animation_screen_, animation_column_, v.height) : display_.submit(animation_screen_, animation_column_, 0, width, v.height, transfer_.data()))) return;
        animation_column_ += width;
        if (animation_column_ == v.width) { animation_column_ = 0; do { ++animation_screen_; } while (animation_screen_ < screens_.size() && !moving(animation_screen_)); }
        if (animation_screen_ == screens_.size()) {
            animation_screen_ = 0;
            if (progress_ == distance()) { animating_ = false; for (auto &s : screens_) { s.valid = false; s.dirty = true; } }
        }
    }
};
} // namespace cntrl::ui
