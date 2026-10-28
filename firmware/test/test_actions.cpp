#include <cntrl/switches.h>
#include "patch_hardware.h"
#include "storage.h"
#include <cassert>
#include <fstream>
#include <string>
#include <vector>

struct Board : TestPatchHardware, cntrl::midi::Sink, cntrl::Replies {
    cntrl::midi::Bridge bridge;
    bool connected = true, writable = true;
    unsigned blocked_route = 9;
    bool locked = false;
    std::string response;
    bool configuration_locked() const override { return locked; }
    void reply(const char *text) override { response = text; }
    std::vector<cntrl::midi::Packet> sent;
    cntrl::Pane views[3];
    unsigned switch_capacity() const override { return 5; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    bool send(const char *, const uint8_t *, unsigned) override { return false; }
    bool available(const char *) const override { return connected; }
    bool send_patch(unsigned owner, const char *, const uint8_t *bytes, unsigned n) override { return bridge.owned_send(owner, 0, bytes, n); }
    cntrl::midi::Bridge::Delivery patch_delivery(unsigned owner) const override { return bridge.delivery(owner); }
    void cancel_patch(unsigned owner) override { bridge.cancel_owner(owner); }
    bool submit(unsigned route, const cntrl::midi::Packet &p) override { if (!writable || route == blocked_route) return false; sent.push_back(p); return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned pane, const cntrl::Pane &view) override { views[pane] = view; }
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
};

int main(int argc, char **argv) {
    assert(argc == 2); std::ifstream file(argv[1]); std::string hex; file >> hex;
    std::vector<uint8_t> bytes;
    for (unsigned i = 0; i < hex.size(); i += 2) bytes.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
    Board board; cntrl::Document doc; assert(doc.load(bytes.data(), unsigned(bytes.size()), board)); assert(doc.switches() == 3);
    cntrl::Switches actions(board, board, board);
    Board rearm_board; cntrl::Switches rearm(rearm_board, rearm_board, rearm_board); rearm.enter(doc, 3);
    rearm.button(0, false); rearm.button(0, true);
    for (unsigned i = 0; i < 8; ++i) { rearm.tick(); rearm_board.bridge.service(rearm_board, 0); }
    assert(rearm_board.sent.size() == 1 && rearm_board.sent.back().bytes[2] == 127); // B may remain held while A rearms.
    auto tick = [&]() { for (unsigned i = 0; i < 8; ++i) { actions.tick(); board.bridge.service(board, 0); } };
    actions.enter(doc, 1); actions.button(0, true); tick(); assert(board.sent.empty());
    actions.button(0, false); actions.button(0, true); actions.button(0, true); tick();
    assert(board.sent.size() == 1 && board.sent.back().bytes[2] == 127);
    actions.button(0, false); tick(); assert(board.sent.size() == 2 && board.sent.back().bytes[2] == 0);
    actions.button(1, true); tick(); actions.button(1, false); tick(); assert(board.sent.size() == 3);
    actions.button(1, true); tick(); assert(board.sent.size() == 4 && board.sent.back().bytes[2] == 85);
    actions.button(0, true); actions.button(0, false); tick(); assert(board.sent.size() == 4);
    board.writable = false; actions.button(0, true); tick(); actions.button(0, false); tick();
    board.writable = true; tick(); tick(); assert(board.sent.size() == 6); assert(board.sent[4].bytes[2] == 127 && board.sent[5].bytes[2] == 0);
    actions.button(0, true); tick(); board.connected = false; tick(); assert(!std::strcmp(board.views[0].lines[2].text, "Unknown remote state"));
    unsigned before = unsigned(board.sent.size()); board.connected = true; tick(); assert(board.sent.size() == before);
    actions.button(0, false); actions.button(0, true); tick(); tick(); assert(board.sent.size() == before + 2); // Explicit Off baseline, then On.
    board.writable = false; actions.close(); tick(); assert(actions.open() && actions.closing());
    actions.close(); assert(!actions.open() && actions.abandoned()); board.writable = true; tick(); board.bridge.service(board, 0); assert(board.sent.size() == before + 2);
    actions.enter(doc, 0); actions.button(0, true); tick(); actions.close(); tick(); assert(!actions.open());
    // Ordinary queue saturation cannot consume the action's cleanup slot.
    uint8_t cc[] = {0xb0, 3, 1}; for (unsigned i = 0; i < cntrl::midi::Bridge::pedal_capacity; ++i) assert(board.bridge.pedal(0, cc, 3));
    assert(!board.bridge.pedal(0, cc, 3)); assert(board.bridge.owned_send(0, 0, cc, 3)); board.bridge.cancel_owner(0);
    // Decoder rejects malformed assignment commands before activation.
    auto invalid = bytes; unsigned offset = unsigned(doc.switch_assignment(0) - bytes.data()); invalid[offset + 1] = 3;
    cntrl::put32(invalid.data() + 20, cntrl::checksum(invalid.data() + 28, unsigned(invalid.size()) - 28));
    cntrl::Document bad; assert(!bad.load(invalid.data(), unsigned(invalid.size()), board));
    // Patch commands resolve the current document and preserve MIDI encoding.
    Board patches; cntrl::Switches alternate(patches, patches, patches); alternate.enter(doc, 0);
    alternate.button(2, true); for (unsigned i = 0; i < 8; ++i) { alternate.tick(); patches.bridge.service(patches, 0); }
    assert(patches.sent.size() == 1 && patches.sent.back().cin == 12 && patches.sent.back().bytes[0] == 0xc1 && patches.sent.back().bytes[1] == 2);
    alternate.button(2, false); for (unsigned i = 0; i < 8; ++i) { alternate.tick(); patches.bridge.service(patches, 0); }
    assert(patches.sent.size() == 2 && patches.sent.back().bytes[1] == 127);
    // A blocked route cannot starve another owner's completion.
    Board fair; fair.blocked_route = 0;
    assert(fair.bridge.owned_send(0, 0, cc, 3)); assert(fair.bridge.owned_send(1, 1, cc, 3)); fair.bridge.service(fair, 0);
    assert(fair.bridge.delivery(0) == cntrl::midi::Bridge::Delivery::pending && fair.bridge.delivery(1) == cntrl::midi::Bridge::Delivery::complete);
    fair.bridge.host_connected(true); assert(fair.bridge.owned_send(2, 8, cc, 3)); fair.bridge.host_connected(false);
    assert(fair.bridge.delivery(2) == cntrl::midi::Bridge::Delivery::cancelled);
    auto edited = bytes; edited[76 + 64] = 2;
    cntrl::put32(edited.data() + 20, cntrl::checksum(edited.data() + 28, unsigned(edited.size()) - 28));
    cntrl::Document changed; assert(changed.load(edited.data(), unsigned(edited.size()), patches));
    cntrl::Switches channel(patches, patches, patches); channel.enter(changed, 0); channel.button(0, true);
    for (unsigned i = 0; i < 8; ++i) { channel.tick(); patches.bridge.service(patches, 0); }
    assert(patches.sent.back().bytes[0] == 0xb0);
    TestStorage storage; Board transfer; TestController controller(transfer);
    cntrl::Documents documents(storage, transfer, controller, transfer); documents.boot(); transfer.locked = true;
    for (char c : std::string("CNTRL SETUP GET 706d616e\n")) documents.receive(uint8_t(c), 0);
    assert(transfer.response == "CNTRL ERROR configuration-locked" && !documents.busy());
    return 0;
}
