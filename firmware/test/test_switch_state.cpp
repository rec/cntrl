#include "midi_endpoints.h"
#include <cntrl/switches.h>
#include "patch_hardware.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

struct Board : TestPatchHardware, cntrl::midi::Sink {
    cntrl::midi::Bridge bridge{test_midi_outputs};
    cntrl::Pane views[3];
    bool writable = true, connected = true;
    std::string error_text;
    std::vector<unsigned> sent;
    unsigned switch_capacity() const override { return 5; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    bool send(const char *, const uint8_t *, unsigned) override { return false; }
    bool available(const char *) const override { return connected; }
    bool send_patch(unsigned owner, const char *, const uint8_t *p, unsigned n) override { return bridge.owned_stream(owner, 0, p, n); }
    cntrl::midi::Bridge::Delivery patch_delivery(unsigned owner) const override { return bridge.delivery(owner); }
    void cancel_patch(unsigned owner) override { bridge.cancel_owner(owner); }
    const cntrl::midi::MidiState *midi_state() const override { return &cntrl::midi::Sink::state(); }
    bool write_packet(unsigned, const cntrl::midi::Packet &p) override {
        if (!writable || !connected) return false;
        sent.push_back(unsigned(p.bytes[0]) << 16 | unsigned(p.bytes[1]) << 8 | p.bytes[2]); return true;
    }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    unsigned display_count() const override { return 3; }
    const char *control_label(cntrl::ControlRole role) const override {
        const char *labels[] = {"A", "B", "C", "D", "E", "F"}; return labels[unsigned(role)];
    }
    void show(unsigned pane, const cntrl::Pane &view) override { views[pane] = view; }
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *text) override { error_text = text; }
    const char *status(unsigned button) const { return views[button % 3].lines[(button / 3) * 3 + 2].text; }
};

int main(int argc, char **argv) {
    assert(argc == 3); std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    Board board; cntrl::Document document;
    assert(document.load(bytes.data(), unsigned(bytes.size()), board));
    cntrl::Switches switches(board, board, board);
    unsigned now = 0;
    auto tick = [&]() { for (unsigned i = 0; i < 64; ++i) { switches.tick(now++); board.bridge.service(board, now); } switches.tick(now); };
    auto press = [&](unsigned button) { switches.button(button, true, now); tick(); switches.button(button, false, now); tick(); };
    switches.enter(document, 0); assert(board.sent.empty());
    assert(!std::strcmp(board.status(0), "Unknown remote state"));
    press(0); assert((board.sent == std::vector<unsigned>{0xb01401, 0xb0077f}));
    assert(!std::strcmp(board.status(0), "On") && !std::strcmp(board.status(1), "On"));
    board.sent.clear(); press(1);
    assert((board.sent == std::vector<unsigned>{0xb01400, 0xb01501})); // Override keeps shared On.
    assert(!std::strcmp(board.status(0), "On") && !std::strcmp(board.status(1), "On"));
    board.sent.clear(); press(1); assert((board.sent == std::vector<unsigned>{0xb00700}));
    assert(!std::strcmp(board.status(0), "Off") && !std::strcmp(board.status(1), "Off"));
    board.sent.clear(); press(2);
    assert((board.sent == std::vector<unsigned>{0xb01500, 0xc00400}));
    assert(!std::strcmp(board.status(2), "On"));
    assert(!std::strcmp(board.status(0), "Unknown remote state")); // Program invalidates old parameters.
    press(3); assert(!std::strcmp(board.status(0), "On") && !std::strcmp(board.status(3), "On"));
    press(4); assert(!std::strcmp(board.status(4), "On"));
    assert(!std::strcmp(board.status(3), "Partial match"));
    // Exit must be delivered before enter and the selecting action under pressure.
    board.sent.clear(); board.writable = false; switches.button(0, true, now); tick();
    assert(board.sent.empty());
    board.writable = true; tick(); switches.button(0, false, now); tick();
    assert((board.sent == std::vector<unsigned>{0xb01600, 0xb01401, 0xb0077f}));
    // Closing runs the selected exit once, after explicit Off cleanup.
    board.sent.clear(); switches.close(); tick(); assert(!switches.open());
    unsigned exits = 0; for (auto p : board.sent) if (p == 0xb01400) ++exits;
    assert(exits == 1);
    unsigned offs = 0; for (auto p : board.sent) if (p == 0xb00700) ++offs;
    assert(offs == 1); // An uninvoked alias does not acquire duplicate Off cleanup.
    // Browser/external invocations establish the same state before entering Switches.
    cntrl::PatchExecutor browser(board, board, board, 5);
    assert(browser.start(document, unsigned(document.named_patch("loud"))));
    for (unsigned i = 0; i < 20; ++i) { browser.tick(now); board.bridge.service(board, now++); }
    board.sent.clear(); switches.enter(document, 0); tick();
    assert(board.sent.empty() && !std::strcmp(board.status(0), "On") && !std::strcmp(board.status(1), "On"));
    switches.close(); tick(); assert(board.sent.empty()); // Visiting leaves previously established values alone.
    switches.enter(document, 0); tick();
    // A disconnected route loses known state and pending work is not replayed.
    board.connected = false; board.bridge.disconnect_output(0); tick();
    assert(!std::strcmp(board.status(0), "Unknown remote state"));
    board.connected = true; tick(); assert(board.sent.empty());
    // Native decoder validates hook indices and the override flag.
    auto bad = bytes; unsigned hook = unsigned(document.switch_hooks(0) - bytes.data());
    bad[hook + 4] = 2; cntrl::put32(bad.data() + 20, cntrl::checksum(bad.data() + 28, unsigned(bad.size()) - 28));
    cntrl::Document rejected; assert(!rejected.load(bad.data(), unsigned(bad.size()), board));
    assert(board.error_text.empty());
    // Bank selection is latched by a PC, and NRPN values survive selector changes.
    Board tracked; const auto &state = *tracked.midi_state();
    auto message = [&](unsigned status, unsigned a = 0, unsigned b = 0) {
        uint8_t cin = status == 0xf0 ? 4 : uint8_t(status >> 4);
        assert(tracked.submit(0, {cin, {uint8_t(status), uint8_t(a), uint8_t(b)}}));
    };
    message(0xb0, 0, 1); message(0xc0, 4); message(0xb0, 0, 2);
    assert(state.value(0, 0, 130) == 1 && state.value(0, 0, 0) == 2);
    message(0xb0, 99, 1); message(0xb0, 98, 2);
    message(0xb0, 6, 3); message(0xb0, 38, 4);
    assert(state.value(0, 0, 386) == 388);
    message(0xb0, 98, 3); assert(state.value(0, 0, 386) == 388);
    message(0xf0); assert(state.value(0, 0, 386) == -1);

    std::ifstream held_file(argv[2], std::ios::binary);
    std::vector<uint8_t> held_bytes((std::istreambuf_iterator<char>(held_file)), {});
    Board held; cntrl::Document held_document;
    assert(held_document.load(held_bytes.data(), unsigned(held_bytes.size()), held));
    cntrl::Switches momentary(held, held, held);
    auto drain = [&]() { for (unsigned i = 0; i < 64; ++i) { momentary.tick(now++); held.bridge.service(held, now); } };
    momentary.enter(held_document, 0);
    held.writable = false; momentary.button(0, true); momentary.button(0, false); drain();
    assert(held.sent.empty());
    held.writable = true; drain();
    assert((held.sent == std::vector<unsigned>{0xb01401, 0xb0077f, 0xb00700}));
    // Releasing while enter was blocked still performs Off, and retains selection.
    held.sent.clear(); momentary.button(1, true); drain(); momentary.button(1, false); drain();
    assert((held.sent == std::vector<unsigned>{0xb01400, 0xb00801}));
    // Close during an accepted enter runs enter then exit, with the selecting action cancelled.
    held.sent.clear(); held.writable = false; momentary.button(0, true); drain();
    momentary.close(); held.writable = true; drain();
    assert(!momentary.open());
    assert((held.sent == std::vector<unsigned>{0xb01401, 0xb01400}));
    assert(held.error_text.empty());
}
