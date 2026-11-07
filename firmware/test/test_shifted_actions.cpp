#include <cntrl/switches.h>
#include "patch_hardware.h"
#include <cassert>
#include <fstream>
#include <vector>

struct Board : TestPatchHardware, cntrl::midi::Sink {
    cntrl::Switches *switches = nullptr;
    cntrl::midi::Bridge bridge;
    std::vector<cntrl::midi::Packet> sent;
    cntrl::Pane views[3];
    bool writable = true, connected = true;
    unsigned operations = 0;
    unsigned switch_capacity() const override { return 5; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    bool available(const char *) const override { return connected; }
    bool send(const char *, const uint8_t *, unsigned) override { return false; }
    bool send_patch(unsigned owner, const char *, const uint8_t *bytes, unsigned n) override { return bridge.owned_send(owner, 0, bytes, n); }
    cntrl::midi::Bridge::Delivery patch_delivery(unsigned owner) const override { return bridge.delivery(owner); }
    void cancel_patch(unsigned owner) override { bridge.cancel_owner(owner); }
    const cntrl::midi::MidiState *midi_state() const override { return &cntrl::midi::Sink::state(); }
    bool state(unsigned operation, uint32_t) override {
        assert(operation == 11 || operation == 12); ++operations;
        return switches->shift(operation == 11);
    }
    bool write_packet(unsigned, const cntrl::midi::Packet &packet) override {
        if (!writable) return false;
        sent.push_back(packet); return true;
    }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned panel, const cntrl::Pane &view) override { views[panel] = view; }
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override { assert(false); }
};

int main(int argc, char **argv) {
    assert(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    Board board; cntrl::Document document;
    assert(document.load(bytes.data(), unsigned(bytes.size()), board));
    cntrl::Switches switches(board, board, board); board.switches = &switches;
    assert(!switches.shift(true));
    auto step = [&](uint32_t now = 0) {
        for (unsigned i = 0; i < 64; ++i) { switches.tick(now); board.bridge.service(board, now); }
    };
    auto press = [&](unsigned button) { switches.button(button, true); step(); };
    auto release = [&](unsigned button) { switches.button(button, false); step(); };
    auto last = [&](unsigned controller, unsigned value) {
        assert(!board.sent.empty());
        const auto &packet = board.sent.back();
        assert(packet.bytes[1] == controller && packet.bytes[2] == value);
    };
    auto shifted = [&]() { return std::strstr(board.views[2].lines[15].text, "Shifted") != nullptr; };
    switches.enter(document, 0);
    press(1); last(10, 127); release(1); last(10, 0);
    press(0); assert(shifted());
    press(1); last(11, 100);
    release(0); assert(!shifted()); release(1); last(11, 0);
    press(1); last(10, 127); release(1); last(10, 0);
    press(2); last(20, 127); release(2);
    press(0);
    unsigned before = unsigned(board.sent.size());
    press(2); assert(board.sent.size() == before + 2);
    assert(board.sent[before].bytes[1] == 20 && board.sent[before].bytes[2] == 0);
    last(21, 100); release(2); release(0);
    // A layer transition waits for the previous layer's cleanup under pressure.
    board.writable = false; before = unsigned(board.sent.size());
    press(2); release(2); assert(board.sent.size() == before);
    board.writable = true; step();
    assert(board.sent.size() == before + 2);
    assert(board.sent[before].bytes[1] == 21 && board.sent[before].bytes[2] == 0);
    last(20, 127);
    press(2); last(20, 0); release(2);
    press(2); last(20, 127); release(2);
    // A long-release action stays paired with its shifted long press.
    press(0);
    switches.button(3, true, 1000); step(1010); step(1040); last(30, 3);
    release(0);
    switches.button(3, false, 1060); step(1070); last(30, 4);
    switches.button(3, true, 1100); step(1110); last(30, 1);
    switches.button(3, false, 1120); step(1130);
    // Hooks and repeated indirect shifts change the flag without reselecting.
    unsigned operations = board.operations;
    press(4); release(4); assert(!shifted());
    assert(board.operations == operations + 5);
    press(0); assert(shifted());
    switches.close(); step(); assert(!switches.open()); last(20, 0);
    switches.enter(document, 0); assert(!shifted());
    press(1); last(10, 127); release(1); last(10, 0);
    // Held entry controls do not establish a shifted layer until rearmed.
    switches.close(); step(); switches.enter(document, 1);
    press(0); assert(!shifted()); release(0); press(0); assert(shifted());
    press(1); last(11, 100);
    board.writable = false; switches.close(); step();
    assert(switches.open() && switches.closing());
    switches.close(); assert(!switches.open() && switches.abandoned());
    board.writable = true; switches.enter(document, 0); assert(!shifted());
    press(0); press(1); last(11, 100);
    board.connected = false; step();
    assert(!std::strcmp(board.views[1].lines[2].text, "Unknown remote state"));
    before = unsigned(board.sent.size());
    board.connected = true; step(); assert(board.sent.size() == before);
    release(0); release(1); switches.close(); step(); assert(!switches.open());
    // Bad alternate nodes and gesture timing are rejected before activation.
    auto invalid = bytes;
    unsigned alternate = unsigned(document.switch_shifted(1) - bytes.data());
    invalid[alternate] = invalid[alternate + 1] = 255;
    cntrl::put32(invalid.data() + 20, cntrl::checksum(invalid.data() + 28, unsigned(invalid.size()) - 28));
    cntrl::Document bad; assert(!bad.load(invalid.data(), unsigned(invalid.size()), board));
    invalid = bytes; alternate = unsigned(document.switch_shifted(3) - bytes.data());
    invalid[alternate + 16] = invalid[alternate + 17] = 255;
    cntrl::put32(invalid.data() + 20, cntrl::checksum(invalid.data() + 28, unsigned(invalid.size()) - 28));
    assert(!bad.load(invalid.data(), unsigned(invalid.size()), board));
}
