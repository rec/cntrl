#pragma once
#include <cstdint>
#include <algorithm>

namespace cntrl::ui {
struct Viewport {
    unsigned width = 0, height = 0, gap = 0, corner_radius = 8;
};
struct Display {
    virtual ~Display() = default;
    virtual unsigned count() const = 0;
    virtual Viewport viewport(unsigned screen) const = 0;
    virtual void poll() = 0;
    virtual bool ready() const = 0;
    virtual bool failed() = 0;
    // A due input scan takes priority after completing a transfer.
    virtual bool priority_due() const = 0;
    // RGB565, big endian, columns first. On acceptance bytes remain borrowed
    // until ready() becomes true. Rejection leaves both ownership and state alone.
    virtual bool submit(unsigned screen, unsigned x, unsigned y, unsigned width, unsigned height, const uint8_t *pixels) = 0;
    // Optional horizontal ring surface. Zero disables the optimization. The
    // renderer owns logical offsets; adapters implement controller commands.
    virtual unsigned scroll_columns(unsigned) const { return 0; }
    virtual unsigned scroll_quantum(unsigned) const { return 1; }
    virtual unsigned scroll_settle_us() const { return 0; }
    virtual bool ring_column(unsigned, unsigned, const uint8_t *, unsigned) { return false; }
    virtual void scroll(unsigned, unsigned) {}
    virtual bool scroll_all(unsigned) { return false; }
    virtual void reset_scroll() {}
};
inline unsigned minimum_display_width(const Display &display) {
    if (!display.count()) return 0;
    unsigned width = display.viewport(0).width;
    for (unsigned i = 1; i < display.count(); ++i) width = std::min(width, display.viewport(i).width);
    return width;
}
inline unsigned minimum_display_height(const Display &display) {
    if (!display.count()) return 0;
    unsigned height = display.viewport(0).height;
    for (unsigned i = 1; i < display.count(); ++i) height = std::min(height, display.viewport(i).height);
    return height;
}

} // namespace cntrl::ui
