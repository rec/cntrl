#include "patch_hardware.h"
#include <cntrl/switches.h>
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

struct Board : TestPatchHardware {
    bool blocked = false, online = true;
    unsigned sent = 0, last = 255;
    bool supports(const char *) const override { return true; }
    unsigned switch_capacity() const override { return 5; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    bool available(const char *) const override { return online; }
    bool send(const char *, const uint8_t *bytes, unsigned length) override {
        ++sent; last = bytes[length - 1]; return true;
    }
    cntrl::midi::Bridge::Delivery patch_delivery(unsigned) const override {
        return blocked ? cntrl::midi::Bridge::Delivery::pending : cntrl::midi::Bridge::Delivery::complete;
    }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
};

int main(int argc, char **argv) {
    assert(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
    Board board; cntrl::Document document;
    assert(document.load(bytes.data(), unsigned(bytes.size()), board));
    unsigned root = unsigned(document.named_patch("delayed"));
    cntrl::PatchExecutor delayed(board, board, board, 0), other(board, board, board, 1);
    assert(delayed.start(document, root));
    board.blocked = true;
    delayed.tick(10); delayed.tick(500);
    assert(board.sent == 1 && !delayed.delaying());
    board.blocked = false; delayed.tick(501);
    assert(delayed.delaying());
    delayed.tick(600); assert(board.sent == 1);
    assert(other.start(document, unsigned(document.named_patch("independent"))));
    other.tick(600); other.tick(600); assert(board.sent == 2);
    delayed.tick(601); delayed.tick(601);
    assert(board.sent == 3 && delayed.status() == cntrl::PatchExecutor::Status::completed);
    assert(delayed.start(document, root));
    delayed.tick(UINT32_MAX - 50); delayed.tick(UINT32_MAX - 50);
    delayed.tick(48); assert(board.sent == 4);
    delayed.tick(49); assert(board.sent == 5); delayed.cancel();
    assert(delayed.start(document, root));
    delayed.tick(1); delayed.tick(1); delayed.cancel();
    unsigned before = board.sent; delayed.tick(200);
    assert(board.sent == before && !delayed.busy());
    cntrl::Switches switches(board, board, board);
    switches.enter(document, 0); switches.button(0, true);
    switches.tick(0); switches.tick(0); switches.tick(0);
    switches.button(0, false);
    switches.tick(1); switches.tick(1); switches.tick(1);
    assert(board.last == 0);
    before = board.sent; switches.tick(200);
    assert(board.sent == before); switches.close(); switches.tick(200);
    assert(!switches.open());
    switches.enter(document, 0); switches.button(0, true);
    switches.tick(0); switches.tick(0); switches.tick(0);
    before = board.sent; board.online = false;
    switches.tick(1); switches.close(); switches.tick(1);
    assert(!switches.open() && switches.abandoned());
    board.online = true; switches.tick(200);
    assert(board.sent == before); // Reconnection does not replay a delayed tail.
}
