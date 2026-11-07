#include <cntrl/ui/renderer.h>
#include <cntrl/ui/pages.h>
#include <cntrl/ui/pixel_throb.h>
#include <cassert>
#include <vector>
#include <algorithm>

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
        cntrl::put32(data + 32 + i * 8, 65 + i); data[32 + i * 8 + 4] = i; data[32 + i * 8 + 6] = 3;
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
    font_assets::cache.begin(); assert(font_assets::cache.add(11, descriptor, std::move(staged))); font_assets::cache.finish(true);
    assert(fonts.glyph(255, 8, 11, "AB", 0, glyph)); assert(glyph.y == -2);
    font_assets::cache.begin(); font_assets::cache.finish(true); assert(cntrl::memory_used == 0);
}
int main() {
    cntrl::memory_budget = 192 * 1024;
    portable_font_bearings_and_character_advances();
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
    const auto view = pages.view(0, 0, status);
    assert(std::strcmp(view.lines[0].text, "Status") == 0);
    assert(view.lines[0].graphics.background == 0 && view.lines[0].graphics.foreground == 0xffff);
    assert(view.lines[0].y == 0 && view.lines[0].graphics.top > 0);
    renderer.show(0, view);
    for (unsigned i = 0; i < 300; ++i) { renderer.draw(i * 1000); display.complete(); }
    const unsigned title_x = (24 - std::strlen(view.lines[0].text) * 2) / 2;
    assert(display.pixels[view.lines[0].graphics.top * 24 + title_x] == 0xffff);
}
