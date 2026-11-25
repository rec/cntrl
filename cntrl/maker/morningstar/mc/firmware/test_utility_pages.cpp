#include <cntrl/ui/pages.h>
#include <cntrl/ui/renderer.h>
#include "patch_chooser/embedded_fonts.h"
#include "patch_chooser/target_capabilities.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>

struct Display final : cntrl::ui::Display {
    std::vector<uint16_t> pixels = std::vector<uint16_t>(160 * 128);
    unsigned count() const override { return 1; }
    cntrl::ui::Viewport viewport(unsigned) const override { return {160, 128, 0, 0}; }
    void poll() override {}
    bool ready() const override { return true; }
    bool failed() override { return false; }
    bool priority_due() const override { return false; }
    bool submit(unsigned, unsigned x, unsigned y, unsigned width, unsigned height, const uint8_t *data) override {
        assert(x + width <= 160 && y + height <= 128);
        for (unsigned column = 0; column < width; ++column) for (unsigned row = 0; row < height; ++row) {
            const unsigned index = 2 * (column * height + row);
            pixels[(y + row) * 160 + x + column] = uint16_t(data[index]) << 8 | data[index + 1];
        }
        return true;
    }
};

int main(int argc, char **argv) {
    cntrl::memory_budget = 2 * 1024 * 1024;
    cntrl::ui::AssetFonts fonts(embedded_fonts, sizeof(embedded_fonts) / sizeof(*embedded_fonts));
    cntrl::ui::Pages pages(fonts, {160, 128, 0, 0});
    pages.controls(cntrl::target::controls); pages.outputs(cntrl::target::midi_connections);
    cntrl::PageSettings settings;
    using R = cntrl::PageSettings;
    settings.words[R::padding] = 2; settings.words[R::title_height] = 12;
    settings.words[R::tempo_height] = 40; settings.words[R::diameter] = 40;
    settings.words[R::tempo_font] = 8; settings.words[R::status_font] = 16; settings.words[R::title_font] = 16;
    settings.words[R::background] = 0xff83; settings.words[R::on_color] = 0xf800; settings.words[R::off_color] = 0x6000;
    settings.words[R::clocks_per_quarter] = 24; settings.words[R::clock_beats] = 1;
    settings.words[R::slot1] = 120; settings.words[R::digits] = 3; settings.words[R::decimals] = 1;
    pages.configure(settings);
    cntrl::ui::PerformanceStatus status;
    status.cycles = 10; status.patches = 59; status.starts = 111; status.starts_status = cntrl::ui::StartsStatus::ready;
    status.ram_used[0] = 310240; status.ram_free[0] = 17440; status.ram_used[1] = 91008; status.ram_free[1] = 433280;
    using Page = cntrl::ui::Pages::Page;
    const Page types[] = {Page::status, Page::cable, Page::panic, Page::pong, Page::time, Page::setups, Page::songs, Page::switches};
    std::vector<uint16_t> sheet(5 * 160 * 2 * 128);
    bool valid = true;
    for (unsigned page = 0; page < sizeof(types) / sizeof(types[0]); ++page) {
        auto view = pages.save_view(); view.page = types[page]; pages.restore_view(view);
        const auto pane = pages.view(0, 0, status, {}, types[page] == Page::panic ? cntrl::Panic::Status::all_off : cntrl::Panic::Status::ready);
        unsigned lines = 0;
        for (const auto &line : pane.lines) if (line.text[0]) {
            ++lines;
            if (!fonts.supports(line.graphics.font, line.graphics.size)) {
                std::cerr << "page " << page << ": unavailable font " << unsigned(line.graphics.font) << "/" << unsigned(line.graphics.size) << ": " << line.text << '\n'; valid = false;
            }
        }
        if (types[page] == Page::status) for (unsigned i = 1; i < cntrl::max_lines; ++i) {
            const auto &line = pane.lines[i];
            if (!line.text[0]) continue;
            if (i == 2) assert(line.graphics.font == cntrl::ui::localized_face && line.graphics.size <= cntrl::ui::status_size);
            else assert(line.graphics.font == cntrl::ui::status_face && line.graphics.size == cntrl::ui::status_size);
        }
        if (types[page] == Page::pong) {
            for (const auto &line : pane.lines) if (line.text[0])
                assert(line.graphics.font == settings.words[R::status_font]);
            const auto middle = pages.view(0, 0, status, {}, cntrl::Panic::Status::ready, 1, 3);
            const auto &legend = middle.lines[1];
            assert(std::strstr(legend.text, "Exit"));
            const auto bounds = fonts.bounds(legend.graphics.font, legend.graphics.size, legend.graphics.asset, legend.text);
            assert(legend.x + bounds.left >= 0 && legend.x + bounds.left + bounds.width <= 160);
            assert(legend.y + bounds.top >= 115 && legend.y + bounds.top + bounds.height <= 128);
        }
        if (types[page] == Page::time) {
            const auto &left = pane.lines[1], &right = pane.lines[5];
            assert(!std::strcmp(left.text, "B toggles") && !std::strcmp(right.text, "E cycles"));
            const auto left_bounds = fonts.bounds(left.graphics.font, left.graphics.size, 0, left.text);
            const auto right_bounds = fonts.bounds(right.graphics.font, right.graphics.size, 0, right.text);
            assert(left.y == right.y && left.x + left_bounds.left + left_bounds.width <= 80);
            assert(right.x + right_bounds.left >= 80 && right.x + right_bounds.left + right_bounds.width == 158);
            assert(std::max(left.y + left_bounds.top + left_bounds.height, right.y + right_bounds.top + right_bounds.height) < pane.toggle.y);
        }
        if (types[page] == Page::panic)
            for (unsigned i = 1; i < cntrl::max_lines; ++i) if (pane.lines[i].text[0])
                assert(pane.lines[i].graphics.font == 17 && pane.lines[i].graphics.size <= 24);
        if (!lines) { std::cerr << "page " << page << ": no text\n"; valid = false; }
        Display display; uint16_t work[160 * 128];
        cntrl::ui::Renderer renderer(display, fonts, work, 160 * 128);
        assert(renderer.prepare(1, false)); renderer.finish(true); renderer.show(0, pane);
        for (unsigned i = 0; i < 10000 && !renderer.ready(); ++i) renderer.draw();
        assert(renderer.ready());
        for (unsigned y = 0; y < 128; ++y) for (unsigned x = 0; x < 160; ++x)
            sheet[((page / 5) * 128 + y) * 800 + (page % 5) * 160 + x] = display.pixels[y * 160 + x];
    }
    if (argc == 2) {
        std::ofstream output(argv[1], std::ios::binary); output << "P6\n800 256\n255\n";
        for (auto pixel : sheet) {
            const char bytes[] = {char((pixel >> 11) * 255 / 31), char(((pixel >> 5) & 63) * 255 / 63), char((pixel & 31) * 255 / 31)};
            output.write(bytes, 3);
        }
    }
    return valid ? 0 : 1;
}
