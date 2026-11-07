#include "patch_switcher/renderer.h"
#include "usb_probe/buttons.h"
#include <cassert>
#include <cstring>

static DisplayBand band;
static bool busy = false, queued = false, due = false, error = false;
static unsigned initialized = 0, ring = 0, panel = 0, all = 0;
DisplayBand *acquire_display_band() { return queued || busy ? nullptr : &band; }
void queue_display_band(DisplayBand &) { queued = true; }
void poll_display_dma() {}
void start_display_dma() { if (queued && !due) { queued = false; busy = true; } }
bool display_dma_pending() { return busy || queued; }
bool display_spi_busy() { return busy; }
bool switch_scan_due() { return due; }
bool take_display_dma_error() { bool value = error; error = false; return value; }
void initialize_display_panorama() { ++initialized; }
void scroll_display_panel(unsigned p, unsigned offset) { panel = p; ring = offset; }
void scroll_display_panorama(unsigned offset) { ++all; ring = offset; }
int main() {
    for (unsigned i = 0; i < sizeof(button_bits); ++i) {
        const uint8_t raw = uint8_t(0xffu ^ (1u << button_bits[i]));
        for (unsigned j = 0; j < sizeof(button_bits); ++j) {
            cntrl::ButtonState button;
            const bool pressed = !(raw & (1u << button_bits[j]));
            assert(!button.update(pressed, 0));
            assert(button.update(pressed, 15) == (i == j));
            assert(button.pressed == (i == j));
        }
    }
    BoardDisplay display;
    assert(display.count() == 3);
    const auto viewport = display.viewport(2);
    assert(viewport.width == 160 && viewport.height == 128 && viewport.gap == display_gap);
    uint8_t pixels[2 * 8 * 128]; std::memset(pixels, 0x73, sizeof(pixels));
    assert(display.submit(2, 20, 10, 8, 100, pixels));
    assert(initialized == 1 && band.panel == 2 && band.column == 20 && band.first == 10 && band.columns == 8 && band.count == 100);
    assert(!display.ready());
    std::memset(pixels, 0x42, sizeof(pixels));
    for (unsigned i = 0; i < 2 * 8 * 100; ++i) assert(band.pixels[i] == 0x73);
    assert(!display.submit(1, 0, 0, 1, 128, pixels)); busy = false;
    due = true; assert(display.priority_due());
    assert(display.ring_column(1, 161, pixels, 128)); assert(queued && !busy);
    display.poll(); assert(queued && !busy); due = false;
    display.poll(); assert(!queued && busy); busy = false;
    display.scroll(1, 78); assert(panel == 1 && ring == 78);
    assert(display.scroll_all(19)); assert(all == 1 && ring == 19);
    display.reset_scroll(); assert(initialized == 2);
    error = true; assert(display.failed()); assert(!display.failed());
}
