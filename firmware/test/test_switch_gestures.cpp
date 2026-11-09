#include <cntrl/switches.h>
#include "patch_hardware.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

struct Board : TestPatchHardware {
    std::vector<unsigned> sent;
    bool writable = true;
    bool connected = true;
    unsigned errors = 0;
    unsigned switch_capacity() const override { return 5; }
    bool supports(const char *) const override { return true; }
    bool available(const char *) const override { return connected; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    bool send(const char *, const uint8_t *p, unsigned n) override {
        assert(n == 3); if (!writable) return false;
        sent.push_back(p[2]); return true;
    }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    unsigned display_count() const override { return 3; }
    const char *control_label(cntrl::ControlRole role) const override {
        const char *labels[] = {"A", "B", "C", "D", "E", "F"}; return labels[unsigned(role)];
    }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override { ++errors; }
};

int main(int argc, char **argv) {
    assert(argc == 3);
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    Board board; cntrl::Document document;
    assert(document.load(bytes.data(), unsigned(bytes.size()), board));
    assert(document.fast_switches() == (argv[2][0] == '1'));
    cntrl::Switches switches(board, board, board);
    auto tick = [&](unsigned now) { for (unsigned i = 0; i < 10; ++i) switches.tick(now); };
    auto edge = [&](bool down, unsigned now) { switches.button(0, down, now + 20); tick(now + 20); };
    switches.enter(document, 0);
    assert(switches.debounce_ms(0) == 20 && !switches.debounce_ms(5));
    edge(true, 100); edge(false, 150); tick(469); assert(board.sent.empty());
    tick(470); assert((board.sent == std::vector<unsigned>{0, 1}));
    board.sent.clear(); edge(true, 1000); edge(false, 1050); edge(true, 1200);
    assert((board.sent == std::vector<unsigned>{4}));
    edge(false, 1250); tick(2000);
    assert((board.sent == std::vector<unsigned>{4, 5}));
    // Exact hold threshold, paired release, and bounded repeats.
    board.sent.clear(); edge(true, 3000); tick(3519); assert(board.sent.empty());
    tick(3520); assert((board.sent == std::vector<unsigned>{2, 6}));
    tick(3620); assert(board.sent.back() == 6);
    board.writable = false;
    for (unsigned i = 0; i < 10000; ++i) tick(4000 + i * 100);
    unsigned before = unsigned(board.sent.size());
    edge(false, 1005000); board.writable = true; tick(1005020);
    assert(board.sent.size() <= before + 3 && board.sent.back() == 3);
    // Closing cancels the delayed single tap.
    board.sent.clear(); edge(true, 1100000); edge(false, 1100050);
    switches.close(); tick(1101000); assert(board.sent.empty() && !switches.open());
    // A held entry rearms only after release.
    switches.enter(document, 1); tick(1200000); assert(board.sent.empty());
    edge(false, 1200010); edge(true, 1200100); edge(false, 1200150); tick(1200470);
    assert((board.sent == std::vector<unsigned>{0, 1}));
    // Losing output while waiting cancels the pending tap permanently.
    board.sent.clear(); edge(true, 1300000); edge(false, 1300050);
    board.connected = false; tick(1300100); board.connected = true; tick(1301000);
    assert(board.sent.empty());
    edge(true, 1302000); edge(false, 1302050); tick(1302370);
    assert((board.sent == std::vector<unsigned>{0, 1}));
    // Extension node references must be validated by the native decoder.
    auto bad = bytes;
    unsigned address = unsigned(document.switch_gestures(0) - bytes.data());
    bad[address] = 0xfe; bad[address + 1] = 0xff;
    cntrl::put32(bad.data() + 20, cntrl::checksum(bad.data() + 28, unsigned(bad.size()) - 28));
    cntrl::Document rejected; assert(!rejected.load(bad.data(), unsigned(bad.size()), board));
    bad = bytes;
    for (unsigned flags = 0; flags < 4; ++flags) {
        bad[document.metadata() + cntrl::scrolling_offset + 5] = uint8_t(flags);
        cntrl::put32(bad.data() + 20, cntrl::checksum(bad.data() + 28, unsigned(bad.size()) - 28));
        assert(rejected.load(bad.data(), unsigned(bad.size()), board));
        assert(rejected.fast_switches() == bool(flags & 1));
        assert(rejected.forward_device_to_host() == !bool(flags & 2));
    }
    bad[document.metadata() + cntrl::scrolling_offset + 5] = 4;
    cntrl::put32(bad.data() + 20, cntrl::checksum(bad.data() + 28, unsigned(bad.size()) - 28));
    assert(!rejected.load(bad.data(), unsigned(bad.size()), board));
    assert(!board.errors);

    // Recognition alone covers exact boundaries and the 32-bit clock wrap.
    uint8_t settings[20]; std::memcpy(settings, document.switch_gestures(0), 20);
    cntrl::SwitchGestures gesture;
    std::vector<unsigned> events;
    auto recognize = [&](uint32_t now) { gesture.tick(now, true, [&](unsigned e) { events.push_back(e); }); };
    gesture.reset(settings); gesture.input(true, 20); recognize(20);
    gesture.input(false, 520); recognize(520);
    assert((events == std::vector<unsigned>{2, 6, 3}));
    gesture.reset(settings); events.clear();
    gesture.input(true, 1020); recognize(1020); gesture.input(false, 1070); recognize(1070);
    gesture.input(true, 1370); recognize(1370);
    assert((events == std::vector<unsigned>{0, 1})); // Exact double deadline.
    gesture.reset(settings); events.clear();
    gesture.input(true, 2000); recognize(2000);
    gesture.input(false, 2499); recognize(2600);
    assert(events.empty()); // A release before the hold deadline stays a tap.
    gesture.input(true, 2798); recognize(2900);
    assert((events == std::vector<unsigned>{4})); // Confirmed before the double deadline.
    gesture.reset(settings); events.clear();
    gesture.input(true, 0xfffffff0u); recognize(0xfffffff0u);
    recognize(484); assert((events == std::vector<unsigned>{2, 6}));
    // With release only, the press remains immediate after debouncing.
    for (unsigned i = 1; i < 6; ++i) settings[i * 2] = settings[i * 2 + 1] = 255;
    gesture.reset(settings); events.clear(); gesture.input(true, 20); recognize(20);
    assert((events == std::vector<unsigned>{0}));
    gesture.input(false, 70); recognize(70); assert(events.back() == 1);
}
