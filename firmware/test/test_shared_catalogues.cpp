#include "patch_hardware.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

struct SharedBoard : TestPatchHardware {
    std::vector<std::vector<uint8_t>> messages;
    bool send(const char *output, const uint8_t *bytes, unsigned length) override {
        assert(!std::strcmp(output, "din"));
        messages.emplace_back(bytes, bytes + length);
        return true;
    }
    bool supports(const char *output) const override { return !std::strcmp(output, "din"); }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override { assert(false); }
};

int main(int argc, char **argv) {
    assert(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    assert(file);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    SharedBoard board;
    cntrl::Document document;
    assert(document.load(bytes.data(), unsigned(bytes.size()), board));
    assert(document.instruments() == 1 && document.patches() == 64);
    cntrl::Pane view;
    document.page(view, 1, 2, true);
    assert(!std::strcmp(view.lines[0].text, "Internal"));
    assert(!std::strcmp(view.lines[1].text, "Igneous"));
    assert(!std::strcmp(view.lines[2].text, "!XTREME!"));
    assert(!view.lines[0].stippled && view.lines[1].stippled && view.lines[2].stippled);
    document.page(view, 1, 2, false);
    assert(!view.lines[1].stippled && !view.lines[2].stippled);
    cntrl::PatchExecutor executor(board, board, board, 5);
    for (unsigned i = 0; i < 64; ++i) {
        assert(executor.start(document, document.browser_root(i)));
        for (unsigned tick = 0; tick < 256 && executor.busy(); ++tick) executor.tick();
        assert(executor.status() == cntrl::PatchExecutor::Status::completed);
        assert(board.messages.size() == i + 1);
        assert((board.messages.back() == std::vector<uint8_t>{0xc0, uint8_t(i)}));
    }
}
