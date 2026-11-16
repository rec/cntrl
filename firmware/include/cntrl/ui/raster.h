#pragma once
#include <cntrl/hardware.h>
#include "display.h"
#include "fonts.h"
#include <algorithm>
#include <cstring>
#include <climits>

namespace cntrl::ui {
// Half-open pixel bounds. An empty region contributes no damage.
struct Region {
    int left = 0, top = 0, right = 0, bottom = 0;
    bool empty() const { return left >= right || top >= bottom; }
    void include(int x, int y, unsigned width, unsigned height) {
        if (!width || !height) return;
        const int end_x = x + int(width), end_y = y + int(height);
        if (empty()) { left = x; top = y; right = end_x; bottom = end_y; }
        else { left = std::min(left, x); top = std::min(top, y); right = std::max(right, end_x); bottom = std::max(bottom, end_y); }
    }
};
class Raster {
public:
    Raster(const Fonts &fonts, uint16_t *pixels) : fonts_(fonts), pixels_(pixels) {}
    void configure(Viewport viewport, const ScreenGraphics &settings, Region region = {0, 0, INT_MAX, INT_MAX}) {
        viewport_ = viewport; settings_ = settings;
        clip_ = {std::max(0, region.left), std::max(0, region.top), std::min(int(viewport.width), region.right), std::min(int(viewport.height), region.bottom)};
        if (!clip_.empty()) for (int y = clip_.top; y < clip_.bottom; ++y)
            std::fill(pixels_ + y * viewport.width + clip_.left, pixels_ + y * viewport.width + clip_.right, settings.background);
    }
    uint16_t *pixels() { return pixels_; }
    const uint16_t *pixels() const { return pixels_; }
    unsigned rows(const TextLine &line) const {
        if (!line.wrap || !line.text[0]) return 1;
        return position(line, unsigned(std::strlen(line.text) - 1)).row + 1;
    }
    struct Position { int x = 0; unsigned row = 0; };
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(raster_localized_position)
#endif
    static Position localized_position(const Fonts &fonts, const TextLine &line, unsigned index, int width) {
        Position result; Glyph current; int previous = 0; bool separated = true;
        const char *cursor = line.text;
        for (unsigned i = 0; i <= index; ++i) {
            const char *start = cursor; uint32_t code;
            if (!utf8::next(cursor, code) || !fonts.glyph(line.graphics.font, line.graphics.size, line.graphics.asset, line.text, i, current)) break;
            result.x += i ? current.x - previous : 0; previous = current.x;
            if (separated && code != ' ' && result.x > 0) {
                const char *word = start; unsigned number = i; uint32_t character; Glyph next;
                int right = current.x;
                while (utf8::next(word, character) && character != ' '
                    && fonts.glyph(line.graphics.font, line.graphics.size, line.graphics.asset, line.text, number++, next))
                    right = std::max(right, next.x + int(next.width));
                if (result.x + right - current.x > width) { result.x = 0; ++result.row; }
            }
            if (result.x + int(current.width) > width && result.x > 0) { result.x = 0; ++result.row; }
            separated = code == ' ';
        }
        return result;
    }
    bool intersects(const TextLine &line, int y) const {
        if (!line.text[0]) return false;
        const auto b = fonts_.bounds(line.graphics.font, line.graphics.size, line.graphics.asset, line.text);
        const int x = line.wrap ? 0 : line.centered ? (int(viewport_.width) - b.width) / 2 : line.x + b.left;
        const int width = line.wrap ? int(viewport_.width) : b.width;
        const int height = b.height + (line.wrap ? int(rows(line) - 1) * line.graphics.line_height : 0);
        return x < clip_.right && x + width > clip_.left && y + b.top < clip_.bottom && y + b.top + height > clip_.top;
    }
    Position position(const TextLine &line, unsigned index) const {
        if (line.wrap && line.graphics.font == localized_face)
            return localized_position(fonts_, line, index, int(viewport_.width) - int(settings_.pad) - int(line.x));
        Position result; Glyph current;
        int previous = 0;
        for (unsigned i = 0; i <= index; ++i) {
            if (!fonts_.glyph(line.graphics.font, line.graphics.size, line.graphics.asset, line.text, i, current)) break;
            const int advance = i ? current.x - previous : 0;
            previous = current.x;
            result.x += advance;
            if (line.wrap && line.x + result.x + int(current.width) > int(viewport_.width - settings_.pad)) { result.x = 0; ++result.row; }
        }
        return result;
    }
    bool letter(const TextLine &line, unsigned index, int y) {
        Glyph glyph;
        if (!fonts_.glyph(line.graphics.font, line.graphics.size, line.graphics.asset, line.text, index, glyph)) return false;
        int x = line.x;
        if (line.centered) {
            const auto b = fonts_.bounds(line.graphics.font, line.graphics.size, line.graphics.asset, line.text);
            x = (int(viewport_.width) - b.width) / 2 - b.left;
        }
        if (line.wrap) { const auto p = position(line, index); x += p.x - glyph.x; y += p.row * line.graphics.line_height; }
        const auto color = index < 64 && (line.color_mask & (uint64_t(1) << index)) ? line.alternate_color : line.graphics.foreground;
        const int origin_x = x + glyph.x, origin_y = y + glyph.y;
        const int first_x = std::max(0, std::max(int(settings_.pad), clip_.left) - origin_x), first_y = std::max(0, std::max(int(settings_.pad), clip_.top) - origin_y);
        const int end_x = std::min(int(glyph.width), std::min(int(viewport_.width) - int(settings_.pad), clip_.right) - origin_x);
        const int end_y = std::min(int(glyph.height), std::min(int(viewport_.height) - int(settings_.pad), clip_.bottom) - origin_y);
        for (int row = first_y; row < end_y; ++row) for (int col = first_x; col < end_x; ++col)
            if (glyph.pixels[row * ((glyph.width + 7) / 8) + col / 8] & (0x80 >> (col % 8)))
                pixel(origin_x + col, origin_y + row, color, settings_.pad);
        return true;
    }
    void pixel(int x, int y, uint16_t color, unsigned inset = 0) {
        if (x >= std::max(int(inset), clip_.left) && y >= std::max(int(inset), clip_.top)
            && x < std::min(int(viewport_.width) - int(inset), clip_.right) && y < std::min(int(viewport_.height) - int(inset), clip_.bottom))
            pixels_[unsigned(y) * viewport_.width + unsigned(x)] = color;
    }
    void rectangle(int x, int y, unsigned width, unsigned height, uint16_t color, bool stipple = false) {
        const int right = std::min(clip_.right, x + int(width)), bottom = std::min(clip_.bottom, y + int(height));
        for (int row = std::max(clip_.top, y); row < bottom; ++row) for (int col = std::max(clip_.left, x); col < right; ++col)
            if (!stipple || ((row + col) & 1)) pixel(col, row, color);
    }
    void circle(const Circle &c) {
        const int span = c.diameter + 2 * c.annulus;
        if (!c.diameter) return;
        const int left = int(c.x) - span / 2, top = int(c.y) - span / 2;
        const int end_x = std::min(clip_.right, left + span), end_y = std::min(clip_.bottom, top + span);
        for (int y = std::max(clip_.top, top); y < end_y; ++y) for (int x = std::max(clip_.left, left); x < end_x; ++x) {
            const int64_t dx = 2 * (x - left) + 1 - span, dy = 2 * (y - top) + 1 - span;
            const int64_t distance = dx * dx + dy * dy;
            if (distance <= int64_t(span) * span) pixel(x, y, distance <= int64_t(c.diameter) * c.diameter ? c.color : c.ring_color);
        }
    }
    void toggle(const ToggleIcon &t) {
        if (!t.visible) return;
        const unsigned height = std::max(4u, viewport_.height * 7 / 64), width = height * 13 / 7;
        const int radius = int(height) / 2;
        for (unsigned row = 0; row < height; ++row) for (unsigned col = 0; col < width; ++col) {
            const int dx = int(col) < radius ? 2 * int(col) + 1 - int(height) : int(col) >= int(width) - radius ? 2 * int(col) + 1 - 2 * int(width) + int(height) : 0;
            const int dy = 2 * int(row) + 1 - int(height);
            if (dx * dx + dy * dy > int(height * height)) continue;
            const int knob = 2 * int(col) + 1 - (t.enabled ? 2 * int(width) - int(height) : int(height));
            pixel(t.x + col, t.y + row, knob * knob + dy * dy <= (int(height) - 4) * (int(height) - 4) ? (t.disabled ? 0xc618 : 0xffff) : t.disabled ? 0x8410 : t.enabled ? 0x07e0 : 0);
        }
    }
    void corners() {
        const unsigned radius = std::min({viewport_.corner_radius, viewport_.width / 2, viewport_.height / 2}), diameter = 2 * radius;
        for (unsigned y = 0; y < radius; ++y) for (unsigned x = 0; x < radius; ++x) {
            const unsigned dx = diameter - 2 * x - 1, dy = diameter - 2 * y - 1;
            if (dx * dx + dy * dy <= diameter * diameter) continue;
            pixel(x, y, 0); pixel(viewport_.width - 1 - x, y, 0);
            pixel(x, viewport_.height - 1 - y, 0); pixel(viewport_.width - 1 - x, viewport_.height - 1 - y, 0);
        }
    }
private:
    const Fonts &fonts_;
    uint16_t *pixels_;
    Viewport viewport_;
    ScreenGraphics settings_;
    Region clip_;
};
} // namespace cntrl::ui
