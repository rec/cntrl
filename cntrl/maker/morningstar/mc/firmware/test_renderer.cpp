#include "patch_switcher/renderer.h"
#include <cassert>
#include <cstring>

static uint16_t screens[3][128][160];
static unsigned writes = 0, pixels_sent = 0;

void write_display_segment(unsigned panel, unsigned x, unsigned y, const uint8_t *pixels, unsigned count) {
    assert(panel < 3 && x + count <= 160 && y < 128 && count > 0);
    ++writes; pixels_sent += count;
    for (unsigned i = 0; i < count; ++i)
        screens[panel][y][x + i] = uint16_t(pixels[2 * i]) << 8 | pixels[2 * i + 1];
}

static void drain(DisplayRenderer &renderer) {
    for (unsigned i = 0; i < 3 * 512; ++i) {
        unsigned before = writes, glyphs = ST7735_t3::glyphs;
        renderer.draw();
        assert(writes - before <= 1 && ST7735_t3::glyphs - glyphs <= 1);
        assert(writes == before || ST7735_t3::glyphs == glyphs);
    }
}

static void blank(unsigned panel, uint16_t background = 0) {
    for (const auto &row : screens[panel]) for (uint16_t pixel : row) assert(pixel == background);
}

static void padding(unsigned panel, unsigned pad, uint16_t color) {
    for (unsigned y = 0; y < 128; ++y) for (unsigned x = 0; x < 160; ++x)
        if (x < pad || y < pad || x >= 160 - pad || y >= 128 - pad)
            assert(screens[panel][y][x] == color);
}

int main() {
    std::memset(screens, 123, sizeof(screens));
    DisplayRenderer renderer;
    cntrl::Pane empty;
    drain(renderer);
    assert(writes == 384 && pixels_sent == 3 * 160 * 128);
    for (unsigned i = 0; i < 3; ++i) blank(i);
    unsigned before = writes;
    renderer.show(0, empty); renderer.show(3, empty); drain(renderer);
    assert(writes == before);

    cntrl::Pane view;
    std::strcpy(view.category, "trumpet");
    std::strcpy(view.patch, "A very long patch name that must be clipped");
    view.category_x = 40; view.patch_x = 4;
    view.graphics.foreground = 0xf800; view.graphics.background = 0x001f;
    renderer.show(2, view); drain(renderer);
    assert(screens[2][4][40] == 0xf800 && screens[2][14][4] == 0xf800);
    assert(screens[2][3][40] == 0x001f);
    padding(2, 4, 0x001f);
    before = writes;
    renderer.show(2, view); drain(renderer); assert(writes == before);

    std::strcpy(view.patch, "Trumpet");
    renderer.show(2, view); drain(renderer);
    assert(screens[2][14][80] == 0x001f);
    renderer.show(2, empty); renderer.draw();
    view.graphics.pad = 20; view.graphics.size = 16; view.graphics.line_height = 16;
    view.category_x = view.patch_x = 0;
    renderer.show(2, view); drain(renderer);
    padding(2, 20, 0x001f);
    assert(screens[2][38][24] == 0xf800);
    blank(0); blank(1);
    renderer.show(2, empty); drain(renderer); blank(2);

    view = empty;
    view.graphics.background = 0x07e0;
    renderer.show(1, view); drain(renderer);
    blank(1, 0x07e0);
    assert(ShowGraphics::supports_font(0, 8) && ShowGraphics::supports_font(1, 13));
    assert(ShowGraphics::supports_font(2, 96) && !ShowGraphics::supports_font(3, 8));
    assert(!ShowGraphics::supports_font(0, 13) && !ShowGraphics::supports_font(1, 15));
}
