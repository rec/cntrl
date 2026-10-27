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
            if (y < 90 || y >= 98) assert(screens[panel][y][x] == pixel);
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
    {
        ShowGraphics graphics;
        cntrl::ScreenGraphics settings;
        settings.font = 5; settings.size = 32;
        graphics.configure(settings);
        graphics.setCursor(10, 10); graphics.write('S');
        uint16_t before[160 * 128];
        std::memcpy(before, graphics.getFrameBuffer(), sizeof(before));
        // An overlapping space must preserve the previous letter's strokes.
        graphics.setCursor(10, 10); graphics.write(' ');
        assert(std::memcmp(before, graphics.getFrameBuffer(), sizeof(before)) == 0);
    }
    std::memset(screens, 123, sizeof(screens));
    DisplayRenderer renderer;
    assert(!renderer.ready());
    cntrl::Pane empty;
    drain(renderer);
    assert(writes == 480 && pixels_sent == 3 * 160 * 128);
    for (unsigned i = 0; i < 3; ++i) blank(i);
    cntrl::Pane no_setups;
    no_setups.lines[1].centered = true;
    no_setups.lines[1].graphics.size = no_setups.lines[1].graphics.line_height = 16;
    std::strcpy(no_setups.lines[1].text, "No setups");
    renderer.show(0, no_setups); drain(renderer);
    assert(screens[0][56][26] == 0 && screens[0][55][26] == 0xffff);
    renderer.show(0, empty); drain(renderer);
    cntrl::Pane editor;
    editor.lines[0].graphics.size = editor.lines[0].graphics.line_height = 16;
    editor.lines[1].graphics.size = editor.lines[1].graphics.line_height = 16;
    editor.lines[1].wrap = true;
    std::strcpy(editor.lines[0].text, "Patch");
    std::memset(editor.lines[1].text, 'X', 63); editor.lines[1].text[63] = 0;
    renderer.show(0, editor); drain(renderer);
    for (unsigned y : {21u, 38u, 56u, 73u, 90u, 107u}) assert(screens[0][y][4] == 0);
    assert(screens[0][107][40] == 0xffff); // Last row has only three characters.
    padding(0, 4, 0xffff);
    renderer.show(0, empty); drain(renderer);
    unsigned before = writes;
    renderer.show(0, empty); renderer.show(3, empty); drain(renderer);
    assert(writes == before);

    cntrl::Pane scaled;
    std::strcpy(scaled.lines[0].text, "A"); std::strcpy(scaled.lines[1].text, "B");
    scaled.lines[0].x = scaled.lines[1].x = 4;
    scaled.lines[1].graphics.foreground = 0;
    scaled.lines[0].graphics.size = scaled.lines[0].graphics.line_height = 32;
    scaled.lines[1].graphics.size = scaled.lines[1].graphics.line_height = 16;
    renderer.show(0, scaled); drain(renderer);
    assert(screens[0][22][4] == 0 && screens[0][21][4] == 0xffff);
    assert(screens[0][49][4] == 0 && screens[0][50][4] == 0xffff);
    assert(screens[0][90][4] == 0 && screens[0][103][4] == 0);
    assert(screens[0][104][4] == 0xffff && screens[0][89][4] == 0xffff);
    padding(0, 4, 0xffff);
    scaled.lines[0].text[0] = 0;
    renderer.show(0, scaled); drain(renderer);
    assert(screens[0][56][4] == 0 && screens[0][55][4] == 0xffff);
    assert(screens[0][69][4] == 0 && screens[0][70][4] == 0xffff);
    scaled.lines[1].text[0] = 0;
    renderer.show(0, scaled); drain(renderer); blank(0);
    renderer.show(0, empty); drain(renderer);

    cntrl::Pane bands;
    for (unsigned i = 0; i < 3; ++i) {
        std::strcpy(bands.lines[i].text, "A");
        bands.lines[i].graphics.background = uint16_t(1u << i);
    }
    renderer.show(0, bands); drain(renderer);
    assert(screens[0][4][150] == 1 && screens[0][43][150] == 1);
    assert(screens[0][44][150] == 2 && screens[0][83][150] == 2);
    assert(screens[0][84][150] == 4 && screens[0][123][150] == 4);
    assert(screens[0][3][150] == 0xffff && screens[0][124][150] == 0xffff);
    bands.lines[1].graphics.background = 8;
    renderer.show(0, bands); drain(renderer);
    assert(screens[0][44][150] == 8 && screens[0][83][150] == 8);
    renderer.show(0, empty); drain(renderer);

    cntrl::Pane view;
    std::strcpy(view.lines[0].text, "trumpet");
    std::strcpy(view.lines[1].text, "A very long patch name that must be clipped");
    view.lines[0].x = 40; view.lines[1].x = 4;
    view.graphics.foreground = 0xf800; view.graphics.background = 0x001f;
    view.lines[0].graphics = view.lines[1].graphics = view.graphics;
    view.lines[1].graphics.foreground = view.graphics.foreground;
    renderer.show(2, view); drain(renderer);
    assert(screens[2][30][40] == 0xf800 && screens[2][90][4] == 0xf800);
    assert(screens[2][29][40] == 0x001f);
    padding(2, 4, 0x001f);
    before = writes;
    renderer.show(2, view); drain(renderer); assert(writes == before);
    std::memset(screens, 123, sizeof(screens)); // Splash bypassed the cached pixels.
    renderer.invalidate(); assert(!renderer.ready()); drain(renderer);
    assert(writes == before + 480);
    padding(2, 4, 0x001f); blank(0); blank(1);

    uint16_t solid[128][160];
    std::memcpy(solid, screens[2], sizeof(solid));
    view.lines[1].stippled = true;
    patch_only = true;
    renderer.show(2, view); drain(renderer);
    for (unsigned y = 0; y < 128; ++y) for (unsigned x = 0; x < 160; ++x) {
        bool hole = y >= 64 && y < 124 && x >= 4 && x < 156 && ((x + y) & 1);
        assert(screens[2][y][x] == (hole ? view.lines[1].graphics.background : solid[y][x]));
    }
    assert(screens[2][90][4] == 0xf800 && screens[2][90][5] == 0x001f);
    view.lines[1].stippled = false;
    renderer.show(2, view); drain(renderer);
    assert(std::memcmp(solid, screens[2], sizeof(solid)) == 0);
    patch_only = false;

    std::strcpy(view.lines[1].text, "Trumpet");
    renderer.show(2, view); drain(renderer);
    assert(screens[2][90][80] == 0x001f);
    renderer.show(2, empty); renderer.draw();
    view.graphics.pad = 20; view.graphics.size = 16; view.graphics.line_height = 16;
    view.lines[0].graphics = view.lines[1].graphics = view.graphics;
    view.lines[0].x = view.lines[1].x = 0;
    renderer.show(2, view); drain(renderer);
    padding(2, 20, 0x001f);
    assert(screens[2][78][24] == 0xf800);
    blank(0); blank(1);
    renderer.show(2, empty); drain(renderer); blank(2);

    view = empty;
    view.graphics.background = 0x07e0;
    renderer.show(1, view); drain(renderer);
    blank(1, 0x07e0);
    assert(ShowGraphics::supports_font(0, 8) && ShowGraphics::supports_font(1, 13));
    assert(ShowGraphics::supports_font(2, 96) && !ShowGraphics::supports_font(3, 8));
    for (unsigned face = 3; face <= 9; ++face) {
        for (unsigned size : {12, 16, 20, 24, 28, 32, 40, 48}) {
            const auto *font = ShowGraphics::native_font(face, size);
            assert(font && font->version == 1 && font->index1_first == 32 && font->index1_last == 126);
        }
        assert(!ShowGraphics::supports_font(face, 13));
    }
    assert(!ShowGraphics::supports_font(10, 24));
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
    // An unspecified duration uses the renderer default, even after an explicit one.
    renderer.transition(false, std::nullopt);
    for (unsigned now = 4500; now < 4750; ++now)
        for (unsigned work = 0; work < 8; ++work) renderer.draw(now);
    assert(!renderer.ready());
    for (unsigned now = 4750; now < 5250; ++now)
        for (unsigned work = 0; work < 8; ++work) renderer.draw(now);
    assert(renderer.ready());
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
