#include "midi_endpoints.h"
#include <cntrl/cable_test.h>
#include <cassert>
#include <string>
#include <vector>

struct Hardware : cntrl::midi::Sink {
    struct Message { std::string route; std::vector<uint8_t> bytes; };
    std::vector<Message> messages;
    bool available = true;
    bool write_packet(unsigned route, const cntrl::midi::Packet &packet) override {
        if (!available) return false;
        messages.push_back({test_midi_outputs.route(route)->name,
            {packet.bytes, packet.bytes + packet.size()}}); return true;
    }
};

void receive(cntrl::CableTest &test, unsigned port, uint8_t status, uint8_t value, uint32_t now) {
    for (uint8_t b : {status, uint8_t(2), value}) test.receive(port, b, now);
}

int main() {
    Hardware hw;
    cntrl::CableTest test(hw, test_cable_outputs, 2);
    test.enter(); test.tick(100);
    assert(!test.enabled() && hw.messages.empty());
    assert(!test.status().enabled && test.status().inputs[0] == -1 && test.status().inputs[1] == -1);
    assert(test.take_redraw() && !test.take_redraw());
    test.toggle(100); test.tick(100);
    assert(hw.messages.size() == 2);
    assert(test.status().sending[0] && test.status().sending[1]);
    assert(hw.messages[0].route == "din" && hw.messages[0].bytes == std::vector<uint8_t>({0xb0, 2, 0}));
    assert(hw.messages[1].route == "trs" && hw.messages[1].bytes == std::vector<uint8_t>({0xb1, 2, 0}));
    test.tick(109); assert(hw.messages.size() == 2);
    test.tick(110); assert(hw.messages.size() == 4);
    receive(test, 0, 0xb0, 1, 111);
    receive(test, 1, 0xb1, 1, 111);
    assert(test.status().inputs[0] == 0);
    assert(test.status().inputs[1] == 1);
    receive(test, 1, 0xb0, 1, 112);
    receive(test, 0, 0xb1, 1, 112);
    assert(test.status().inputs[1] == 0);
    assert(test.status().inputs[0] == 1);
    // Running status and interleaved MIDI clock use the shared parser.
    for (uint8_t b : {2, 0xf8, 1}) test.receive(0, b, 120);
    test.tick(369);
    assert(test.status().inputs[1] == -1 && test.status().inputs[0] == 1);
    test.tick(370); assert(test.status().inputs[0] == -1);
    receive(test, 0, 0xb0, 127, 371); // Never sent: no match.
    receive(test, 1, 0xb2, 1, 371); // Wrong channel.
    for (uint8_t b : {0xb0, 3, 1}) test.receive(0, b, 371); // Wrong controller.
    assert(test.status().inputs[0] == -1 && test.status().inputs[1] == -1);
    test.toggle(372);
    auto count = hw.messages.size(); test.tick(1000);
    assert(hw.messages.size() == count);
    test.toggle(1000); hw.available = false; test.tick(1000);
    receive(test, 0, 0xb0, 0, 1001); // Failed send cannot pass.
    assert(test.status().inputs[0] == -1);
    assert(!test.status().sending[0] && !test.status().sending[1]);
    hw.available = true; test.tick(1010); receive(test, 0, 0xb0, 0, 1011);
    assert(test.status().inputs[0] == 0);
    // One output can be received at both inputs independently.
    receive(test, 1, 0xb0, 0, 1012);
    assert(test.status().inputs[0] == 0 && test.status().inputs[1] == 0);
    hw.available = false; test.tick(1260);
    assert(!test.status().sending[0] && !test.status().sending[1]);
    test.tick(1261);
    assert(test.status().inputs[0] == -1 && test.status().inputs[1] == 0);
    test.tick(1262); assert(test.status().inputs[1] == -1);
    hw.available = true;
    test.leave(); count = hw.messages.size(); test.tick(2000);
    assert(!test.enabled() && hw.messages.size() == count);
    test.enter(); assert(!test.enabled() && test.status().inputs[0] == -1);
    test.toggle(0xfffffffau); test.tick(0xfffffffau); test.tick(4);
    assert(hw.messages.size() == count + 4); // Timestamp wrap.

}
