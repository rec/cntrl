#pragma once
#include <cntrl/hardware.h>
#include <algorithm>
#include <optional>

namespace cntrl::ui {
// A selection and its animated position, in fractions of a row. The page
// supplies the dimensions and draws each item into the existing text fields.
class Picker {
public:
    void reset(unsigned count, unsigned selected = 0) {
        count_ = count;
        selected_ = count ? std::min(selected, count - 1) : 0;
        origin_ = target();
        scroll_ = 0;
    }
    unsigned count() const { return count_; }
    unsigned selected() const { return selected_; }
    bool move(bool previous, uint32_t now) {
        if (!count_ || (previous ? !selected_ : selected_ + 1 == count_)) return false;
        origin_ = position(now);
        if (previous) --selected_; else ++selected_;
        started_ = now;
        return true;
    }
    bool changed(uint32_t now, uint32_t refreshed) const {
        return position(now) != position(refreshed);
    }
    template<class Item>
    void paint(Pane &pane, Rectangle bounds, unsigned row_height, uint32_t now,
               Rectangle &highlight, unsigned first_line, Item item,
               std::optional<bool> fixed = std::nullopt) {
        if (!count_ || !bounds.width || !bounds.height || !row_height || first_line >= max_lines) return;
        const unsigned slots = max_lines - first_line;
        const bool fits = count_ <= std::min(slots, bounds.height / row_height);
        const int offset = int(int64_t(position(now)) * row_height / units_per_row);
        int highlight_y = bounds.y + (int(bounds.height) - int(row_height)) / 2;
        if (fixed.value_or(fits)) {
            // Reserve room for a partial row when the visible rows use all fields.
            if (!fits) {
                const unsigned field_height = std::max(row_height, (slots - 1) * row_height + 1);
                bounds.height = uint16_t(std::min(unsigned(bounds.height), field_height));
            }
            const int travel = std::max(0, int(bounds.height) - int(row_height));
            const int last_scroll = std::max(0, int(count_ * row_height) - int(bounds.height));
            scroll_ = std::clamp(scroll_, std::max(0, offset - travel), std::min(offset, last_scroll));
            highlight_y = bounds.y + offset - scroll_;
        } else scroll_ = 0;
        const int top = highlight_y - offset;
        const int bottom = bounds.y + bounds.height;
        unsigned index = unsigned(std::max(0, (int(bounds.y) - top) / int(row_height)));
        int y = top + int(index * row_height);
        unsigned line = first_line;
        for (; index < count_ && y < bottom && line < max_lines; ++index, ++line, y += int(row_height))
            item(index, pane.lines[line], y);
        pane.text_clip = bounds;
        pane.first_clipped_line = uint8_t(first_line);
        pane.clipped_lines = uint8_t(line - first_line);
        const int highlight_top = std::max(int(bounds.y), highlight_y);
        highlight.x = bounds.x;
        highlight.y = int16_t(highlight_top);
        highlight.width = bounds.width;
        highlight.height = uint16_t(std::min(bottom, highlight_y + int(row_height)) - highlight_top);
    }
private:
    static constexpr int units_per_row = 256;
    static constexpr uint32_t duration_ms = 140;
    unsigned count_ = 0, selected_ = 0;
    int origin_ = 0, scroll_ = 0;
    uint32_t started_ = 0;

    int target() const { return int(selected_) * units_per_row; }
    int position(uint32_t now) const {
        const uint32_t elapsed = now - started_;
        if (elapsed >= duration_ms) return target();
        return origin_ + (target() - origin_) * int(elapsed) / int(duration_ms);
    }
};
} // namespace cntrl::ui
