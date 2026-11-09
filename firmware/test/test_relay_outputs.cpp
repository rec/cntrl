#include "patch_hardware.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

struct Board : TestPatchHardware {
    std::vector<std::pair<std::string, bool>> writes;
    bool qualified = true, midi_writable = true, relay_writable = true, tip = false, ring = false;
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    bool send(const char *, const uint8_t *, unsigned) override { return midi_writable; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *message) override { std::fprintf(stderr, "%s\n", message); assert(false); }
    bool supports_relay(const char *name) const override { return qualified && (!std::strcmp(name, "amp.tip") || !std::strcmp(name, "amp.ring")); }
    bool relay_write(const char *name, bool engaged) override {
        assert(supports_relay(name)); writes.emplace_back(name, engaged);
        if (!relay_writable) return false;
        (!std::strcmp(name, "amp.tip") ? tip : ring) = engaged; return true;
    }
};

int main(int argc, char **argv) {
    assert(argc == 2); std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
    Board board; cntrl::Document document;
    assert(document.load(bytes.data(), unsigned(bytes.size()), board));
    Board unsupported; unsupported.qualified = false;
    cntrl::Document invalid; assert(!invalid.load(bytes.data(), unsigned(bytes.size()), unsupported));
    cntrl::Controller controller(board, board, board); controller.activate(document);
    assert(!board.tip && board.ring && board.writes.size() == 2);
    cntrl::PatchExecutor execution(board, board, board, 8);
    auto invoke = [&](const char *name, uint32_t now) {
        assert(execution.start(document, unsigned(document.named_patch(name))));
        for (unsigned i = 0; i < 8 && execution.busy(); ++i) execution.tick(now);
    };
    invoke("engage", 0); assert(board.tip);
    invoke("toggle", 0); assert(!board.tip);
    invoke("toggle", 0); assert(board.tip);
    invoke("disengage", 0); assert(!board.tip);
    invoke("pulse", 10); assert(board.tip);
    assert(board.relays.tick(109) && board.tip);
    assert(board.relays.tick(110) && !board.tip);
    invoke("ring", 200); assert(!board.ring && !board.tip);
    assert(board.relays.tick(249) && !board.ring);
    assert(board.relays.tick(250) && board.ring);
    invoke("pulse", 300); invoke("ring", 320);
    assert(board.relays.tick(369) && board.tip && !board.ring);
    assert(board.relays.tick(370) && board.tip && board.ring);
    invoke("engage", 380);
    assert(board.relays.tick(400) && board.tip); // A later command cancels the old deadline.
    invoke("disengage", 410);
    board.midi_writable = false; invoke("blocked", 500);
    assert(execution.busy() && board.tip);
    assert(board.relays.tick(600) && !board.tip); execution.tick(600);
    assert(execution.busy()); // MIDI backpressure does not hold the relay pulse open.
    execution.cancel(); board.midi_writable = true;
    invoke("cancelled", 700); assert(execution.busy() && board.tip);
    execution.cancel(); assert(board.relays.tick(799) && board.tip);
    assert(board.relays.tick(800) && !board.tip); // Issued pulses finish independently of the calling patch.
    invoke("pulse", 0xfffffff0u);
    assert(board.relays.tick(83) && board.tip);
    assert(board.relays.tick(84) && !board.tip);
    invoke("pulse", 1000); assert(board.tip);
    assert(board.relays.reset() && !board.tip && board.ring);
    assert(board.relays.tick(1100));
    assert(!board.relays.execute("amp.tip", 0, 0, 1200)); // Setup exit removes the old commands and deadlines.
    controller.activate(document); invoke("engage", 1300);
    controller.activate(document); assert(!board.tip && board.ring); // Setup activation restores configured rests.
    invoke("pulse", 1400); board.relay_writable = false;
    assert(!board.relays.tick(1500) && board.tip);
    unsigned attempts = unsigned(board.writes.size());
    assert(board.relays.tick(1501) && board.writes.size() == attempts); // Failed restorations are reported once, without silent retries.
    board.relay_writable = true; invoke("disengage", 1502); assert(!board.tip);
}
