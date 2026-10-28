#include <cntrl/controller.h>
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>
struct Board : cntrl::Hardware {
    unsigned switches = 0, sends = 0, errors = 0;
    bool available = true;
    bool send(const char *, const uint8_t *, unsigned) override { ++sends; return true; }
    bool supports(const char *) const override { return true; }
    bool supports_function(unsigned function) const override { return function == 1; }
    bool stock_firmware(uint8_t pair, const cntrl::SongContinuation * = nullptr) override { assert(pair == 33); ++switches; return available; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override { ++errors; }
};
int main(int argc, char **argv) {
    assert(argc == 2); std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    Board board; cntrl::Document document;
    assert(document.load(bytes.data(), unsigned(bytes.size()), board));
    cntrl::Controller controller(board); controller.activate(document);
    controller.action(0); controller.action(1); controller.action(5);
    assert(!board.switches && !board.sends);
    controller.action(2); assert(board.switches == 1 && !board.sends && !controller.depth());
    controller.action(5); assert(board.switches == 1);
    board.available = false; controller.action(2); assert(board.errors == 1 && !controller.depth());
}
