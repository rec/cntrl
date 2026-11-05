#include <cntrl/panic.h>
#include <array>
#include <cassert>
#include <cstring>
#include <vector>

struct Hardware : cntrl::Hardware, cntrl::PanicOutputs {
    std::vector<std::array<uint8_t, 3>> messages[4];
    std::vector<std::pair<uint8_t, bool>> brightness;
    bool connected[4] = {true, true, true, true}, blocked[4] = {};
    unsigned attempts = 0;
    static unsigned index(const char *output) {
        const char *names[] = {"din", "trs", "usb-device", "usb-host"};
        for (unsigned i = 0; i < 4; ++i) if (!std::strcmp(output, names[i])) return i;
        assert(false); return 0;
    }
    bool available(const char *output) const override { return std::strncmp(output, "configurable-", 13) && connected[index(output)]; }
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
    void show(unsigned, const cntrl::Pane &) override { assert(false); }
    void set_brightness(uint8_t value, bool temporary = false) override { brightness.emplace_back(value, temporary); }
    void set_splash(bool, uint32_t) override { assert(false); }
    void error(const char *) override { assert(false); }
};

void tick(cntrl::Panic &panic, Hardware &hw, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        unsigned before = hw.attempts; panic.tick(0, true);
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

void test_tap_without_display_side_effects() {
    Hardware hw; cntrl::Panic panic(hw, hw);
    panic.enter(77); assert(panic.open() && !panic.active());
    assert(hw.brightness.empty());
    assert(panic.take_redraw() && !panic.take_redraw());
    panic.button(true, 0); panic.button(true, 0);
    assert(hw.brightness.size() == 1 && hw.brightness[0].first == 255 && hw.brightness[0].second);
    panic.button(false, 0);
    assert(hw.brightness.back().first == 77 && !hw.brightness.back().second);
    tick(panic, hw, 1000); assert(!panic.active());
    for (const auto &messages : hw.messages) assert(messages.size() == 48);
    check_controllers(hw);
}

void test_hold_full_loop_and_release() {
    Hardware hw; cntrl::Panic panic(hw, hw);
    panic.enter(192); panic.button(true, 0);
    tick(panic, hw, 4 * 48);
    check_controllers(hw);
    for (const auto &messages : hw.messages) assert(messages.size() == 48);
    tick(panic, hw, 4 * 16 * 128);
    for (const auto &messages : hw.messages)
        for (unsigned channel = 0; channel < 16; ++channel)
            for (unsigned note = 0; note < 128; ++note)
                assert((messages[48 + channel * 128 + note] == std::array<uint8_t, 3>{uint8_t(0x80 + channel), uint8_t(note), 0}));
    tick(panic, hw, 4 * 48); check_controllers(hw, 48 + 2048);
    panic.button(false, 0); tick(panic, hw, 100);
    for (const auto &messages : hw.messages) assert(messages.size() == 48 + 2048 + 48);
    assert(!panic.active() && hw.brightness.back().first == 192);
    panic.button(true, 0); tick(panic, hw, 4 * 48 + 4);
    panic.button(false, 0);
    unsigned before = hw.attempts; tick(panic, hw, 100); assert(hw.attempts == before);
    panic.button(true, 0); panic.leave();
    assert(!panic.open() && !panic.active() && hw.brightness.back().first == 192);
}

void test_backpressure_and_disconnection() {
    Hardware hw; cntrl::Panic panic(hw, hw);
    hw.blocked[0] = true; hw.connected[3] = false;
    panic.enter(100); panic.button(true, 0); panic.button(false, 0);
    tick(panic, hw, 1000);
    assert(panic.active() && hw.messages[0].empty() && hw.messages[3].empty());
    assert(hw.messages[1].size() == 48 && hw.messages[2].size() == 48);
    hw.blocked[0] = false;
    tick(panic, hw, 1000); assert(!panic.active() && hw.messages[0].size() == 48);
    panic.button(true, 0); tick(panic, hw, 1); panic.leave();
    unsigned before = hw.attempts; tick(panic, hw, 100); assert(hw.attempts == before);
    panic.enter(50); assert(!panic.active());
}

void test_blocked_panic_finishes_one_second_after_release() {
    Hardware hw; cntrl::Panic panic(hw, hw);
    hw.blocked[2] = true;
    panic.enter(77); panic.button(true, 100);
    tick(panic, hw, 1000);
    panic.tick(10000, true); assert(panic.active());
    panic.button(false, 10000);
    panic.tick(10999, true); assert(panic.status() == cntrl::Panic::Status::sending);
    panic.take_redraw();
    panic.tick(11000, true);
    assert(!panic.active() && panic.status() == cntrl::Panic::Status::incomplete && panic.take_redraw());
    for (unsigned i : {0u, 1u, 3u}) assert(hw.messages[i].size() == 48);
    assert(hw.brightness.back().first == 77);
    hw.blocked[2] = false;
    panic.button(true, 12000); panic.button(false, 12001);
    for (unsigned i = 0; i < 1000; ++i) panic.tick(12002, true);
    assert(panic.status() == cntrl::Panic::Status::ready);
    panic.button(true, 0xfffffff0); panic.button(false, 0xfffffff0);
    panic.tick(984, false);
    assert(panic.status() == cntrl::Panic::Status::incomplete);
}

int main() { test_tap_without_display_side_effects(); test_hold_full_loop_and_release(); test_backpressure_and_disconnection(); test_blocked_panic_finishes_one_second_after_release(); }
