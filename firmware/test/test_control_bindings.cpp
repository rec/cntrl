#include "patch_hardware.h"
#include "storage.h"
#include <cntrl/switches.h>
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

struct Board : TestPatchHardware, cntrl::midi::Sink, cntrl::Replies {
    cntrl::midi::Bridge bridge{test_midi_outputs};
    cntrl::Switches switches{*this, *this, *this};
    cntrl::PatchExecutor entry{*this, *this, *this, 8};
    std::vector<cntrl::midi::Packet> sent;
    cntrl::Pane panes[3];
    uint8_t held = 0;
    uint32_t now = 0;
    unsigned pins = 0;
    bool exiting = false;
    std::string response;
    void reply(const char *text) override { response = text; }
    void data(const uint8_t *, unsigned) override { assert(false); }
    void begin() override { ++pins; }
    void end() override { assert(pins); --pins; }
    unsigned switch_capacity() const override { return 5; }
    unsigned input_count() const override { return 8; }
    const char *input_label(unsigned input) const override {
        static constexpr const char *labels[] = {"A", "B", "C", "D", "E", "F", "G", "H"}; return labels[input];
    }
    bool supports_trs_mode(unsigned, cntrl::midi::TrsMode) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    bool send(const char *, const uint8_t *, unsigned) override { return false; }
    bool send_patch(unsigned owner, const char *, const uint8_t *bytes, unsigned size) override { return bridge.owned_send(owner, 0, bytes, size); }
    cntrl::midi::Bridge::Delivery patch_delivery(unsigned owner) const override { return bridge.delivery(owner); }
    void cancel_patch(unsigned owner) override { bridge.cancel_owner(owner); }
    bool write_packet(unsigned, const cntrl::midi::Packet &packet) override { sent.push_back(packet); return true; }
    void show(unsigned pane, const cntrl::Pane &content) override { panes[pane] = content; }
    void error(const char *message) override { std::fprintf(stderr, "%s\n", message); }
    bool state(unsigned operation, uint32_t) override { assert(operation == 10); exiting = true; return true; }
    cntrl::midi::Bridge::Delivery prepare_leave(cntrl::PatchExecutor &invocation) override { return switches.prepare_leave(invocation); }
    cntrl::midi::Bridge::Delivery bind_controls(const cntrl::Document &document, unsigned node, cntrl::PatchExecutor &invocation) override {
        return switches.bind_controls(document, node, invocation);
    }
    void tick(unsigned count = 32) {
        for (unsigned i = 0; i < count; ++i) { entry.tick(now); switches.tick(now++, held); bridge.service(*this, now); }
    }
    void button(unsigned input, bool down) {
        held = down ? held | (1u << input) : held & ~(1u << input);
        switches.button(input, down, now); tick();
    }
};

int main(int argc, char **argv) {
    cntrl::memory_budget = 192 * 1024;
    assert(argc == 3);
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
    Board board; cntrl::Document document;
    assert(document.load(bytes.data(), unsigned(bytes.size()), board));
    if (!std::strcmp(argv[2], "--validate")) return 0;
    if (!std::strcmp(argv[2], "--reject")) {
        assert(board.entry.start(document, unsigned(document.named_patch("bad")))); board.tick();
        assert(!board.switches.open() && !board.switches.performance() && board.pins == 0); return 0;
    }
    // A held opening switch cannot trigger its newly installed action.
    board.held = 4;
    assert(board.entry.start(document, unsigned(document.named_patch("verse")))); board.tick();
    assert(board.switches.performance() && board.switches.open());
    assert(board.sent.size() == 1 && board.sent[0].bytes[2] == 30);
    assert(!std::strcmp(board.panes[1].lines[0].text, "verse"));
    assert(board.switches.expression(0).channel == 2 && board.switches.expression(0).controller == 11);
    board.button(2, false);
    // D and F are ordinary bound switches; navigation does not intercept them.
    board.button(3, true); board.button(3, false);
    assert(board.sent.back().bytes[2] == 100);
    board.button(7, true); board.button(7, false);
    assert(board.sent.back().bytes[2] == 100);
    board.button(0, true);
    assert(board.sent.back().bytes[1] == 64 && board.sent.back().bytes[2] == 127);
    board.button(1, true);
    assert(board.sent.back().bytes[1] == 10 && board.sent.back().bytes[2] == 1);
    unsigned before = unsigned(board.sent.size());
    // Switching contexts from a bound action drains momentary and gesture
    // releases before the new entry MIDI, even while those buttons stay held.
    board.button(2, true);
    assert(board.switches.performance() && !board.switches.closing());
    bool sustain_released = false, gesture_released = false;
    for (unsigned i = before; i < board.sent.size(); ++i) {
        const auto &packet = board.sent[i];
        if (packet.bytes[1] == 64 && packet.bytes[2] == 0) sustain_released = true;
        if (packet.bytes[1] == 10 && packet.bytes[2] == 2) gesture_released = true;
        if (packet.bytes[1] == 7 && packet.bytes[2] == 90) assert(sustain_released && gesture_released);
    }
    assert(sustain_released && gesture_released && board.sent.back().bytes[2] == 90);
    before = unsigned(board.sent.size());
    board.button(0, false); board.button(1, false); board.button(2, false);
    assert(board.sent.size() == before);
    // Pending double taps are cancelled when another patch takes ownership.
    board.button(4, true); board.button(4, false);
    assert(board.entry.start(document, unsigned(document.named_patch("empty")))); board.tick(400);
    assert(board.switches.open());
    assert(board.sent.size() == before);
    // A new bound map can be entered again after an intentionally empty map.
    assert(board.entry.start(document, unsigned(document.named_patch("verse")))); board.tick();
    board.button(5, true);
    assert(board.exiting && !board.switches.open() && !board.switches.performance());
    assert(board.pins == 0);
    // The normal editor and history path can still reach a program wrapped
    // by bindings, and exported derivations retain the edited volume.
    TestStorage storage; TestController controller(board);
    cntrl::Documents documents(storage, board, controller, board); documents.boot();
    auto command = [&](const std::string &line) { for (char c : line + "\n") documents.receive(uint8_t(c), 0); };
    char put[128]; std::snprintf(put, sizeof(put), "CNTRL SETUP PUT 54657374 %u %08lx 0",
        unsigned(bytes.size()), (unsigned long)cntrl::checksum(bytes.data(), unsigned(bytes.size())));
    command(put); assert(board.response == "CNTRL SEND");
    for (uint8_t b : bytes) documents.receive(b, 0);
    while (documents.busy()) documents.tick(0);
    assert(board.response == "CNTRL OK" && documents.select("Test")); documents.tick(0);
    assert(documents.edit_begin());
    assert(documents.edit_adjust(4, 3, 1) && documents.edit_value(4, 3) == 101);
    assert(documents.edit_save(0)); while (documents.busy()) documents.tick(0);
    const auto &saved = documents.document();
    std::ofstream output(argv[2], std::ios::binary);
    output.write(reinterpret_cast<const char *>(saved.data()), saved.length());
}
