#include <cntrl/ui/renderer.h>
#include <cntrl/ui/pages.h>
#include <cntrl/ui/page_host.h>
#include <cntrl/ui/application_hardware.h>
#include <cntrl/ui/pixel_throb.h>
#include <cassert>
#include <vector>
#include <string>
#include <algorithm>
#include "page_fixture.h"

struct Fonts final : cntrl::ui::Fonts {
    const uint8_t bits[2] = {0x80, 0x80};
    bool supports(unsigned, unsigned, uint32_t = 0) const override { return true; }
    cntrl::ui::Bounds bounds(unsigned, unsigned, uint32_t, const char *s) const override { return {0, 0, int(std::strlen(s) * 2), 2}; }
    bool glyph(unsigned, unsigned, uint32_t, const char *s, unsigned i, cntrl::ui::Glyph &g) const override {
        if (i >= std::strlen(s)) return false;
        g = {bits, 1, 2, int(i * 2), 0}; return true;
    }
};
struct Display : cntrl::ui::Display {
    std::vector<uint16_t> pixels = std::vector<uint16_t>(24 * 18, 0);
    bool due = false, pending = false, accept = true;
    const uint8_t *borrowed = nullptr;
    unsigned screen = 0, x = 0, y = 0, width = 0, height = 0, calls = 0;
    unsigned count() const override { return 1; }
    cntrl::ui::Viewport viewport(unsigned) const override { return {24, 18, 0, 0}; }
    void poll() override {}
    bool ready() const override { return !pending; }
    bool failed() override { return false; }
    bool priority_due() const override { return due; }
    bool submit(unsigned s, unsigned px, unsigned py, unsigned w, unsigned h, const uint8_t *p) override {
        if (!accept || pending) return false;
        screen = s; x = px; y = py; width = w; height = h; borrowed = p; pending = true; ++calls; return true;
    }
    void complete() {
        if (!pending) return;
        for (unsigned col = 0; col < width; ++col) for (unsigned row = 0; row < height; ++row) {
            const unsigned i = 2 * (col * height + row);
            pixels[(y + row) * 24 + x + col] = uint16_t(borrowed[i]) << 8 | borrowed[i + 1];
        }
        pending = false;
    }
};
struct RingDisplay final : Display {
    std::vector<uint16_t> ring = std::vector<uint16_t>(26 * 18, 0);
    unsigned offset = 0, reveals = 0;
    bool reject_once = false;
    unsigned scroll_columns(unsigned) const override { return 26; }
    unsigned scroll_quantum(unsigned) const override { return 2; }
    bool ring_column(unsigned s, unsigned col, const uint8_t *p, unsigned h) override {
        if (reject_once) { reject_once = false; return false; }
        return submit(s, col, 0, 1, h, p);
    }
    void scroll(unsigned, unsigned value) override { offset = value; ++reveals; refresh(); }
    void reset_scroll() override { offset = 0; }
    void refresh() {
        for (unsigned row = 0; row < 18; ++row) for (unsigned col = 0; col < 24; ++col)
            pixels[row * 24 + col] = ring[row * 26 + (offset + col) % 26];
    }
    void complete() {
        if (!pending) return;
        for (unsigned row = 0; row < height; ++row) ring[(y + row) * 26 + x] = uint16_t(borrowed[2 * row]) << 8 | borrowed[2 * row + 1];
        pending = false; refresh();
    }
};
static void ring_transition_preserves_hidden_columns_and_retries() {
    Fonts fonts; RingDisplay display;
    uint16_t work[24 * 18] = {}, cache[24 * 18] = {}, previous[24 * 18] = {};
    cntrl::ui::Renderer renderer(display, fonts, work, 24 * 18, cache, previous, 24 * 18);
    cntrl::Pane pane; pane.graphics.background = 0x1111; pane.graphics.pad = 0;
    renderer.show(0, pane);
    for (unsigned i = 0; i < 200; ++i) { renderer.draw(i * 100); display.complete(); }
    assert(renderer.ready()); renderer.transition(false, 10, 0, false, false, false);
    pane.graphics.background = 0x2222; renderer.show(0, pane);
    for (unsigned i = 0; i < 1000 && !display.reveals; ++i) {
        display.reject_once = i == 50;
        renderer.draw(i * 100); display.complete();
    }
    assert(display.reveals == 1);
    for (unsigned y = 0; y < 18; ++y) for (unsigned x = 0; x < 24; ++x)
        assert(display.pixels[y * 24 + x] == (x < 2 ? 0x2222 : 0x1111));
    for (unsigned i = 1000; i < 3000; ++i) { renderer.draw(i * 100); display.complete(); }
    assert(renderer.ready()); assert(display.reveals == 12);
    for (auto pixel : display.pixels) assert(pixel == 0x2222);
}
static void portable_font_bearings_and_character_advances() {
    std::vector<uint8_t> bytes(8 + 23 + 48 + 4);
    std::memcpy(bytes.data(), "CFN2", 4); cntrl::put32(bytes.data() + 4, 1);
    uint8_t *entry = bytes.data() + 8;
    entry[0] = 8; entry[1] = 2; entry[7] = 2; entry[11] = 2; entry[13] = 3; entry[14] = 2;
    cntrl::put32(entry + 15, 48); cntrl::put32(entry + 19, 4);
    uint8_t *data = entry + 23;
    for (unsigned i = 0; i < 2; ++i) {
        cntrl::put32(data + i * 16, 65 + i); data[i * 16 + 4] = 2; data[i * 16 + 6] = 2; data[i * 16 + 10] = 2;
        cntrl::put32(data + i * 16 + 12, i * 2);
        cntrl::put32(data + 32 + i * 8, 65 + i); data[32 + i * 8 + 4] = uint8_t(i); data[32 + i * 8 + 6] = 3;
        data[48 + i * 2] = 0x80; data[49 + i * 2] = 0xc0;
    }
    font_assets::Entry descriptor; std::memcpy(descriptor.bytes, entry, 23);
    assert(font_assets::valid_pixels(descriptor, data));
    cntrl::ui::EmbeddedFont embedded{16, bytes.data(), unsigned(bytes.size())};
    cntrl::ui::AssetFonts fonts(&embedded, 1);
    assert(fonts.supports(16, 8)); assert(!fonts.supports(16, 9));
    const auto bounds = fonts.bounds(16, 8, 0, "AB");
    assert(bounds.left == 0 && bounds.top == 0 && bounds.width == 6 && bounds.height == 2);
    cntrl::ui::Glyph glyph;
    assert(fonts.glyph(16, 8, 0, "AB", 1, glyph)); assert(glyph.x == 3 && glyph.y == 0 && glyph.width == 2);
    assert(!fonts.glyph(16, 8, 0, "AB", 2, glyph)); assert(cntrl::memory_used == 0);
    cntrl::MemoryBuffer staged; assert(staged.resize(52)); std::memcpy(staged.data, data, 52);
    assert(font_assets::cache.begin(1)); assert(font_assets::cache.add(11, descriptor, std::move(staged))); font_assets::cache.finish(true);
    assert(fonts.glyph(255, 8, 11, "AB", 0, glyph)); assert(glyph.y == -2);
    const auto asset_bounds = fonts.bounds(255, 8, 11, "AB");
    assert(asset_bounds.left == 0 && asset_bounds.top == -2 && asset_bounds.width == 6 && asset_bounds.height == 2);
    const auto *active = font_assets::cache.get(11, 8);
    const unsigned budget = cntrl::memory_budget, used = cntrl::memory_used;
    cntrl::memory_budget = used;
    assert(!font_assets::cache.begin(1));
    assert(font_assets::cache.get(11, 8) == active && cntrl::memory_used == used);
    cntrl::memory_budget = budget;
    assert(font_assets::cache.begin(1) && font_assets::cache.reuse(11, 8));
    font_assets::cache.finish(false);
    assert(font_assets::cache.get(11, 8) == active && cntrl::memory_used == used);
    assert(font_assets::cache.begin(33));
    for (unsigned i = 0; i < 33; ++i) {
        cntrl::MemoryBuffer pixels; assert(pixels.resize(52)); std::memcpy(pixels.data, data, 52);
        assert(font_assets::cache.add(100 + i, descriptor, std::move(pixels)));
    }
    assert(!font_assets::cache.fits()); font_assets::cache.finish(true);
    assert(font_assets::cache.get(132, 8) && !font_assets::cache.get(11, 8));
    assert(font_assets::cache.begin(0)); font_assets::cache.finish(true); assert(cntrl::memory_used == 0);
}
static void font_bounds_preserve_bearings_spaces_and_shaped_offsets() {
    std::vector<uint8_t> bytes(8 + 23 + 88 + 6);
    std::memcpy(bytes.data(), "CFN2", 4); cntrl::put32(bytes.data() + 4, 1);
    uint8_t *entry = bytes.data() + 8, *data = entry + 23;
    entry[0] = 8; entry[1] = 2; entry[7] = 3; entry[9] = 1; entry[11] = 3; entry[13] = 8; entry[14] = 4;
    cntrl::put32(entry + 15, 88); cntrl::put32(entry + 19, 6);
    for (unsigned i = 0; i < 3; ++i) {
        const unsigned code = i ? 64 + i : 32;
        cntrl::put32(data + i * 16, code);
        cntrl::put32(data + 48 + i * 8, code);
        data[48 + i * 8 + 4] = uint8_t(i);
        data[48 + i * 8 + 6] = uint8_t(i == 2 ? 5 : i + 2);
    }
    data[20] = data[22] = 2; data[24] = data[25] = 0xff; data[26] = 5;
    data[36] = 3; data[38] = 4; data[40] = 1; data[42] = 1; cntrl::put32(data + 44, 2);
    data[72] = 2; data[74] = 0xfe; data[75] = 0xff; data[76] = 9; data[78] = 1;
    data[80] = 0xc3; data[81] = 0xa9; data[82] = 2;
    data[84] = 0xfd; data[85] = 0xff; data[86] = 0xfe; data[87] = 0xff;
    font_assets::Entry descriptor; std::memcpy(descriptor.bytes, entry, 23);
    assert(font_assets::valid_pixels(descriptor, data));
    const cntrl::ui::EmbeddedFont embedded{16, bytes.data(), unsigned(bytes.size())};
    const cntrl::ui::AssetFonts fonts(&embedded, 1);
    const auto check = [&](const char *text, int left, int top, int width, int height) {
        const auto b = fonts.bounds(16, 8, 0, text);
        assert(b.left == left && b.top == top && b.width == width && b.height == height);
    };
    check("AB ", -1, -1, 11, 8);
    check(" BA", 0, -1, 10, 8);
    check("", 0, 0, 0, 0); check("   ", 0, 0, 6, 0);
    check("AB?", -1, -1, 0, 8);
    check("\xc3\xa9", -2, 1, 9, 4);
    std::string long_text;
    for (unsigned i = 0; i < 100; ++i) long_text += "AB";
    check(long_text.c_str(), -1, -1, 801, 8);
}
struct MixedDisplay final : cntrl::ui::Display {
    const cntrl::ui::Viewport views[5] = {{9, 13, 2, 0}, {17, 7, 0, 0}, {21, 11, 3, 0}, {7, 19, 0, 0}, {15, 5, 0, 0}};
    std::vector<uint16_t> pixels[5];
    MixedDisplay() { for (unsigned i = 0; i < 5; ++i) pixels[i].resize(views[i].width * views[i].height); }
    unsigned count() const override { return 5; }
    cntrl::ui::Viewport viewport(unsigned i) const override { return views[i]; }
    void poll() override {}
    bool ready() const override { return true; }
    bool failed() override { return false; }
    bool priority_due() const override { return false; }
    bool submit(unsigned screen, unsigned x, unsigned y, unsigned width, unsigned height, const uint8_t *data) override {
        assert(screen < 5 && x + width <= views[screen].width && y + height <= views[screen].height);
        for (unsigned col = 0; col < width; ++col) for (unsigned row = 0; row < height; ++row) {
            const unsigned i = 2 * (col * height + row);
            pixels[screen][(y + row) * views[screen].width + x + col] = uint16_t(data[i]) << 8 | data[i + 1];
        }
        return true;
    }
};
static void distinct_geometries_and_reserved_windows() {
    Fonts fonts; MixedDisplay display;
    uint16_t work[231] = {}, cache[675] = {}, previous[675] = {};
    cntrl::ui::Renderer renderer(display, fonts, work, 231, cache, previous, 675);
    for (unsigned i = 0; i < 5; ++i) { cntrl::Pane pane; pane.graphics.background = uint16_t(i + 1); renderer.show(i, pane); }
    for (unsigned i = 0; i < 1000; ++i) renderer.draw(i * 1000);
    assert(renderer.ready());
    for (unsigned screen = 0; screen < 5; ++screen) for (auto pixel : display.pixels[screen]) assert(pixel == screen + 1);
    renderer.transition(false, 10, -1, 2, true, false);
    for (unsigned i = 0; i < 3; ++i) { cntrl::Pane pane; pane.graphics.background = uint16_t(i + 11); renderer.show(i, pane); }
    for (unsigned i = 0; i < 3000; ++i) renderer.draw(i * 1000);
    assert(renderer.ready());
    for (unsigned screen = 0; screen < 5; ++screen) for (auto pixel : display.pixels[screen]) assert(pixel == screen + (screen < 3 ? 11 : 1));
    // A panoramic region transition in the middle cannot sample or move its neighbours.
    renderer.transition(false,10,1,0,false,true,2);
    for (unsigned screen = 1; screen < 3; ++screen) {
        cntrl::Pane pane; pane.graphics.background = uint16_t(screen + 21); renderer.show(screen,pane);
    }
    for (unsigned i = 0; i < 3000; ++i) {
        renderer.draw(i * 1000);
        for (unsigned screen : {0u,3u,4u}) for (auto pixel : display.pixels[screen]) assert(pixel == screen + (screen < 3 ? 11 : 1));
    }
    assert(renderer.ready());
    for (unsigned screen = 1; screen < 3; ++screen) for (auto pixel : display.pixels[screen]) assert(pixel == screen + 21);
    renderer.target_brightness(0,3);
    for (unsigned i = 0; i < 3000; ++i) renderer.draw(i * 1000);
    for (auto pixel : display.pixels[3]) assert(pixel == 0);
    for (auto pixel : display.pixels[0]) assert(pixel == 11);
    renderer.target_brightness(128,0);
    for (unsigned i = 0; i < 3000; ++i) renderer.draw(i * 1000);
    for (auto pixel : display.pixels[3]) assert(pixel == 4);
}
static void circles_clip_before_iterating_and_do_not_overflow() {
    Fonts fonts; uint16_t pixels[24 * 18] = {};
    cntrl::ui::Raster raster(fonts, pixels); cntrl::ScreenGraphics graphics; graphics.pad = 0; graphics.background = 0;
    raster.configure({24, 18, 0, 0}, graphics);
    raster.circle({0, 0, 65535, 0, 0xffff, 0});
    for (auto pixel : pixels) assert(pixel == 0xffff);
    raster.circle({65535, 65535, 65535, 65535, 0x1234, 0x1234});
    // This viewport lies entirely within the very large outer annulus.
    for (auto pixel : pixels) assert(pixel == 0x1234);
}
static void minimum_viewports_follow_declared_surfaces() {
    struct Geometry : cntrl::ui::Display {
        unsigned count() const override { return 3; }
        cntrl::ui::Viewport viewport(unsigned i) const override {
            constexpr cntrl::ui::Viewport views[] = {{320, 120}, {80, 240}, {160, 160}}; return views[i];
        }
        void poll() override {}
        bool ready() const override { return true; }
        bool failed() override { return false; }
        bool priority_due() const override { return false; }
        bool submit(unsigned, unsigned, unsigned, unsigned, unsigned, const uint8_t *) override { return true; }
    } display;
    assert(cntrl::ui::minimum_display_width(display) == 80);
    assert(cntrl::ui::minimum_display_height(display) == 120);
}
static void automatic_text_survives_background_and_stipple_passes() {
    for (bool bands : {false, true}) for (bool stippled : {false, true}) {
        Fonts fonts; Display display;
        uint16_t work[24 * 18] = {}, cache[24 * 18] = {}, previous[24 * 18] = {};
        cntrl::ui::Renderer renderer(display, fonts, work, 24 * 18, cache, previous, 24 * 18);
        cntrl::Pane pane; pane.graphics.background = 0x1234; pane.graphics.pad = 0;
        pane.three_bands = bands; pane.heading_band = true;
        auto &line = pane.lines[0]; line.graphics = pane.graphics;
        line.graphics.background = 0; line.graphics.foreground = 0xffff;
        line.graphics.line_height = 2; line.graphics.top = 0;
        line.stippled = stippled; std::strcpy(line.text, "AB");
        renderer.show(0, pane);
        for (unsigned i = 0; i < 200; ++i) { renderer.draw(i * 1000); display.complete(); }
        assert(renderer.ready());
        assert(std::count(display.pixels.begin(), display.pixels.end(), 0xffff) == (stippled ? 2 : 4));
    }
}
static void title_text_is_drawn_over_its_indicator() {
    Fonts fonts; Display display;
    uint16_t work[24 * 18] = {}, cache[24 * 18] = {}, previous[24 * 18] = {};
    cntrl::ui::Renderer renderer(display, fonts, work, 24 * 18, cache, previous, 24 * 18);
    cntrl::Pane pane; pane.graphics.pad = 0; pane.three_bands = pane.heading_band = true;
    auto &line = pane.lines[0]; line.graphics = pane.graphics;
    line.graphics.background = 0; line.graphics.foreground = 0xffff;
    line.graphics.line_height = 2; line.graphics.top = 0;
    std::strcpy(line.text, "AB"); pane.circles[0] = {5, 3, 6, 0, 0x07e0, 0};
    renderer.show(0, pane);
    for (unsigned i = 0; i < 200; ++i) { renderer.draw(i * 1000); display.complete(); }
    assert(renderer.ready());
    assert(display.pixels[2 * 24 + 4] == 0xffff && display.pixels[2 * 24 + 6] == 0xffff);
    assert(display.pixels[2 * 24 + 5] == 0x07e0);
}
void messages_restore_the_latest_page_after_replacement_and_expiry() {
    Fonts fonts; Display display;
    uint16_t work[24 * 18] = {}, cache[24 * 18] = {}, previous[24 * 18] = {};
    cntrl::ui::Renderer renderer(display, fonts, work, 24 * 18, cache, previous, 24 * 18);
    cntrl::Pane pane; pane.graphics.pad = 0; pane.graphics.background = 0x1234;
    renderer.show(0, pane);
    cntrl::TextMessages messages;
    assert(messages.publish(cntrl::TextMessages::Kind::cue, "Go", 2, 0));
    renderer.message(messages, 0);
    for (unsigned i = 0; i < 500; ++i) { renderer.draw(); display.complete(); }
    assert(display.pixels[8 * 24 + 8] != 0x1234);
    pane.graphics.background = 0x5678; renderer.show(0, pane);
    assert(messages.publish(cntrl::TextMessages::Kind::warning, "Wait", 4, 4999));
    renderer.message(messages, 5000);
    for (unsigned i = 0; i < 500; ++i) { renderer.draw(); display.complete(); }
    assert(display.pixels[8 * 24 + 8] != 0x5678);
    renderer.message(messages, 9999);
    for (unsigned i = 0; i < 500; ++i) { renderer.draw(); display.complete(); }
    assert(std::all_of(display.pixels.begin(), display.pixels.end(), [](uint16_t p) { return p == 0x5678; }));
}
static void expression_reuses_its_initial_layout_and_redraws_only_changed_content() {
    struct MeasuredFonts : cntrl::ui::Fonts {
        mutable unsigned measurements = 0;
        bool supports(unsigned, unsigned size, uint32_t = 0) const override { ++measurements; return size >= 8 && size <= 24; }
        cntrl::ui::Bounds bounds(unsigned, unsigned size, uint32_t, const char *text) const override {
            ++measurements; return {0, -int(size), int(std::strlen(text) * size / 2), int(size)};
        }
        bool glyph(unsigned, unsigned, uint32_t, const char *, unsigned, cntrl::ui::Glyph &) const override { return false; }
    } fonts;
    cntrl::ui::Pages pages(fonts, {200, 128, 0, 0});
    constexpr cntrl::ui::ControlBinding bindings[] = {{0,"A"},{1,"B"},{2,"C"}};
    pages.controls({0,1,2,0,1,2,2,0,2,bindings,3});
    const cntrl::midi::OutputConnection output{"jack","A longer port label",0,cntrl::midi::OutputTransport::physical,true,true,3};
    pages.outputs({&output,1});
    cntrl::PageSettings settings;
    settings.words[cntrl::PageSettings::padding] = 2;
    settings.words[cntrl::PageSettings::title_height] = 12;
    settings.words[cntrl::PageSettings::status_font] = 16;
    settings.words[cntrl::PageSettings::title_font] = 16;
    pages.configure(settings);
    auto layout = page_fixture(1,{unsigned(cntrl::ui::Pages::Page::expression)},{{0}});
    pages.layout(cntrl::PageLayout{layout.data()});
    const unsigned prepared = fonts.measurements; assert(prepared);
    pages.expression_status("Jack 1  Unassigned","50%  Calibrated","C: Calibrate");
    assert(pages.due(0));
    const auto first = pages.view(0,0,{});
    assert(std::strcmp(first.lines[0].text,"Expression") == 0);
    assert(std::strcmp(first.lines[4].text,"A: Jack   B: Cancel") == 0);
    assert(!pages.expression_status_due(79) && pages.expression_status_due(80));
    assert(!pages.expression_status_due(81));
    assert(!pages.due(80) && !pages.due(10000));
    pages.expression_status("Jack 1  Unassigned","50%  Calibrated","C: Calibrate");
    assert(!pages.due(10000));
    pages.expression_status("A longer port label  CC 127 Ch 16","Raw 1023  Uncalibrated","Reconnect: heel");
    assert(pages.due(10000));
    const auto changed = pages.view(10000,10000000,{});
    assert(std::strcmp(changed.lines[2].text,"Raw 1023  Uncalibrated") == 0);
    for (unsigned i = 0; i < 5; ++i) {
        assert(first.lines[i].x == changed.lines[i].x && first.lines[i].y == changed.lines[i].y);
        assert(first.lines[i].graphics.font == changed.lines[i].graphics.font);
        assert(first.lines[i].graphics.size == changed.lines[i].graphics.size);
    }
    assert(first.lines[1].graphics.size == first.lines[2].graphics.size && first.lines[2].graphics.size == first.lines[3].graphics.size);
    assert(!pages.due(20000));
    pages.select(0); assert(pages.due(20000)); // Re-entering exposes the page again.
    pages.view(20000,20000000,{});
    assert(fonts.measurements == prepared); // No font fitting on visits or changed-value redraws.
}

int main() {
    expression_reuses_its_initial_layout_and_redraws_only_changed_content();
    messages_restore_the_latest_page_after_replacement_and_expiry();
    cntrl::memory_budget = 192 * 1024;
    automatic_text_survives_background_and_stipple_passes();
    title_text_is_drawn_over_its_indicator();
    minimum_viewports_follow_declared_surfaces();
    circles_clip_before_iterating_and_do_not_overflow();
    distinct_geometries_and_reserved_windows();
    portable_font_bearings_and_character_advances();
    font_bounds_preserve_bearings_spaces_and_shaped_offsets();
    ring_transition_preserves_hidden_columns_and_retries();
    Fonts fonts; Display display;
    uint16_t work[24 * 18] = {}, cache[24 * 18] = {}, previous[24 * 18] = {};
    cntrl::ui::Renderer renderer(display, fonts, work, 24 * 18, cache, previous, 24 * 18);
    cntrl::Pane pane; pane.graphics.background = 0x1234; pane.graphics.pad = 0;
    pane.lines[0].graphics = pane.graphics; pane.lines[0].graphics.foreground = 0xffff; pane.lines[0].y = 2; pane.lines[0].graphics.top = 3; pane.lines[0].x = 3;
    std::strcpy(pane.lines[0].text, "AB"); renderer.show(0, pane);
    for (unsigned i = 0; i < 200; ++i) { renderer.draw(i * 1000); display.complete(); }
    assert(renderer.ready()); assert(display.pixels[5 * 24 + 3] == 0xffff); assert(display.pixels.back() == 0x1234);
    const unsigned calls = display.calls;
    renderer.show(0, pane);
    for (unsigned i = 0; i < 200; ++i) { renderer.draw(i * 1000); display.complete(); }
    assert(display.calls == calls);
    pane.graphics.background = pane.lines[0].graphics.background = 0xabcd; renderer.show(0, pane); display.due = true;
    for (unsigned i = 0; i < 100; ++i) renderer.draw();
    assert(display.calls == calls); display.due = false;
    // A transfer borrows the renderer buffer until explicit completion.
    for (unsigned i = 0; i < 100 && !display.pending; ++i) renderer.draw();
    assert(display.pending); std::vector<uint8_t> before(display.borrowed, display.borrowed + display.height * display.width * 2);
    for (unsigned i = 0; i < 100; ++i) renderer.draw();
    assert(std::equal(before.begin(), before.end(), display.borrowed)); display.complete();
    for (unsigned i = 0; i < 200; ++i) { renderer.draw(i * 1000); display.complete(); }
    assert(renderer.ready());
    renderer.transition(false, 10, 0, false, true, false);
    pane.graphics.background = pane.lines[0].graphics.background = 0x4567; renderer.show(0, pane);
    for (unsigned i = 0; i < 2000; ++i) { renderer.draw(i * 1000); display.complete(); }
    assert(renderer.ready()); assert(display.pixels.back() == 0x4567);
    cntrl::ui::Pages pages(fonts, display.viewport(0));
    cntrl::PageSettings settings; settings.words[cntrl::PageSettings::title_height] = 20; pages.configure(settings);
    cntrl::ui::PerformanceStatus status; status.starts_status = cntrl::ui::StartsStatus::ready; status.starts = 42;
    constexpr cntrl::ui::ControlBinding controls[] = {{0, "Less"}, {1, "More"}, {2, "Yes"}, {3, "Back"}};
    pages.controls({0, 1, 2, 0, 3, 3, 3, 0, 2, controls, 4});
    const auto view = pages.view(0, 0, status);
    assert(std::strcmp(view.lines[0].text, "Status") == 0);
    assert(view.lines[0].graphics.background == 0 && view.lines[0].graphics.foreground == 0xffff);
    assert(view.lines[0].y == 0 && view.lines[0].graphics.top > 0);
    renderer.show(0, view);
    for (unsigned i = 0; i < 300; ++i) { renderer.draw(i * 1000); display.complete(); }
    const auto title_x = (24 - std::strlen(view.lines[0].text) * 2) / 2;
    assert(display.pixels[view.lines[0].graphics.top * 24 + title_x] == 0xffff);
    pages.viewport({200, 128, 0, 0});
    uint8_t layout[12 + 3 * 32 + 37] = {'C', 'P', 'A', 'G', 1, 3, 2, 0};
    layout[12 + 3 * 32 + 32] = unsigned(cntrl::ui::Pages::Page::clock_tempo);
    layout[12 + 3 * 32 + 33] = 1;
    pages.layout(cntrl::PageLayout{layout});
    const auto clock_page = pages.view(0, 0, status);
    assert(std::strcmp(clock_page.lines[3].text, "Less -1   More +1   Yes Slot") == 0);
    layout[12 + 3 * 32 + 32] = unsigned(cntrl::ui::Pages::Page::firmware); pages.layout(cntrl::PageLayout{layout}); pages.tap(0);
    const auto confirmation = pages.view(0, 0, status);
    assert(std::strcmp(confirmation.lines[0].text, "Back for stock firmware.") == 0);
    assert(std::strcmp(confirmation.lines[1].text, "Yes to cancel") == 0);

    settings.words[cntrl::PageSettings::diameter] = 18;
    settings.words[cntrl::PageSettings::annulus] = 5;
    settings.words[cntrl::PageSettings::tempo_height] = 40;
    pages.configure(settings); pages.viewport({320, 256, 0, 0});
    layout[12 + 3 * 32 + 32] = unsigned(cntrl::ui::Pages::Page::tap); pages.layout(cntrl::PageLayout{layout});
    const auto large_tap = pages.view(0, 0, status);
    assert(large_tap.circles[0].x == 160 && large_tap.circles[0].diameter == 18 && large_tap.circles[0].annulus == 5);
    assert(large_tap.lines[1].graphics.line_height == 40 && large_tap.lines[0].graphics.line_height == 34);

}
