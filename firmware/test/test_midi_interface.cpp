#include <cntrl/midi_interface.h>
#include <cntrl/midi_bridge.h>
#include <cassert>
#include <cstring>

struct Hardware : cntrl::MidiInterfaceControl {
    cntrl::midi::Bridge bridge;
    bool saved = true;
    unsigned configures = 0, restores = 0;
    void configure_interface(bool enabled) override { ++configures; bridge.configure(enabled); }
    void restore_interface() override { ++restores; bridge.configure(saved); }
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
    Hardware hw; cntrl::MidiInterface ui(hw);
    hw.bridge.configure(hw.saved);
    ui.enter(true, hw.saved);
    assert(ui.open() && ui.enabled() && hw.bridge.enabled());
    assert(!hw.configures && !hw.restores);
    assert(ui.status().connected && ui.take_redraw() && !ui.take_redraw());
    ui.leave(); assert(!hw.restores && hw.bridge.enabled());
    ui.enter(true, hw.saved);
    ui.toggle(); assert(!ui.enabled() && !hw.bridge.enabled());
    ui.take_redraw(); ui.connected(false);
    assert(!ui.status().connected && ui.take_redraw());
    ui.connected(false); assert(!ui.take_redraw());
    ui.leave(); assert(!ui.open() && hw.bridge.enabled() && hw.restores == 1);
    hw.saved = false; hw.bridge.configure(false);
    ui.enter(true, false); assert(!ui.enabled() && !hw.bridge.enabled());
    ui.toggle(); assert(ui.enabled() && hw.bridge.enabled());
    ui.leave(); assert(!hw.bridge.enabled() && hw.restores == 2);
}

void test_switch_requires_host_and_preserves_setting_on_reconnect() {
    Hardware hw; cntrl::MidiInterface ui(hw);
    for (bool enabled : {false, true}) {
        hw.bridge.configure(enabled);
        ui.enter(false, enabled); ui.take_redraw();
        unsigned before = hw.configures;
        ui.toggle();
        assert(ui.enabled() == enabled && hw.configures == before && !ui.take_redraw());
        ui.activity(0, false, 10);
        assert(!ui.status().lit[0]);
        ui.connected(true);
        assert(ui.enabled() == enabled && ui.take_redraw());
        ui.toggle();
        assert(ui.enabled() != enabled && hw.configures == before + 1);
        ui.connected(false); ui.take_redraw();
        ui.toggle();
        assert(ui.enabled() != enabled && hw.configures == before + 1 && !ui.take_redraw());
        ui.connected(true);
        assert(ui.enabled() != enabled);
        ui.leave();
    }
}

void test_independent_lights_and_timing() {
    Hardware hw; cntrl::MidiInterface ui(hw);
    ui.enter(true, true); ui.take_redraw();
    for (unsigned i = 0; i < 4; ++i) {
        ui.activity(i % 2, i >= 2, i * 10); ui.tick(i * 10);
        assert(ui.status().lit[i] && ui.take_redraw());
    }
    ui.activity(0, false, 50); ui.tick(50);
    assert(!ui.take_redraw());
    ui.tick(110); assert(ui.status().lit[0] && !ui.status().lit[1] && ui.status().lit[2] && ui.status().lit[3]);
    ui.tick(130); assert(ui.status().lit[0] && !ui.status().lit[2] && !ui.status().lit[3]);
    ui.tick(150); assert(!ui.status().lit[0]);
    ui.activity(0, false, 0xffffffd0u);
    ui.tick(51); assert(ui.status().lit[0]);
    ui.tick(52); assert(!ui.status().lit[0]);
    ui.activity(1, true, 53); ui.connected(false); ui.tick(54);
    for (bool lit : ui.status().lit) assert(!lit);
    ui.connected(true); ui.tick(55); assert(!ui.status().lit[3]);
    ui.activity(0, true, 56); ui.toggle(); ui.tick(56);
    for (bool lit : ui.status().lit) assert(!lit);
    ui.activity(0, false, 57); ui.tick(57); assert(!ui.status().lit[0]);
    ui.leave(); ui.take_redraw(); ui.tick(1000); assert(!ui.take_redraw());
}

void test_physical_input_activity_without_usb_delivery() {
    cntrl::midi::Bridge bridge; Output output;
    bridge.configure(true); bridge.connected(true, 0);
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
    bridge.connected(false, 0);
    assert(bridge.receive(7, 0xf8, 100));
    assert(!bridge.receive(7, 0xf0, 100));
    assert(!bridge.receive(7, 1, 100));
    assert(bridge.receive(7, 2, 100));
    assert(bridge.receive(7, 0xf7, 100));
    bridge.connected(true, 0); output.blocked = false;
    bridge.service(output, 101); assert(!output.accepted);
    bridge.configure(false);
    assert(!bridge.receive(7, 0xf8, 102));
}

int main() {
    test_lifecycle_and_host_status();
    test_switch_requires_host_and_preserves_setting_on_reconnect();
    test_independent_lights_and_timing();
    test_physical_input_activity_without_usb_delivery();
}
