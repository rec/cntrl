#include "patch_hardware.h"
#include <cntrl/auxiliary_switches.h>
#include <cntrl/switches.h>
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

struct Board : TestPatchHardware {
    Board() { assert(auxiliary.prepare(*this)); }
    cntrl::Switches switches{*this, *this, *this};
    cntrl::AuxiliarySwitches auxiliary;
    std::vector<std::vector<uint8_t>> sent;
    unsigned contacts[4] = {};
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    unsigned switch_capacity() const override { return 5; }
    bool supports_trs_mode(unsigned port, cntrl::midi::TrsMode mode) const override {
        return mode == cntrl::midi::TrsMode::off || (port < 2 && mode == cntrl::midi::TrsMode::aux);
    }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    bool send(const char *, const uint8_t *bytes, unsigned size) override { sent.emplace_back(bytes, bytes + size); return true; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *message) override { std::fprintf(stderr, "%s\n", message); assert(false); }
    void scan(const cntrl::Document &document, uint32_t now) {
        auxiliary.scan(*this, document, now, [&](unsigned port) { return contacts[port]; },
            [&](unsigned input) { return switches.debounce_ms(input); });
        for (unsigned input = input_count(); input < 32; ++input) {
            if (auxiliary.event(input) & cntrl::press_event) switches.button(input, true, now);
            if (auxiliary.event(input) & cntrl::release_event) switches.button(input, false, now);
        }
        for (unsigned i = 0; i < 8; ++i) switches.tick(now, auxiliary.held());
    }
};

int main(int argc, char **argv) {
    cntrl::memory_budget = 192 * 1024;
    assert(argc == 2); std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
    Board board; cntrl::Document document;
    assert(document.load(bytes.data(), unsigned(bytes.size()), board));
    assert(document.inputs() == 12 && !std::strcmp(document.input_label(8), "configurable-1.both"));
    Board startup; startup.contacts[0] = 1; startup.scan(document, 0);
    startup.switches.enter(document, startup.auxiliary.held());
    startup.contacts[0] = 3; startup.scan(document, 1); startup.scan(document, 21);
    assert(startup.sent.empty()); // A held third switch may close tip and ring at slightly different instants.
    startup.contacts[0] = 0; startup.scan(document, 22); startup.scan(document, 42);
    assert(startup.sent.empty());
    board.scan(document, 0); board.switches.enter(document, board.auxiliary.held());
    board.contacts[0] = 1; board.scan(document, 1);
    board.contacts[0] = 3; board.scan(document, 3); board.scan(document, 22);
    assert(board.sent.empty()); board.scan(document, 23);
    assert(board.sent.size() == 1 && board.sent.back()[1] == 64 && board.sent.back()[2] == 127);
    board.contacts[0] = 0; board.scan(document, 24);
    board.contacts[0] = 3; board.scan(document, 25); board.scan(document, 46);
    assert(board.sent.size() == 1); // Bounce cannot turn a combined press into separate tip/ring actions.
    board.contacts[0] = 0; board.scan(document, 50); board.scan(document, 70);
    assert(board.sent.size() == 2 && board.sent.back()[2] == 0);
    board.contacts[1] = 3; board.scan(document, 80); board.scan(document, 100);
    assert(board.sent.size() == 4 && board.sent[2][1] == 3 && board.sent[3][1] == 4);
    board.switches.close(); board.scan(document, 101);
    board.contacts[0] = 3; board.scan(document, 110); board.scan(document, 130);
    unsigned before = unsigned(board.sent.size());
    board.switches.enter(document, board.auxiliary.held()); board.scan(document, 131);
    assert(board.sent.size() == before); // A held input is disarmed on context entry, even above bit 7.
    board.contacts[0] = 0; board.scan(document, 140); board.scan(document, 160);
    assert(board.sent.size() == before);
    board.contacts[0] = 3; board.scan(document, 170); board.scan(document, 190);
    assert(board.sent.size() == before + 1 && board.sent.back()[2] == 127);
    auto disabled = bytes; disabled[document.metadata() + cntrl::trs_modes_offset] = 0;
    disabled[document.metadata() + cntrl::scrolling_offset + 5] &= ~4u;
    cntrl::put32(disabled.data() + 20, cntrl::checksum(disabled.data() + 28, unsigned(disabled.size()) - 28));
    cntrl::Document off; assert(off.load(disabled.data(), unsigned(disabled.size()), board));
    board.scan(off, 191);
    assert(board.sent.size() == before + 2 && board.sent.back()[2] == 0); // Role removal releases owned momentary actions immediately.
}
