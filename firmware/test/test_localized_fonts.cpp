#include <cntrl/ui/fonts.h>
#include <cntrl/ui/raster.h>
#include <cntrl/ui/renderer.h>
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

static std::vector<uint8_t> read(const char *path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

struct Display : cntrl::ui::Display {
    std::vector<uint16_t> pixels = std::vector<uint16_t>(96 * 64, 0);
    unsigned count() const override { return 1; }
    cntrl::ui::Viewport viewport(unsigned) const override { return {96, 64, 0, 0}; }
    void poll() override {}
    bool ready() const override { return true; }
    bool failed() override { return false; }
    bool priority_due() const override { return false; }
    bool submit(unsigned, unsigned x, unsigned y, unsigned width, unsigned height, const uint8_t *bytes) override {
        for (unsigned column = 0; column < width; ++column) for (unsigned row = 0; row < height; ++row) {
            const auto *p = bytes + 2 * (column * height + row);
            pixels[(y + row) * 96 + x + column] = uint16_t(p[0]) << 8 | p[1];
        }
        return true;
    }
};

static void render(const cntrl::ui::Fonts &fonts, const char *path) {
    std::vector<uint16_t> expected;
    for (unsigned frames : {0u, 1u, 2u}) {
        Display display; uint16_t work[96 * 64];
        cntrl::ui::Renderer renderer(display, fonts, work, 96 * 64);
        assert(renderer.prepare(frames, false)); renderer.finish(true);
        cntrl::Pane pane;
        const char *captions[] = {"Éléments enregistrés", "Paramètres de la pédale"};
        for (unsigned i = 0; i < 2; ++i) {
            auto &line = pane.lines[i]; line.graphics = pane.graphics;
            line.graphics.font = cntrl::ui::localized_face; line.graphics.size = 16;
            line.graphics.line_height = 16; line.wrap = true;
            cntrl::ui::utf8::copy(line.text, sizeof(line.text), captions[i]);
        }
        renderer.show(0, pane);
        for (unsigned i = 0; i < 10000 && !renderer.ready(); ++i) renderer.draw(i);
        assert(renderer.ready());
        if (expected.empty()) expected = display.pixels;
        else assert(expected == display.pixels);
    }
    std::ofstream image(path, std::ios::binary); image << "P6\n96 64\n255\n";
    for (auto pixel : expected) {
        const uint8_t rgb[] = {uint8_t((pixel >> 11) * 255 / 31), uint8_t(((pixel >> 5) & 63) * 255 / 63), uint8_t((pixel & 31) * 255 / 31)};
        image.write(reinterpret_cast<const char *>(rgb), 3);
    }
}

int main(int argc, char **argv) {
    assert(argc == 4);
    cntrl::memory_budget = 1024 * 1024;
    auto english = read(argv[1]), french = read(argv[2]);
    assert(cntrl::ui::AssetFonts::valid_localized(french.data(), unsigned(french.size())));
    assert(!cntrl::ui::AssetFonts::valid_localized(french.data(), unsigned(french.size() - 1)));
    const cntrl::ui::EmbeddedFont embedded[] = {{0, english.data(), unsigned(english.size())}};
    cntrl::ui::AssetFonts fonts(embedded, 1);
    const auto english_bounds = fonts.bounds(0, 12, 0, "Midi");
    assert(fonts.bounds(cntrl::ui::localized_face, 12, 0, "Midi").width == english_bounds.width);
    cntrl::ui::AssetFonts::localized(french.data(), unsigned(french.size()));
    assert(fonts.supports(cntrl::ui::localized_face, 12));
    assert(!fonts.supports(cntrl::ui::localized_face, 13));
    cntrl::ui::Glyph actual, accent;
    assert(fonts.glyph(cntrl::ui::localized_face, 12, 0, "Café", 3, actual));
    assert(fonts.glyph(cntrl::ui::localized_face, 12, 0, "é", 0, accent));
    assert(actual.pixels == accent.pixels && actual.width == accent.width && actual.height == accent.height);
    assert(!fonts.glyph(cntrl::ui::localized_face, 12, 0, "Café", 4, actual));
    assert(!fonts.glyph(0, 12, 0, "é", 0, actual)); // Authored/embedded faces are not replaced.
    const auto bounds = fonts.bounds(cntrl::ui::localized_face, 12, 0, "Éléments");
    assert(bounds.width > 20 && bounds.height > 0);
    assert(fonts.bounds(0, 12, 0, "Midi").width == english_bounds.width);
    uint16_t pixels[96 * 32] = {};
    cntrl::ui::Raster raster(fonts, pixels);
    cntrl::ScreenGraphics settings; settings.background = 0; settings.foreground = 0xffff;
    raster.configure({96, 32, 0, 0}, settings);
    cntrl::TextLine line; cntrl::ui::utf8::copy(line.text, sizeof(line.text), "Café");
    line.graphics = settings; line.graphics.font = cntrl::ui::localized_face; line.graphics.size = 12;
    unsigned painted = 0;
    while (raster.letter(line, painted, 8)) ++painted;
    assert(painted == 4);
    unsigned lit = 0; for (auto pixel : pixels) lit += pixel != 0;
    assert(lit > 20);
    char text[16];
    assert(cntrl::ui::utf8::copy(text, sizeof(text), "Café"));
    assert(cntrl::ui::utf8::erase_last(text) && !std::strcmp(text, "Caf"));
    assert(cntrl::ui::utf8::copy(text, sizeof(text), "Cafe\xcc\x81"));
    assert(cntrl::ui::utf8::erase_last(text) && !std::strcmp(text, "Caf"));
    assert(!cntrl::ui::utf8::copy(text, 4, "éé") && !std::strcmp(text, "é"));
    assert(!cntrl::ui::utf8::copy(text, 3, "e\xcc\x81") && !text[0]);
    std::memcpy(text, "é\xc3", 4);
    assert(!cntrl::ui::utf8::copy(text, sizeof(text), text) && !std::strcmp(text, "é"));
    const char *invalid = "\xc0\xaf"; uint32_t code;
    assert(!cntrl::ui::utf8::next(invalid, code));
    render(fonts, argv[3]);
    cntrl::ui::AssetFonts::localized(nullptr, 0);
    assert(fonts.bounds(cntrl::ui::localized_face, 12, 0, "Midi").width == english_bounds.width);
}
