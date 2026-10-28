#include <cntrl/panic.h>
#include <array>
#include <cassert>
#include <cstring>
#include <vector>

struct Hardware : cntrl::Hardware, cntrl::PanicOutputs {
    cntrl::Pane panes[3];
    std::vector<std::array<uint8_t, 3>> messages[4];
    std::vector<std::pair<uint8_t, bool>> brightness;
    bool connected[4] = {true, true, true, true}, blocked[4] = {};
    unsigned attempts = 0;
    static unsigned index(const char *output) {
        const char *names[] = {"din", "trs", "usb-device", "usb-host"};
        for (unsigned i = 0; i < 4; ++i) if (!std::strcmp(output, names[i])) return i;
        assert(false); return 0;
    }
    bool available(const char *output) const override { return connected[index(output)]; }
    bool transmit(const char *output, const uint8_t *bytes) override {
        ++attempts;
        if (blocked[index(output)]) return false;
        messages[index(output)].push_back({bytes[0], bytes[1], bytes[2]}); return true;
    }
    bool send(const char *, const uint8_t *, unsigned) override { assert(false); return false; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned i, const cntrl::Pane &view) override { panes[i] = view; }
    void set_brightness(uint8_t value, bool temporary = false) override { brightness.emplace_back(value, temporary); }
    void set_splash(bool, uint32_t) override { assert(false); }
    void error(const char *) override { assert(false); }
};

void tick(cntrl::Panic &panic, Hardware &hw, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        unsigned before = hw.attempts; panic.tick();
        assert(hw.attempts - before <= 1);
    }
}

void check_controllers(const Hardware &hw, unsigned offset = 0) {
    const uint8_t controllers[] = {123, 120, 121};
    for (const auto &messages : hw.messages)
        for (unsigned channel = 0; channel < 16; ++channel)
            for (unsigned controller = 0; controller < 3; ++controller)
                assert((messages[offset + channel * 3 + controller] == std::array<uint8_t, 3>{uint8_t(0xb0 + channel), controllers[controller], 0}));
}

void test_tap_and_page() {
    Hardware hw; cntrl::Panic panic(hw, hw);
    panic.enter(77); assert(panic.open() && !panic.active());
    assert(hw.brightness.empty());
    for (const auto &line : hw.panes[0].lines) assert(!line.text[0]);
    assert(!std::strcmp(hw.panes[1].lines[0].text, "Panic"));
    assert(hw.panes[1].lines[0].centered && hw.panes[1].lines[0].y == 104);
    for (unsigned i = 1; i < cntrl::max_lines; ++i) assert(!hw.panes[1].lines[i].text[0]);
    assert(!std::strcmp(hw.panes[2].lines[0].text, "Back"));
    assert(hw.panes[2].lines[0].y == 4 && hw.panes[2].lines[0].x == 108);
    for (const auto &pane : hw.panes) assert(pane.graphics.background == 0);
    panic.button(true); panic.button(true);
    assert(hw.brightness.size() == 1 && hw.brightness[0].first == 255 && hw.brightness[0].second);
    panic.button(false);
    assert(hw.brightness.back().first == 77 && !hw.brightness.back().second);
    tick(panic, hw, 1000); assert(!panic.active());
    for (const auto &messages : hw.messages) assert(messages.size() == 48);
    check_controllers(hw);
}

void test_hold_full_loop_and_release() {
    Hardware hw; cntrl::Panic panic(hw, hw);
    panic.enter(192); panic.button(true);
    tick(panic, hw, 4 * 48);
    check_controllers(hw);
    for (const auto &messages : hw.messages) assert(messages.size() == 48);
    tick(panic, hw, 4 * 16 * 128);
    for (const auto &messages : hw.messages)
        for (unsigned channel = 0; channel < 16; ++channel)
            for (unsigned note = 0; note < 128; ++note)
                assert((messages[48 + channel * 128 + note] == std::array<uint8_t, 3>{uint8_t(0x80 + channel), uint8_t(note), 0}));
    tick(panic, hw, 4 * 48); check_controllers(hw, 48 + 2048);
    panic.button(false); tick(panic, hw, 100);
    for (const auto &messages : hw.messages) assert(messages.size() == 48 + 2048 + 48);
    assert(!panic.active() && hw.brightness.back().first == 192);
    panic.button(true); tick(panic, hw, 4 * 48 + 4);
    panic.button(false);
    unsigned before = hw.attempts; tick(panic, hw, 100); assert(hw.attempts == before);
    panic.button(true); panic.leave();
    assert(!panic.open() && !panic.active() && hw.brightness.back().first == 192);
}

void test_backpressure_and_disconnection() {
    Hardware hw; cntrl::Panic panic(hw, hw);
    hw.blocked[0] = true; hw.connected[3] = false;
    panic.enter(100); panic.button(true); panic.button(false);
    tick(panic, hw, 1000);
    assert(panic.active() && hw.messages[0].empty() && hw.messages[3].empty());
    assert(hw.messages[1].size() == 48 && hw.messages[2].size() == 48);
    hw.blocked[0] = false;
    tick(panic, hw, 1000); assert(!panic.active() && hw.messages[0].size() == 48);
    panic.button(true); tick(panic, hw, 1); panic.leave();
    unsigned before = hw.attempts; tick(panic, hw, 100); assert(hw.attempts == before);
    panic.enter(50); assert(!panic.active());
}

int main() { test_tap_and_page(); test_hold_full_loop_and_release(); test_backpressure_and_disconnection(); }
