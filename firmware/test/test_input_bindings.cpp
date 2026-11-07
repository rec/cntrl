#include <cntrl/switches.h>
#include "patch_hardware.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

struct Board : TestPatchHardware {
    std::vector<unsigned> sent;
    bool mismatch = false;
    unsigned input_count() const override { return 8; }
    const char *input_label(unsigned index) const override {
        static constexpr const char *labels[] = {"Less", "More", "Yes", "Back", "Top", "Side", "Aux", "Spare"};
        return mismatch ? "Wrong" : index < 8 ? labels[index] : "";
    }
    unsigned control_input(cntrl::ControlRole role) const override { return role == cntrl::ControlRole::back ? 3 : unsigned(role); }
    unsigned switch_capacity() const override { return 5; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    bool supports_function(unsigned code) const override { return code == 1; }
    unsigned display_count() const override { return 3; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    bool send(const char *, const uint8_t *data, unsigned length) override {
        assert(length == 3); sent.push_back(data[2]); return true;
    }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override { assert(false); }
};
int main(int argc, char **argv) {
    assert(argc == 2); std::ifstream input(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(input), {});
    Board board; cntrl::Document document; assert(document.load(bytes.data(), unsigned(bytes.size()), board));
    Board wrong; wrong.mismatch = true; cntrl::Document rejected;
    assert(!rejected.load(bytes.data(), unsigned(bytes.size()), wrong));
    assert(document.switch_assignment(0)[0] == 7 && document.switch_assignment(1)[0] == 5);
    assert(document.switch_hooks(7) && document.switch_hooks(5) && document.switch_shifted(7));
    assert(document.switch_gestures(7) && !document.switch_gestures(5));
    const auto *stock = document.node(unsigned(document.named_patch("stock")));
    assert(stock[0] == 4 && stock[4] == 129);
    cntrl::PatchExecutor executor(board, board, board, 0);
    assert(executor.invoke(cntrl::StockFirmwarePatch{129})); executor.cancel();
    cntrl::Switches switches(board, board, board); switches.enter(document, 0);
    auto tick = [&](unsigned start) { for (unsigned i = 0; i < 100; ++i) switches.tick(start + i); };
    tick(0); assert(board.sent.empty());
    switches.button(7, true, 200); tick(200); assert((board.sent == std::vector<unsigned>{13, 11}));
    switches.button(7, false, 400); tick(400); assert(board.sent.back() == 12);
    switches.button(5, true, 600); tick(600); assert(board.sent.back() == 21 && board.sent[board.sent.size() - 2] == 23);
    switches.button(5, false, 800); tick(800);
    assert(switches.shift(true));
    switches.button(7, true, 1000); tick(1000); assert(board.sent.back() == 15);
    switches.button(7, false, 1200); tick(1200);
    switches.button(3, true, 1400); tick(1400); assert(!switches.open() && board.sent.back() == 14);
}
