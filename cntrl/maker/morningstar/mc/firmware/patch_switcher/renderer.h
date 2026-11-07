#pragma once
#include <cntrl/ui/renderer.h>
#include "panorama.h"
#include "target.h"

// The controller accepts column-major rectangles. Layout and pixels come from
// the portable renderer; this adapter only arbitrates the shared display bus.
class BoardDisplay final : public cntrl::ui::Display {
public:
    unsigned count() const override { return 3; }
    cntrl::ui::Viewport viewport(unsigned) const override { return {160, 128, display_gap, 8}; }
    void poll() override { poll_display_dma(); start_display_dma(); }
    bool ready() const override { return !display_dma_pending(); }
    bool failed() override { return take_display_dma_error(); }
    bool priority_due() const override { return switch_scan_due(); }
    bool submit(unsigned panel, unsigned x, unsigned y, unsigned width, unsigned height, const uint8_t *pixels) override {
        if (!initialized_) { initialize_display_panorama(); initialized_ = true; }
        auto *band = acquire_display_band();
        if (!band || width > 8 || height > 128) return false;
        band->panel = panel; band->column = x; band->columns = width; band->first = y; band->count = height;
        std::memcpy(band->pixels, pixels, width * height * 2);
        queue_display_band(*band); start_display_dma(); return true;
    }
    unsigned scroll_columns(unsigned) const override { return display_ram_columns; }
    unsigned scroll_quantum(unsigned) const override { return 2; }
    unsigned scroll_settle_us() const override { return 500; }
    bool ring_column(unsigned panel, unsigned column, const uint8_t *pixels, unsigned height) override {
        return submit(panel, column, 0, 1, height, pixels);
    }
    void scroll(unsigned panel, unsigned offset) override { scroll_display_panel(panel, offset); }
    bool scroll_all(unsigned offset) override { scroll_display_panorama(offset); return true; }
    void reset_scroll() override { initialize_display_panorama(); initialized_ = true; }
private:
    bool initialized_ = false;
};
struct RenderPixels {
    uint16_t work[160 * 128] = {};
    uint16_t cache[3 * 160 * 128] = {};
    uint16_t previous[3 * 160 * 128] = {};
};
