#include "patch_hardware.h"
#include <cntrl/switches.h>
#include <cassert>
#include <fstream>
#include <string>
#include <vector>

using Bytes = std::vector<uint8_t>;

struct Board : TestPatchHardware, cntrl::midi::Sink {
    cntrl::midi::Bridge bridge{test_midi_outputs};
    bool online = true, accepting = true, writable = true;
    Bytes wire[2];
    cntrl::Pane panes[3];
    std::string last_error;
    bool send(const char *, const uint8_t *, unsigned) override { return false; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    unsigned switch_capacity() const override { return 5; }
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    bool available(const char *) const override { return online; }
    bool send_patch(unsigned owner, const char *output, const uint8_t *bytes, unsigned size) override {
        return accepting && bridge.owned_stream(owner, !std::strcmp(output, "trs") ? 1 : 0, bytes, size);
    }
    cntrl::midi::Bridge::Delivery patch_delivery(unsigned owner) const override { return bridge.delivery(owner); }
    void cancel_patch(unsigned owner) override { bridge.cancel_owner(owner); }
    bool write_packet(unsigned output, const cntrl::midi::Packet &packet) override {
        if (!writable) return false;
        wire[output].insert(wire[output].end(), packet.bytes, packet.bytes + packet.size()); return true;
    }
    void show(unsigned pane, const cntrl::Pane &content) override { panes[pane] = content; }
    void error(const char *message) override { last_error = message; }
};

void run(cntrl::PatchExecutor &executor, Board &board, const cntrl::Document &document, const char *patch) {
    int node = document.named_patch(patch); assert(node >= 0);
    assert(executor.start(document, unsigned(node)));
    for (unsigned i = 0; executor.busy() && i < 100; ++i) { executor.tick(i); board.bridge.service(board, i); }
    assert(executor.status() == cntrl::PatchExecutor::Status::completed);
}

std::string displayed(const cntrl::Document &document, const Board &board, unsigned line) {
    cntrl::Pane pane; document.page(pane, 0, 0, false, &board.counters);
    return pane.lines[line].text;
}

void test_shared_initial_value_wrap_and_reverse(const cntrl::Document &document) {
    Board board; cntrl::PatchExecutor up(board, board, board, 0), down(board, board, board, 1);
    assert(displayed(document, board, 1) == "95");
    run(down, board, document, "down"); // The first press sends the initial value in either direction.
    assert((board.wire[0] == Bytes{0xb1,7,95}));
    run(up, board, document, "up"); assert(displayed(document, board, 1) == "100");
    run(up, board, document, "up"); assert(displayed(document, board, 1) == "0");
    run(down, board, document, "down"); assert(displayed(document, board, 1) == "100");
    run(down, board, document, "down"); assert(displayed(document, board, 1) == "95");
    assert((board.wire[0] == Bytes{0xb1,7,95,0xb1,7,100,0xb1,7,0,0xb1,7,100,0xb1,7,95}));
    run(up, board, document, "program"); assert(displayed(document, board, 2) == "128");
    run(up, board, document, "program"); assert(displayed(document, board, 2) == "1");
    run(down, board, document, "program_down"); assert(displayed(document, board, 2) == "128");
    assert((board.wire[1] == Bytes{0xcf,127,0xcf,0,0xcf,127}));
}

void test_bounce_reaches_endpoints_with_negative_and_large_steps(const cntrl::Document &document) {
    Board board; cntrl::PatchExecutor executor(board, board, board, 0);
    for (unsigned i = 0; i < 7; ++i) run(executor, board, document, "bounded");
    assert((board.wire[0] == Bytes{0xb0,1,3,0xb0,1,0,0xb0,1,4,0xb0,1,5,0xb0,1,1,0xb0,1,0,0xb0,1,4}));
    for (unsigned i = 0; i < 3; ++i) run(executor, board, document, "fixed");
    assert(board.wire[0][board.wire[0].size() - 1] == 64);
}

void test_rejected_blocked_cancelled_and_disconnected_sends(const cntrl::Document &document) {
    Board board; cntrl::PatchExecutor executor(board, board, board, 0);
    board.accepting = false;
    assert(executor.start(document, unsigned(document.named_patch("up"))));
    for (unsigned i = 0; i < 10; ++i) executor.tick(i);
    assert(executor.busy() && displayed(document, board, 1) == "95");
    assert(board.wire[0].empty()); executor.cancel();
    board.accepting = true; board.writable = false;
    assert(executor.start(document, unsigned(document.named_patch("up"))));
    executor.tick(); assert(displayed(document, board, 1) == "95");
    board.bridge.service(board, 0);
    executor.tick(); assert(executor.busy() && board.wire[0].empty());
    executor.cancel(); // Accepted values are retained even if later delivery is cancelled.
    board.writable = true; run(executor, board, document, "up");
    assert((board.wire[0] == Bytes{0xb1,7,100}));
    board.online = false;
    assert(executor.start(document, unsigned(document.named_patch("up")))); executor.tick();
    assert(executor.status() == cntrl::PatchExecutor::Status::failed);
    assert(displayed(document, board, 1) == "100");
    board.online = true; run(executor, board, document, "up");
    assert(displayed(document, board, 1) == "0");
}

void test_switches_share_displayed_value_and_setup_reload_resets(const cntrl::Document &document) {
    Board board; cntrl::Controller controller(board, board, board);
    controller.activate(document);
    cntrl::Switches switches(board, board, board); switches.enter(document, 0);
    for (unsigned press = 0; press < 2; ++press) {
        switches.button(0, true);
        for (unsigned i = 0; i < 30; ++i) { switches.tick(i); board.bridge.service(board, i); }
        switches.button(0, false);
    }
    assert(displayed(document, board, 1) == "100");
    assert(!std::strcmp(board.panes[0].lines[1].text, "100"));
    assert(!std::strcmp(board.panes[1].lines[1].text, "100"));
    assert(board.last_error.empty());
    controller.activate(document);
    assert(displayed(document, board, 1) == "95");
    cntrl::PatchExecutor executor(board, board, board, 2); run(executor, board, document, "down");
    assert(board.wire[0].back() == 95);
}

int main(int argc, char **argv) {
    assert(argc == 2); std::ifstream file(argv[1]); std::string hex; file >> hex;
    Bytes bytes; for (unsigned i = 0; i < hex.size(); i += 2) bytes.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
    Board board; cntrl::Document document; assert(document.load(bytes.data(), unsigned(bytes.size()), board));
    test_shared_initial_value_wrap_and_reverse(document);
    test_bounce_reaches_endpoints_with_negative_and_large_steps(document);
    test_rejected_blocked_cancelled_and_disconnected_sends(document);
    test_switches_share_displayed_value_and_setup_reload_resets(document);
    // Invalid references and definitions are rejected before execution.
    Bytes invalid = bytes;
    unsigned offset = unsigned(document.counter(0) - bytes.data()); invalid[offset + 98] = 127;
    cntrl::put32(invalid.data() + 20, cntrl::checksum(invalid.data() + 28, unsigned(invalid.size()) - 28));
    cntrl::Document rejected; assert(!rejected.load(invalid.data(), unsigned(invalid.size()), board));
}
