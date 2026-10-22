#include "patch_switcher/renderer.h"
#include <array>
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

static void drain(DisplayRenderer &renderer, const unsigned char *font) {
    for (unsigned i = 0; i < 3 * 128; ++i) {
        unsigned before = writes;
        renderer.draw(font);
        assert(writes - before <= 1); // Bound work between switch scans.
    }
}

static void matches(unsigned panel, const cntrl::Pane &pane, const unsigned char *font) {
    for (unsigned y = 0; y < 128; ++y)
        for (unsigned x = 0; x < 160; ++x)
            assert(screens[panel][y][x] == (cntrl::text_pixel(pane, 160, 128, x, y, font) ? 255 : 0));
}

int main() {
    // Distinct fake glyphs exercise wrapping, erasure and changes of scale.
    std::array<unsigned char, 1280> font;
    for (unsigned i = 0; i < font.size(); ++i) font[i] = uint8_t((i * 37) & 127);
    std::memset(screens, 123, sizeof(screens)); // Unknown power-on display RAM.
    DisplayRenderer renderer;
    cntrl::Pane empty;
    drain(renderer, font.data());
    assert(writes == 384 && pixels_sent == 3 * 160 * 128);
    for (unsigned i = 0; i < 3; ++i) matches(i, empty, font.data());
    unsigned before = writes;
    renderer.show(0, empty);
    renderer.show(3, empty);
    drain(renderer, font.data());
    assert(writes == before);

    cntrl::Pane full;
    std::strcpy(full.title, "NEXT");
    std::strcpy(full.instrument, "pman");
    std::strcpy(full.category, "trumpet");
    std::strcpy(full.patch, "A very long patch name that wraps over multiple lines");
    renderer.show(2, full);
    pixels_sent = 0;
    drain(renderer, font.data());
    assert(pixels_sent > 0 && pixels_sent < 160 * 128);
    matches(2, full, font.data());
    before = writes;
    renderer.show(2, full);
    drain(renderer, font.data());
    assert(writes == before);

    cntrl::Pane shorter = full;
    std::strcpy(shorter.patch, "Trumpet");
    renderer.show(2, shorter);
    renderer.draw(font.data());
    // A newer request during painting must converge without a stale cache.
    renderer.show(2, empty);
    drain(renderer, font.data());
    drain(renderer, font.data());
    matches(2, empty, font.data());
    renderer.show(1, shorter);
    drain(renderer, font.data());
    matches(1, shorter, font.data());
    matches(0, empty, font.data());
    matches(2, empty, font.data());
    renderer.show(1, empty);
    drain(renderer, font.data());
    matches(1, empty, font.data());
}
