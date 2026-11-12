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

void test_triples(Board &board, const cntrl::Document &document) {
    cntrl::Switches switches(board, board, board);
    switches.enter(document, 0);
    const bool held = cntrl::u16(document.switch_gestures(0) + 20) != 65535;
    auto tick = [&](uint32_t now) { for (unsigned i = 0; i < 10; ++i) switches.tick(now); };
    auto edge = [&](bool down, uint32_t now) { switches.button(0, down, now); tick(now); };
    auto first_two = [&](uint32_t base) {
        board.sent.clear(); edge(true, base); edge(false, base + 10);
        edge(true, base + 100); edge(false, base + 110);
        assert(board.sent.empty());
    };
    first_two(100); edge(true, 300);
    assert(board.sent == (held ? std::vector<unsigned>{} : std::vector<unsigned>{9}));
    edge(false, 350); tick(1000); assert((board.sent == std::vector<unsigned>{9, 10}));
    first_two(1100); tick(1509); assert(board.sent.empty());
    tick(1510); assert((board.sent == std::vector<unsigned>{4, 5}));
    board.sent.clear(); edge(true, 1600); edge(false, 1610); tick(1909);
    assert(board.sent.empty()); tick(1910); assert((board.sent == std::vector<unsigned>{0, 1}));
    // At the third-tap deadline, finish the double and start a fresh sequence.
    first_two(2100); edge(true, 2510); assert((board.sent == std::vector<unsigned>{4, 5}));
    edge(false, 2520); tick(2820); assert((board.sent == std::vector<unsigned>{4, 5, 0, 1}));
    first_two(3000); edge(true, 3200);
    switches.button(0, false, 3699); tick(4000);
    assert((board.sent == std::vector<unsigned>{9, 10}));
    first_two(4100); edge(true, 4300); tick(4799);
    assert(board.sent == (held ? std::vector<unsigned>{} : std::vector<unsigned>{9}));
    tick(4800); tick(4850);
    assert(board.sent == (held ? std::vector<unsigned>{11} : std::vector<unsigned>{9}));
    edge(false, 4900);
    assert(board.sent == (held ? std::vector<unsigned>{11, 12} : std::vector<unsigned>{9, 10}));
    // Holding the second press still selects the long double, excluding triples.
    board.sent.clear(); edge(true, 5000); edge(false, 5010); edge(true, 5100);
    tick(5600); edge(false, 5610); tick(6000);
    assert((board.sent == std::vector<unsigned>{7, 8}));
    first_two(6100); switches.close(); tick(6300); assert(board.sent.empty());
    switches.enter(document, 0);
    first_two(7100); edge(true, 7300); switches.close(); tick(7350);
    assert(board.sent == (held ? std::vector<unsigned>{} : std::vector<unsigned>{9, 10}));
    switches.enter(document, 0);
    first_two(8100); edge(true, 8300); tick(8800); switches.close(); tick(8900);
    assert(board.sent == (held ? std::vector<unsigned>{11, 12} : std::vector<unsigned>{9, 10}));

    uint8_t settings[cntrl::gesture_record_size];
    std::memcpy(settings, document.switch_gestures(0), sizeof(settings));
    cntrl::SwitchGestures gesture; std::vector<unsigned> events;
    auto recognize = [&](uint32_t now) { gesture.tick(now, true, [&](unsigned event) { events.push_back(event); }); };
    auto triple_press = [&]() {
        gesture.reset(settings); events.clear();
        for (unsigned i = 0; i < 3; ++i) {
            uint32_t now = 0xffffff00u + i * 100;
            gesture.input(true, now); recognize(now);
            if (i < 2) { gesture.input(false, now + 10); recognize(now + 10); }
        }
    };
    triple_press(); recognize(443);
    assert(events == (held ? std::vector<unsigned>{} : std::vector<unsigned>{9}));
    gesture.input(false, 444); recognize(444);
    assert(events == (held ? std::vector<unsigned>{11, 12} : std::vector<unsigned>{9, 10}));
    if (held) {
        settings[16] = settings[17] = settings[18] = settings[19] = 255;
        triple_press(); recognize(444); gesture.input(false, 445); recognize(445);
        assert((events == std::vector<unsigned>{11, 12}));
    }
}

int main(int argc, char **argv) {
    cntrl::memory_budget = 192 * 1024;
    assert(argc == 3 || argc == 4);
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    Board board; cntrl::Document document;
    assert(document.load(bytes.data(), unsigned(bytes.size()), board));
    if (argc == 3) { test_triples(board, document); return 0; }
    const bool long_double = argv[3][0] == '1';
    assert(document.fast_switches() == (argv[2][0] == '1'));
    cntrl::Switches switches(board, board, board);
    auto tick = [&](unsigned now) { for (unsigned i = 0; i < 10; ++i) switches.tick(now); };
    auto edge = [&](bool down, unsigned now) { switches.button(0, down, now + 20); tick(now + 20); };
    switches.enter(document, 0);
    assert(switches.debounce_ms(0) == 20 && !switches.debounce_ms(5));
    edge(true, 100); edge(false, 150); tick(469); assert(board.sent.empty());
    tick(470); assert((board.sent == std::vector<unsigned>{0, 1}));
    board.sent.clear(); edge(true, 1000); edge(false, 1050); edge(true, 1200);
    assert(board.sent == (long_double ? std::vector<unsigned>{} : std::vector<unsigned>{4}));
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
    if (long_double) {
        // A held second press selects only its long-double action and release.
        board.sent.clear(); edge(true, 1400000); edge(false, 1400050); edge(true, 1400200);
        tick(1400719); assert(board.sent.empty());
        tick(1400720); tick(1400900); assert((board.sent == std::vector<unsigned>{7}));
        edge(false, 1401000); assert((board.sent == std::vector<unsigned>{7, 8}));
        // Closing an undecided second press emits nothing.
        board.sent.clear(); edge(true, 1500000); edge(false, 1500050); edge(true, 1500200);
        switches.close(); tick(1500300); assert(board.sent.empty() && !switches.open());
        // A recognized held second press is paired when its context closes.
        switches.enter(document, 0);
        edge(true, 1600000); edge(false, 1600050); edge(true, 1600200); tick(1600720);
        switches.close(); tick(1600800); assert((board.sent == std::vector<unsigned>{7, 8}));
    }
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
    uint8_t settings[cntrl::gesture_record_size]; std::memcpy(settings, document.switch_gestures(0), sizeof(settings));
    // Baseline gestures retain their original recognition times.
    settings[12] = settings[13] = settings[14] = settings[15] = 255;
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
    for (unsigned i = 1; i < cntrl::gesture_event_count; ++i) settings[i * 2] = settings[i * 2 + 1] = 255;
    gesture.reset(settings); events.clear(); gesture.input(true, 20); recognize(20);
    assert((events == std::vector<unsigned>{0}));
    gesture.input(false, 70); recognize(70); assert(events.back() == 1);

    // Second-press thresholds use confirmed edge times, including wraparound.
    std::memcpy(settings, document.switch_gestures(0), sizeof(settings));
    settings[12] = 7; settings[13] = 0; settings[14] = 8; settings[15] = 0;
    auto double_press = [&](uint32_t base) {
        gesture.reset(settings); events.clear();
        gesture.input(true, base); recognize(base);
        gesture.input(false, base + 10); recognize(base + 10);
        gesture.input(true, base + 100); recognize(base + 100);
        assert(events.empty());
    };
    double_press(0); recognize(599); assert(events.empty());
    gesture.input(false, 600); recognize(600);
    assert((events == std::vector<unsigned>{7, 8}));
    double_press(1000); gesture.input(false, 1599); recognize(1900);
    assert((events == std::vector<unsigned>{4, 5}));
    double_press(2000); gesture.cancel([&](unsigned event) { events.push_back(event); });
    assert(events.empty());
    double_press(0xfffffff0u); recognize(583); assert(events.empty());
    recognize(584); recognize(1000); assert((events == std::vector<unsigned>{7}));
    gesture.cancel([&](unsigned event) { events.push_back(event); });
    assert((events == std::vector<unsigned>{7, 8}));
    // The long double gesture also works without an ordinary double action.
    settings[6] = settings[7] = 255;
    double_press(3000); recognize(3600);
    gesture.input(false, 3610); recognize(3610);
    assert((events == std::vector<unsigned>{7, 8}));
}
