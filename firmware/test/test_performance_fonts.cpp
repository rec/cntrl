#include <cntrl/ui/renderer.h>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

struct Display : cntrl::ui::Display {
    std::vector<uint16_t> pixels = std::vector<uint16_t>(160 * 128, 0);
    unsigned count() const override { return 1; }
    cntrl::ui::Viewport viewport(unsigned) const override { return {160, 128, 0, 0}; }
    void poll() override {}
    bool ready() const override { return true; }
    bool failed() override { return false; }
    bool priority_due() const override { return false; }
    bool submit(unsigned, unsigned x, unsigned y, unsigned width, unsigned height, const uint8_t *bytes) override {
        for (unsigned column = 0; column < width; ++column) for (unsigned row = 0; row < height; ++row) {
            const auto *p = bytes + 2 * (column * height + row);
            pixels[(y + row) * 160 + x + column] = uint16_t(p[0]) << 8 | p[1];
        }
        return true;
    }
};

int main(int argc, char **argv) {
    assert(argc == 8);
    std::ifstream input(argv[1], std::ios::binary);
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
    const unsigned face = unsigned(std::atoi(argv[2])), size = unsigned(std::atoi(argv[3]));
    const int height = std::atoi(argv[4]), top = std::atoi(argv[5]);
    const cntrl::ui::EmbeddedFont embedded[] = {{face, bytes.data(), unsigned(bytes.size())}};
    cntrl::ui::AssetFonts fonts(embedded, 1);
    std::ifstream characters(argv[6], std::ios::binary);
    const std::string repertoire{std::istreambuf_iterator<char>(characters), {}};
    cntrl::ui::Glyph glyph;
    // The compiler's layout offset must contain every supported character,
    // including accents and descenders, using the actual firmware provider.
    unsigned index = 0; uint32_t code;
    const char *cursor = repertoire.c_str();
    while (cntrl::ui::utf8::next(cursor, code)) {
        assert(fonts.glyph(face, size, 0, repertoire.c_str(), index++, glyph));
        assert(top + glyph.y >= 0);
        assert(top + glyph.y + int(glyph.height) <= height);
    }
    assert(index >= 95);
    cntrl::memory_budget = 1024 * 1024;
    Display display;
    uint16_t work[160 * 128];
    cntrl::ui::Renderer renderer(display, fonts, work, 160 * 128);
    assert(renderer.prepare(0, false)); renderer.finish(true);
    cntrl::Pane pane; pane.three_bands = true;
    pane.graphics.background = 0; pane.graphics.foreground = 0xffff;
    const char *captions[] = {"synth", "Igneous", "TalkBox!"};
    for (unsigned i = 0; i < 3; ++i) {
        auto &line = pane.lines[i];
        line.graphics = pane.graphics;
        line.graphics.font = face; line.graphics.size = size;
        line.graphics.line_height = unsigned(height); line.graphics.top = top;
        line.graphics.foreground = 0xffff; line.graphics.background = 0;
        line.centered = true;
        cntrl::ui::utf8::copy(line.text, sizeof(line.text), captions[i]);
    }
    renderer.show(0, pane);
    for (unsigned i = 0; i < 10000 && !renderer.ready(); ++i) renderer.draw(i);
    assert(renderer.ready());
    for (unsigned i = 0; i < 3; ++i) {
        const unsigned start = i * 128 / 3, end = (i + 1) * 128 / 3;
        unsigned ink = 0;
        for (unsigned y = start; y < end; ++y) for (unsigned x = 0; x < 160; ++x) {
            if (!display.pixels[y * 160 + x]) continue;
            assert(y >= start + 7 && y < end - 7);
            ++ink;
        }
        assert(ink > 10);
    }
    std::ofstream image(argv[7], std::ios::binary); image << "P6\n160 128\n255\n";
    for (auto pixel : display.pixels) {
        const uint8_t value = pixel ? 255 : 0;
        const uint8_t rgb[] = {value, value, value};
        image.write(reinterpret_cast<const char *>(rgb), 3);
    }
}
