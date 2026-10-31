#include "patch_switcher/renderer.h"
#include <cassert>
#include <algorithm>
#include <cstring>

static uint16_t screens[3][128][160];
static uint16_t ram[3][128][display_ram_columns];
static unsigned ring = 0;
static bool check_motion = false, check_undo = false;
static unsigned checked_progress = 0;
static unsigned writes = 0, pixels_sent = 0;
static unsigned scroll_updates = 0;
static bool patch_only = false;

static uint16_t rounded_background(unsigned x, unsigned y, uint16_t color) {
    // Expected discrete 8-pixel arc, independent of the renderer's circle test.
    constexpr unsigned cut[] = {5, 3, 2, 1, 1};
    unsigned edge = std::min(y, 127 - y);
    return edge < 5 && std::min(x, 159 - x) < cut[edge] ? 0 : color;
}

void scroll_display_panorama(unsigned offset) {
    ++scroll_updates;
    if (check_motion) checked_progress += check_undo ? (ring + display_ram_columns - offset) % display_ram_columns : (offset + display_ram_columns - ring) % display_ram_columns;
    ring = offset;
    for (unsigned panel = 0; panel < 3; ++panel)
        for (unsigned y = 0; y < 128; ++y)
                for (unsigned x = 0; x < 160; ++x) screens[panel][y][x] = ram[panel][y][(ring + x) % display_ram_columns];
    if (check_motion) {
        assert(checked_progress <= display_pitch);
        for (unsigned panel = 0; panel < 3; ++panel) for (unsigned x = 0; x < 160; ++x) {
            auto sample = panorama_sample(panel, x, checked_progress, check_undo);
            uint16_t expected = sample.x >= 160 ? 0 : uint16_t(0x1111 * (sample.pane + (check_undo ? 2 : 1)));
            for (unsigned y = 0; y < 128; ++y)
                assert(screens[panel][y][x] == (sample.x >= 160 ? 0 : rounded_background(sample.x, y, expected)));
        }
    }
}

void initialize_display_panorama() { scroll_display_panorama(0); }

void write_display_column(unsigned panel, unsigned column, const uint8_t *pixels) {
    assert(panel < 3 && column < display_ram_columns);
    ++writes; pixels_sent += 128;
    unsigned x = (column + display_ram_columns - ring) % display_ram_columns;
    // Column writes happen before the scroll register update. Updating a
    // visible column here produces the wrong-image sliver at the outgoing edge.
    if (check_motion && checked_progress < display_pitch) assert(x >= 160);
    for (unsigned y = 0; y < 128; ++y) {
        uint16_t pixel = uint16_t(pixels[2 * y]) << 8 | pixels[2 * y + 1];
        if (patch_only) {
            assert(panel == 2);
            if (y < 90 || y >= 98) assert(screens[panel][y][x] == pixel);
        }
        ram[panel][y][column] = pixel;
        if (x < 160) screens[panel][y][x] = pixel;
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
    for (unsigned y = 0; y < 128; ++y) for (unsigned x = 0; x < 160; ++x)
        assert(screens[panel][y][x] == rounded_background(x, y, background));
}

static void padding(unsigned panel, unsigned pad, uint16_t color) {
    for (unsigned y = 0; y < 128; ++y) for (unsigned x = 0; x < 160; ++x)
        if (x < pad || y < pad || x >= 160 - pad || y >= 128 - pad)
            assert(screens[panel][y][x] == rounded_background(x, y, color));
}

static void sparse_text() {
    // One two-pixel diagonal glyph, referenced twice in a prepared UTF-8 run.
    font_assets::Entry entry;
    entry.bytes[0] = 24; entry.bytes[1] = 2; entry.bytes[7] = 1;
    entry.bytes[9] = 1; entry.bytes[11] = 1;
    entry.bytes[13] = 3; entry.bytes[14] = 2;
    entry.bytes[15] = 46; entry.bytes[19] = 2;
    auto data = std::make_unique<uint8_t[]>(48);
    data[0] = 9; data[4] = 2; data[6] = 2; data[10] = 2;
    data[16] = '1'; data[22] = 3;
    data[24] = 2; data[28] = 5; data[30] = 2;
    data[32] = 0xc3; data[33] = 0xa9; data[42] = 3;
    data[46] = 128; data[47] = 64;
    assert(font_assets::valid_pixels(entry, data.get()));
    font_assets::cache.begin();
    assert(font_assets::cache.add(123, entry, std::move(data)));
    font_assets::cache.finish(true);
    ShowGraphics graphics; cntrl::ScreenGraphics settings;
    settings.font = 255; settings.asset = 123; settings.size = 24;
    settings.foreground = 0x1234; settings.background = 0xffff; settings.pad = 4;
    graphics.configure(settings);
    assert(graphics.sparse_letter(settings, "é", 0, 5, 10, false));
    assert(graphics.sparse_letter(settings, "é", 1, 5, 10, false));
    assert(!graphics.sparse_letter(settings, "é", 2, 5, 10, false));
    auto pixels = graphics.getFrameBuffer();
    assert(pixels[8 * 160 + 5] == 0x1234 && pixels[9 * 160 + 6] == 0x1234);
    assert(pixels[8 * 160 + 8] == 0x1234 && pixels[9 * 160 + 9] == 0x1234);
    assert(pixels[8 * 160 + 6] == 0xffff);
    assert(graphics.sparse_letter(settings, "11", 1, 10, 20, false));
    assert(pixels[18 * 160 + 13] == 0x1234);
    assert(!graphics.sparse_letter(settings, "2", 0, 10, 20, false));
    assert(graphics.sparse_letter(settings, "é", 0, 0, 2, false));
    assert(pixels[0] == 0xffff); // Padding clips sparse glyphs too.
    assert(graphics.sparse_letter(settings, "é", 0, 0, 30, true));
    assert(pixels[28 * 160 + 77] == 0x1234);
    assert(cntrl::label_valid(reinterpret_cast<const uint8_t *>("Café"), 6));
    assert(!cntrl::label_valid(reinterpret_cast<const uint8_t *>("\xc0\xaf"), 3));
    assert(!cntrl::label_valid(reinterpret_cast<const uint8_t *>("\xed\xa0\x80"), 4));
    font_assets::cache.begin(); font_assets::cache.finish(true);
}

int main() {
    sparse_text();
    {
        ShowGraphics graphics;
        cntrl::ScreenGraphics settings;
        settings.font = 5; settings.size = 28;
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
    assert(ShowGraphics::native_font(0, 32) == &Arial_32);
    assert(!ShowGraphics::supports_font(0, 56)); // No invented, magnified size.
    assert(!renderer.ready());
    cntrl::Pane empty;
    drain(renderer);
    assert(writes == 480 && pixels_sent == 3 * 160 * 128);
    for (unsigned i = 0; i < 3; ++i) blank(i);
    cntrl::Pane empty_sparse;
    empty_sparse.graphics.background = 0;
    empty_sparse.lines[0].graphics.font = 255;
    renderer.show(0, empty_sparse); drain(renderer); blank(0, 0);
    renderer.show(0, empty); drain(renderer); blank(0);
    cntrl::Pane corner_text;
    corner_text.graphics.pad = corner_text.lines[0].graphics.pad = 0;
    corner_text.graphics.background = 0x07e0;
    corner_text.lines[0].graphics.background = 0x001f;
    corner_text.lines[0].graphics.foreground = 0xf800;
    corner_text.lines[0].x = 0; corner_text.lines[0].y = 0;
    std::strcpy(corner_text.lines[0].text, "A");
    renderer.show(0, corner_text); drain(renderer);
    assert(screens[0][0][0] == 0 && screens[0][4][0] == 0);
    assert(screens[0][1][4] == 0xf800); // Text inside the arc survives.
    assert(screens[0][0][5] == 0x001f && screens[0][64][80] == 0x001f);
    assert(screens[0][127][159] == 0 && screens[0][127][154] == 0x001f);
    renderer.show(0, empty); drain(renderer); blank(0);
    cntrl::Pane no_setups;
    no_setups.lines[1].centered = true;
    no_setups.lines[1].graphics.size = no_setups.lines[1].graphics.line_height = 16;
    std::strcpy(no_setups.lines[1].text, "No setups");
    renderer.show(0, no_setups); drain(renderer);
    assert(screens[0][56][26] == 0 && screens[0][55][26] == 0xffff);
    renderer.show(0, empty); drain(renderer);
    cntrl::Pane editor;
    editor.lines[0].graphics.size = editor.lines[0].graphics.line_height = 16;
    editor.lines[1].graphics.size = 12; editor.lines[1].graphics.line_height = 16;
    editor.lines[1].wrap = true;
    std::strcpy(editor.lines[0].text, "Patch");
    std::memset(editor.lines[1].text, 'X', 63); editor.lines[1].text[63] = 0;
    renderer.show(0, editor); drain(renderer);
    for (unsigned y : {6u, 26u, 46u, 66u, 86u, 106u}) assert(screens[0][y][4] == 0);
    assert(screens[0][106][150] == 0xffff); // Final row ends before the right edge.
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

    cntrl::Pane back;
    back.graphics.background = 0;
    std::strcpy(back.lines[0].text, "Back");
    back.lines[0].graphics.background = 0;
    back.lines[0].graphics.foreground = 0xffff;
    back.lines[0].graphics.size = back.lines[0].graphics.line_height = 16;
    back.lines[0].x = 108; back.lines[0].y = 4;
    renderer.show(2, back); drain(renderer);
    unsigned label_pixels = 0;
    for (unsigned y = 0; y < 128; ++y) for (unsigned x = 0; x < 160; ++x) {
        if (screens[2][y][x]) {
            assert(x >= 108 && x < 156 && y >= 4 && y < 20);
            ++label_pixels;
        }
    }
    assert(label_pixels);
    renderer.show(2, empty); drain(renderer);

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
    assert(ShowGraphics::supports_font(5, 28)); // Fixed cable-test UI title.
    assert(!ShowGraphics::supports_font(5, 32));
    assert(!ShowGraphics::supports_font(8, 24)); // Shows use sparse SD assets.
    assert(!ShowGraphics::supports_font(10, 24));
    assert(ShowGraphics::supports_font(0, 13) && !ShowGraphics::supports_font(1, 15));

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
    assert(ring == display_pitch % display_ram_columns);
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
    // A fast send after browsing must animate even during a candidate repaint.
    cntrl::Pane browsing;
    browsing.graphics.background = 0xf800;
    renderer.show(2, browsing); renderer.draw();
    assert(!renderer.ready());
    unsigned before_scroll = scroll_updates;
    renderer.transition(false, 500);
    browsing.graphics.background = 0x001f;
    renderer.show(1, browsing);
    for (unsigned now = 5250; now < 6250; ++now)
        for (unsigned work = 0; work < 8; ++work) renderer.draw(now);
    assert(renderer.ready() && scroll_updates > before_scroll);
    blank(1, 0x001f); blank(2, 0xf800);
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
