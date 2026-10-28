#include <cntrl/midi_interface.h>
#include <cntrl/midi_bridge.h>
#include <cassert>
#include <cstring>

struct Hardware : cntrl::Hardware, cntrl::MidiInterfaceControl {
    cntrl::Pane panes[3];
    cntrl::midi::Bridge bridge;
    bool saved = true;
    unsigned renders = 0, sends = 0, restores = 0;
    void configure_interface(bool enabled) override { bridge.configure(enabled); }
    void restore_interface() override { ++restores; bridge.configure(saved); }
    bool send(const char *, const uint8_t *, unsigned) override { ++sends; return true; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned i, const cntrl::Pane &pane) override { panes[i] = pane; ++renders; }
    void set_brightness(uint8_t, bool) override { assert(false); }
    void set_splash(bool, uint32_t) override { assert(false); }
    void error(const char *) override { assert(false); }
    bool lit(unsigned port) const { return panes[1].lines[3 + port].text[0] == '*'; }
};

struct Output : cntrl::midi::Sink {
    bool blocked = true;
    unsigned accepted = 0;
    bool submit(unsigned, const cntrl::midi::Packet &) override {
        if (blocked) return false;
        ++accepted; return true;
    }
};

void test_lifecycle_and_host_status() {
    Hardware hw; cntrl::MidiInterface ui(hw, hw);
    hw.bridge.configure(hw.saved);
    ui.enter(true);
    assert(ui.open() && !ui.enabled() && !hw.bridge.enabled());
    assert(!std::strcmp(hw.panes[1].lines[2].text, "Off"));
    assert(!hw.panes[1].lines[1].text[0]);
    assert(!std::strcmp(hw.panes[2].lines[0].text, "Back"));
    assert(hw.panes[2].lines[0].x == 108 && hw.panes[2].lines[0].y == 4);
    assert(hw.panes[0].graphics.background == 0);
    for (auto &line : hw.panes[0].lines) assert(!line.text[0]);
    const char *const labels[] = {"DIN in", "TRS in", "DIN out", "TRS out"};
    for (unsigned i = 0; i < 4; ++i) {
        assert(!std::strcmp(hw.panes[1].lines[7 + i].text, labels[i]));
        assert(!hw.lit(i));
        assert(hw.panes[1].lines[3 + i].y == int(60 + (i / 2) * 20));
    }
    unsigned renders = hw.renders;
    ui.connected(false); ui.tick(0);
    assert(hw.renders == renders); // Connection changes are hidden while Off.
    ui.toggle(); ui.tick(1);
    assert(ui.enabled() && hw.bridge.enabled());
    assert(!std::strcmp(hw.panes[1].lines[1].text, "No host"));
    ui.connected(true); ui.tick(2);
    assert(!std::strcmp(hw.panes[1].lines[1].text, "Host connected"));
    renders = hw.renders; ui.connected(true); ui.tick(3);
    assert(hw.renders == renders && !hw.sends);
    ui.leave(); assert(!ui.open() && hw.bridge.enabled() && hw.saved);
    assert(hw.restores == 1);
    ui.enter(true); assert(!ui.enabled() && !hw.bridge.enabled());
    hw.saved = false; ui.leave(); assert(!hw.bridge.enabled());
}

void test_independent_lights_and_timing() {
    Hardware hw; cntrl::MidiInterface ui(hw, hw);
    ui.enter(true); ui.toggle(); ui.tick(0);
    for (unsigned i = 0; i < 4; ++i) {
        ui.activity(i % 2, i >= 2, i * 10); ui.tick(i * 10);
        assert(hw.lit(i));
    }
    unsigned renders = hw.renders;
    ui.activity(0, false, 50); ui.tick(50);
    assert(hw.renders == renders); // Refreshing an already lit indicator is invisible.
    ui.tick(110); assert(hw.lit(0) && !hw.lit(1) && hw.lit(2) && hw.lit(3));
    ui.tick(130); assert(hw.lit(0) && !hw.lit(2) && !hw.lit(3));
    ui.tick(150); assert(!hw.lit(0));
    ui.activity(0, false, 0xffffffd0u);
    ui.tick(51); assert(hw.lit(0));
    ui.tick(52); assert(!hw.lit(0));
    ui.activity(1, true, 53); ui.tick(53);
    ui.connected(false); ui.tick(54);
    for (unsigned i = 0; i < 4; ++i) assert(!hw.lit(i));
    ui.connected(true); ui.tick(55); assert(!hw.lit(3));
    ui.activity(0, true, 56); ui.toggle(); ui.tick(56);
    for (unsigned i = 0; i < 4; ++i) assert(!hw.lit(i));
    ui.activity(0, false, 57); ui.tick(57);
    assert(!hw.lit(0));
    ui.leave(); renders = hw.renders; ui.tick(1000);
    assert(hw.renders == renders);
}

void test_physical_input_activity_without_usb_delivery() {
    cntrl::midi::Bridge bridge; Output output;
    bridge.configure(true); bridge.connected(true);
    assert(!bridge.receive(6, 0x90, 0));
    assert(!bridge.receive(6, 60, 0));
    assert(bridge.receive(6, 100, 0));
    for (unsigned i = 0; i < bridge.capacity; ++i) {
        assert(!bridge.receive(6, 61, i));
        assert(bridge.receive(6, 100, i));
    }
    assert(bridge.counters(6).dropped == 1);
    bridge.service(output, 0); assert(!output.accepted);
    // A disconnected host still permits physical parsing without queuing data.
    bridge.connected(false);
    assert(bridge.receive(7, 0xf8, 100));
    assert(!bridge.receive(7, 0xf0, 100));
    assert(!bridge.receive(7, 1, 100));
    assert(bridge.receive(7, 2, 100));
    assert(bridge.receive(7, 0xf7, 100));
    bridge.connected(true); output.blocked = false;
    bridge.service(output, 101); assert(!output.accepted);
    bridge.configure(false);
    assert(!bridge.receive(7, 0xf8, 102));
}

int main() {
    test_lifecycle_and_host_status();
    test_independent_lights_and_timing();
    test_physical_input_activity_without_usb_delivery();
}
