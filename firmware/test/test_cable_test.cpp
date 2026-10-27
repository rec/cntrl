#include <cntrl/cable_test.h>
#include <cntrl/module_menu.h>
#include <cassert>
#include <string>
#include <vector>

struct Hardware : cntrl::Hardware {
    struct Message { std::string route; std::vector<uint8_t> bytes; };
    std::vector<Message> messages;
    cntrl::Pane panes[3];
    bool available = true;
    bool send(const char *route, const uint8_t *bytes, unsigned n) override {
        if (!available) return false;
        messages.push_back({route, {bytes, bytes + n}}); return true;
    }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned i, const cntrl::Pane &pane) override { panes[i] = pane; }
    void set_brightness(uint8_t) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override { assert(false); }
};

void receive(cntrl::CableTest &test, unsigned port, uint8_t status, uint8_t value, uint32_t now) {
    for (uint8_t b : {status, uint8_t(2), value}) test.receive(port, b, now);
}

int main() {
    Hardware hw;
    cntrl::CableTest test(hw);
    test.enter(); test.tick(100);
    assert(!test.enabled() && hw.messages.empty());
    assert(!std::strcmp(hw.panes[0].lines[1].text, "Off"));
    assert(!hw.panes[1].lines[0].text[0] && !hw.panes[1].lines[1].text[0]);
    test.toggle(100); test.tick(100);
    assert(hw.messages.size() == 2);
    assert(hw.messages[0].route == "din" && hw.messages[0].bytes == std::vector<uint8_t>({0xb0, 2, 0}));
    assert(hw.messages[1].route == "trs" && hw.messages[1].bytes == std::vector<uint8_t>({0xb1, 2, 0}));
    test.tick(109); assert(hw.messages.size() == 2);
    test.tick(110); assert(hw.messages.size() == 4);
    receive(test, 0, 0xb0, 1, 111);
    receive(test, 1, 0xb1, 1, 111);
    assert(!std::strcmp(hw.panes[1].lines[0].text, "DIN"));
    assert(!std::strcmp(hw.panes[1].lines[1].text, "TRS"));
    receive(test, 1, 0xb0, 1, 112);
    receive(test, 0, 0xb1, 1, 112);
    assert(!std::strcmp(hw.panes[1].lines[0].text, "DIN to TRS"));
    assert(!std::strcmp(hw.panes[1].lines[1].text, "TRS to DIN"));
    // Running status and interleaved MIDI clock use the shared parser.
    for (uint8_t b : {2, 0xf8, 1}) test.receive(0, b, 120);
    test.tick(369);
    assert(!hw.panes[1].lines[0].text[0] && hw.panes[1].lines[1].text[0]);
    test.tick(370); assert(!hw.panes[1].lines[1].text[0]);
    receive(test, 0, 0xb0, 127, 371); // Never sent: no match.
    receive(test, 1, 0xb2, 1, 371); // Wrong channel.
    for (uint8_t b : {0xb0, 3, 1}) test.receive(0, b, 371); // Wrong controller.
    assert(!hw.panes[1].lines[0].text[0] && !hw.panes[1].lines[1].text[0]);
    test.toggle(372);
    auto count = hw.messages.size(); test.tick(1000);
    assert(hw.messages.size() == count);
    test.toggle(1000); hw.available = false; test.tick(1000);
    receive(test, 0, 0xb0, 0, 1001); // Failed send cannot pass.
    assert(!hw.panes[1].lines[0].text[0]);
    hw.available = true; test.tick(1010); receive(test, 0, 0xb0, 0, 1011);
    assert(hw.panes[1].lines[0].text[0]);
    test.leave(); count = hw.messages.size(); test.tick(2000);
    assert(!test.open() && hw.messages.size() == count);
    test.enter(); assert(!test.enabled() && !hw.panes[1].lines[0].text[0]);
    test.toggle(0xfffffffau); test.tick(0xfffffffau); test.tick(4);
    assert(hw.messages.size() == count + 4); // Timestamp wrap.

    const char *const names[] = {"Setups", "MIDI cable test"};
    cntrl::ModuleMenu menu(hw, names, 2);
    menu.enter(); assert(menu.button(2, true) == -1);
    menu.poll(0); menu.button(1, true);
    assert(menu.button(2, true) == 1 && !menu.open());
    menu.enter(); menu.poll(0);
    assert(menu.button(5, true) == -2 && !menu.open());
}
