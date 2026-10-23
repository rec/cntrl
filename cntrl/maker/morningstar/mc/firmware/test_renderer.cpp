#include "patch_switcher/renderer.h"
#include <cassert>
#include <cstring>

static uint8_t screens[3][128][160];
static unsigned writes = 0, pixels_sent = 0;

void write_display_segment(unsigned panel, unsigned x, unsigned y, const uint8_t *pixels, unsigned count) {
    assert(panel < 3 && x + count <= 160 && y < 128 && count > 0);
    ++writes;
    pixels_sent += count;
    for (unsigned i = 0; i < count; ++i) {
        assert(pixels[2 * i] == pixels[2 * i + 1]);
        screens[panel][y][x + i] = pixels[2 * i];
    }
}

static void drain(DisplayRenderer &renderer) {
    for (unsigned i = 0; i < 3 * 512; ++i) {
        unsigned before = writes;
        unsigned glyphs = ST7735_t3::glyphs;
        renderer.draw();
        assert(writes - before <= 1); // Bound work between switch scans.
        assert(ST7735_t3::glyphs - glyphs <= 1);
        assert(writes == before || ST7735_t3::glyphs == glyphs);
    }
}

static void blank(unsigned panel) {
    for (const auto &row : screens[panel]) for (uint8_t pixel : row) assert(pixel == 0);
}

int main() {
    std::memset(screens, 123, sizeof(screens)); // Unknown power-on display RAM.
    DisplayRenderer renderer;
    cntrl::Pane empty;
    drain(renderer);
    assert(writes == 384 && pixels_sent == 3 * 160 * 128);
    for (unsigned i = 0; i < 3; ++i) blank(i);
    unsigned before = writes;
    renderer.show(0, empty);
    renderer.show(3, empty);
    drain(renderer);
    assert(writes == before);

    cntrl::Pane full;
    std::strcpy(full.title, "NEXT");
    std::strcpy(full.instrument, "pman");
    std::strcpy(full.category, "trumpet");
    std::strcpy(full.patch, "A very long patch name that wraps over multiple lines");
    renderer.show(2, full);
    pixels_sent = 0;
    drain(renderer);
    assert(pixels_sent > 0 && pixels_sent < 160 * 128);
    assert(screens[2][0][0] == 255); // Title.
    assert(screens[2][30][0] == 255); // Patch, enlarged to 12x16 cells.
    assert(screens[2][46][0] == 255); // Wrapped second line.
    before = writes;
    renderer.show(2, full);
    drain(renderer);
    assert(writes == before);

    cntrl::Pane shorter = full;
    std::strcpy(shorter.patch, "Trumpet");
    renderer.show(2, shorter);
    drain(renderer);
    assert(screens[2][30][0] == 255 && screens[2][46][0] == 0);
    renderer.show(2, full);
    renderer.draw();
    // A newer request during painting must converge without a stale cache.
    renderer.show(2, empty);
    drain(renderer);
    blank(2);
    renderer.show(1, shorter);
    drain(renderer);
    assert(screens[1][30][0] == 255 && screens[1][46][0] == 0);
    blank(0);
    blank(2);
    renderer.show(1, empty);
    drain(renderer);
    blank(1);

    cntrl::Pane crowded;
    std::memset(crowded.title, 'T', sizeof(crowded.title) - 1);
    std::memset(crowded.instrument, 'I', sizeof(crowded.instrument) - 1);
    std::memset(crowded.category, 'C', sizeof(crowded.category) - 1);
    std::memset(crowded.patch, 'P', sizeof(crowded.patch) - 1);
    renderer.show(0, crowded);
    drain(renderer);
    assert(screens[0][70][0] == 255 && screens[0][70][5] == 0); // Reduced to 6x8.
    assert(screens[0][78][0] == 255); // Wrap at the smaller size.
}
