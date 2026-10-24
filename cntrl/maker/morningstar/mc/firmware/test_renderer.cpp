#include "patch_switcher/renderer.h"
#include <cassert>
#include <cstring>

static uint16_t screens[3][128][160];
static uint16_t ram[3][128][160];
static unsigned ring = 0;
static bool check_motion = false, check_undo = false;
static unsigned checked_progress = 0;
static unsigned writes = 0, pixels_sent = 0;
static bool patch_only = false;

void scroll_display_panorama(unsigned offset) {
    if (check_motion) checked_progress += check_undo ? (ring + 160 - offset) % 160 : (offset + 160 - ring) % 160;
    ring = offset;
    for (unsigned panel = 0; panel < 3; ++panel)
        for (unsigned y = 0; y < 128; ++y)
            for (unsigned x = 0; x < 160; ++x) screens[panel][y][x] = ram[panel][y][(ring + x) % 160];
    if (check_motion) {
        assert(checked_progress <= display_pitch);
        for (unsigned panel = 0; panel < 3; ++panel) for (unsigned x = 0; x < 160; ++x) {
            auto sample = panorama_sample(panel, x, checked_progress, check_undo);
            uint16_t expected = sample.x >= 160 ? 0 : uint16_t(0x1111 * (sample.pane + (check_undo ? 2 : 1)));
            for (unsigned y = 0; y < 128; ++y) assert(screens[panel][y][x] == expected);
        }
    }
}

void initialize_display_panorama() { scroll_display_panorama(0); }

void write_display_column(unsigned panel, unsigned column, const uint8_t *pixels) {
    assert(panel < 3 && column < 160);
    ++writes; pixels_sent += 128;
    unsigned x = (column + 160 - ring) % 160;
    for (unsigned y = 0; y < 128; ++y) {
        uint16_t pixel = uint16_t(pixels[2 * y]) << 8 | pixels[2 * y + 1];
        if (patch_only) {
            assert(panel == 2);
            if (y < 14 || y >= 22) assert(screens[panel][y][x] == pixel);
        }
        ram[panel][y][column] = screens[panel][y][x] = pixel;
    }
}

static void drain(DisplayRenderer &renderer) {
    for (unsigned i = 0; i < 3 * 512; ++i) {
        unsigned before = writes, glyphs = ST7735_t3::glyphs;
        renderer.draw();
        assert(writes - before <= 1 && ST7735_t3::glyphs - glyphs <= 1);
        assert(writes == before || ST7735_t3::glyphs == glyphs);
    }
    assert(renderer.ready());
}

static void blank(unsigned panel, uint16_t background = 0xffff) {
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
    assert(!renderer.ready());
    cntrl::Pane empty;
    drain(renderer);
    assert(writes == 480 && pixels_sent == 3 * 160 * 128);
    for (unsigned i = 0; i < 3; ++i) blank(i);
    cntrl::Pane no_setups;
    no_setups.centered = true; no_setups.graphics.size = no_setups.graphics.line_height = 16;
    std::strcpy(no_setups.patch, "No setups");
    renderer.show(0, no_setups); drain(renderer);
    assert(screens[0][56][26] == 0 && screens[0][55][26] == 0xffff);
    renderer.show(0, empty); drain(renderer);
    cntrl::Pane editor;
    editor.graphics.size = editor.graphics.line_height = 16;
    editor.wrap_patch = true;
    std::strcpy(editor.category, "Patch");
    std::memset(editor.patch, 'X', 63); editor.patch[63] = 0;
    renderer.show(0, editor); drain(renderer);
    for (unsigned row = 0; row < 6; ++row) assert(screens[0][22 + 16 * row][4] == 0);
    assert(screens[0][102][40] == 0xffff); // Last row has only three characters.
    padding(0, 4, 0xffff);
    renderer.show(0, empty); drain(renderer);
    unsigned before = writes;
    renderer.show(0, empty); renderer.show(3, empty); drain(renderer);
    assert(writes == before);

    cntrl::Pane view;
    std::strcpy(view.category, "trumpet");
    std::strcpy(view.patch, "A very long patch name that must be clipped");
    view.category_x = 40; view.patch_x = 4;
    view.graphics.foreground = 0xf800; view.graphics.background = 0x001f;
    view.patch_color = view.graphics.foreground;
    renderer.show(2, view); drain(renderer);
    assert(screens[2][4][40] == 0xf800 && screens[2][14][4] == 0xf800);
    assert(screens[2][3][40] == 0x001f);
    padding(2, 4, 0x001f);
    before = writes;
    renderer.show(2, view); drain(renderer); assert(writes == before);
    std::memset(screens, 123, sizeof(screens)); // Splash bypassed the cached pixels.
    renderer.invalidate(); assert(!renderer.ready()); drain(renderer);
    assert(writes == before + 480);
    padding(2, 4, 0x001f); blank(0); blank(1);

    view.patch_color = view.graphics.half = 0x780f;
    patch_only = true;
    renderer.show(2, view); drain(renderer);
    assert(screens[2][4][40] == 0xf800 && screens[2][14][4] == 0x780f);
    view.patch_color = view.graphics.foreground;
    renderer.show(2, view); drain(renderer);
    assert(screens[2][4][40] == 0xf800 && screens[2][14][4] == 0xf800);
    patch_only = false;

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

    // A complete send shifts C/R left and introduces the new grey candidate.
    for (unsigned i = 0; i < 3; ++i) {
        cntrl::Pane pane; pane.graphics.background = uint16_t(0x1111 * (i + 1));
        renderer.show(i, pane);
    }
    drain(renderer);
    renderer.transition(false, 2000);
    check_motion = true; checked_progress = 0;
    for (unsigned i = 0; i < 3; ++i) {
        cntrl::Pane pane; pane.graphics.background = uint16_t(0x1111 * (i + 2));
        renderer.show(i, pane);
    }
    for (unsigned now = 0; now < 1000; ++now)
        for (unsigned work = 0; work < 8; ++work) renderer.draw(now);
    assert(!renderer.ready() && checked_progress < display_pitch);
    for (unsigned now = 1000; now < 3000; ++now)
        for (unsigned work = 0; work < 8; ++work) renderer.draw(now);
    assert(renderer.ready());
    check_motion = false; assert(checked_progress == display_pitch);
    for (unsigned i = 0; i < 3; ++i) blank(i, uint16_t(0x1111 * (i + 2)));
    assert(ring == display_pitch % 160);
    renderer.transition(true, 500);
    check_motion = check_undo = true; checked_progress = 0;
    for (unsigned i = 0; i < 3; ++i) {
        cntrl::Pane pane; pane.graphics.background = uint16_t(0x1111 * (i + 1));
        renderer.show(i, pane);
    }
    for (unsigned now = 3000; now < 4500; ++now)
        for (unsigned work = 0; work < 8; ++work) renderer.draw(now);
    assert(renderer.ready() && ring == 0);
    check_motion = false; assert(checked_progress == display_pitch);
    for (unsigned i = 0; i < 3; ++i) blank(i, uint16_t(0x1111 * (i + 1)));
    assert(transition_progress(1000, 2000) == display_pitch / 2);
    assert(transition_progress(1999, 2000) < display_pitch);
    assert(transition_progress(2000, 2000) == display_pitch);
    assert(transition_progress(500, 500) == display_pitch);
    for (unsigned panel = 0; panel < 3; ++panel) for (unsigned x = 0; x < 160; ++x) {
        auto left = panorama_sample(panel, x, display_pitch, false);
        assert(left.pane == int(panel) + 1 && left.x == x);
        auto right = panorama_sample(panel, x, display_pitch, true);
        assert(right.pane == int(panel) - 1 && right.x == x);
    }
}
