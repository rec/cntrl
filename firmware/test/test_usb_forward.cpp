#include <cntrl/usb_midi_forward.h>
#include <cntrl/midi_bridge.h>
#include <cassert>
#include <string>
#include <vector>

struct Connection {
    std::vector<std::string> calls;
    unsigned clears = 0, stops = 0;
    void clear_device() { ++clears; }
    void stop_forwarding() { ++stops; }
    void detach() { calls.push_back("detach"); }
    void publish(const char *name) { calls.push_back(name ? name : "absent"); }
    void attach() { calls.push_back("attach"); }
};
static void hotplug_defers_transfers_and_publishes_latest_identity() {
    cntrl::midi::UsbMidiForward forwarding; Connection connection;
    forwarding.poll(true, false, 0, "", 0, false, connection);
    forwarding.poll(true, true, 1, "Fancy Synth", 1, true, connection);
    assert(connection.calls.empty());
    assert(connection.clears == 1);
    forwarding.poll(true, true, 1, "Fancy Synth", 2, false, connection);
    assert(connection.calls.empty());
    forwarding.poll(true, true, 1, "Fancy Synth", 12, false, connection);
    assert(connection.calls == std::vector<std::string>{"detach"});
    forwarding.poll(true, true, 1, "Fancy Synth", 261, false, connection);
    assert(!forwarding.active(1) && connection.calls.size() == 1);
    forwarding.poll(true, true, 2, "Other Synth", 262, false, connection);
    assert(forwarding.active(2) && !forwarding.active(3));
    assert(connection.clears == 2);
    assert(connection.calls == std::vector<std::string>({"detach", "Other Synth", "attach"}));
    forwarding.poll(true, true, 2, "Other Synth", 300, false, connection);
    assert(connection.calls.size() == 3);
    forwarding.poll(false, true, 2, "Other Synth", 301, false, connection);
    assert(!forwarding.active(2));
    assert(connection.stops == 1);
    forwarding.poll(false, true, 2, "Other Synth", 551, false, connection);
    assert(!forwarding.active(2) && connection.calls[4] == "absent");
    forwarding.poll(true, true, 3, "Fancy Synth", 0xfffffff0, false, connection);
    forwarding.poll(true, false, 3, "", 234, false, connection);
    assert(!forwarding.active(3) && connection.calls[7] == "absent");
}

static void descriptor_adds_named_cables_and_preserves_other_interfaces() {
    // A MIDI streaming interface followed by a different composite function.
    std::vector<uint8_t> source = {
        9, 2, 92, 0, 2, 1, 0, 0x80, 50,
        9, 4, 0, 0, 2, 1, 3, 0, 0,
        7, 0x24, 1, 0, 1, 37, 0,
        6, 0x24, 2, 1, 1, 5,
        6, 0x24, 2, 2, 2, 5,
        9, 0x24, 3, 1, 3, 1, 2, 1, 5,
        9, 0x24, 3, 2, 4, 1, 1, 1, 5,
        9, 5, 3, 2, 0, 2, 0, 0, 0,
        5, 0x25, 1, 1, 1,
        9, 5, 0x83, 2, 0, 2, 0, 0, 0,
        5, 0x25, 1, 1, 3,
        9, 4, 1, 0, 0, 3, 0, 0, 0,
    };
    assert(source.size() == 92);
    uint8_t output[124];
    unsigned size = cntrl::midi::append_usb_midi_port(source.data(), unsigned(source.size()), output, sizeof(output), 11);
    assert(size == 124 && output[2] == 124);
    assert(!std::memcmp(output + size - 9, source.data() + source.size() - 9, 9));
    assert(output[23] == 67); // Class-specific interface length includes new jacks.
    unsigned associations = 0, named = 0;
    for (unsigned i = 9; i < size; i += output[i]) {
        assert(output[i] >= 2 && i + output[i] <= size);
        if (output[i + 1] == 0x24 && (output[i + 2] == 2 || output[i + 2] == 3))
            if (output[i + output[i] - 1] == 11) ++named;
        if (output[i + 1] == 0x25) {
            assert(output[i] == 6 && output[i + 3] == 2);
            assert(output[i + 4] == (associations ? 3 : 1));
            assert(output[i + 5] == (associations ? 7 : 5)); ++associations;
        }
    }
    assert(named == 4 && associations == 2);
    assert(!cntrl::midi::append_usb_midi_port(source.data(), unsigned(source.size()), output, sizeof(output) - 1, 11));
    source[9] = 255;
    assert(!cntrl::midi::append_usb_midi_port(source.data(), unsigned(source.size()), output, sizeof(output), 11));
}

struct ForwardSink : cntrl::midi::Sink {
    std::vector<uint32_t> messages;
    bool blocked = false;
    bool write_packet(unsigned route, const cntrl::midi::Packet &packet) override {
        if (blocked) return false;
        messages.push_back(packet.packed(route)); return true;
    }
};
static void relay_is_bidirectional_without_enabling_the_physical_interface() {
    using namespace cntrl::midi;
    const OutputConnection ports[] = {
        {"computer", "Computer", 0, OutputTransport::usb_device},
        {"synth", "Synth", 1, OutputTransport::usb_host},
    };
    Bridge bridge({ports, 2}); ForwardSink sink;
    bridge.configure(false); bridge.connected(true, 0); bridge.host_connected(true);
    bridge.relay_packet(1, {9, {0x90, 60, 100}}, 0);
    bridge.relay_packet(0, {4, {0xf0, 0x7d, 1}}, 0);
    bridge.relay_packet(0, {15, {0xf8, 0, 0}}, 0);
    bridge.relay_packet(0, {6, {2, 0xf7, 0}}, 0);
    bridge.configure(true); bridge.configure(false);
    sink.blocked = true; bridge.service(sink, 0); assert(sink.messages.empty());
    sink.blocked = false;
    for (unsigned i = 0; i < 4; ++i) bridge.service(sink, i);
    assert(sink.messages.size() == 4);
    std::vector<uint32_t> computer, synth;
    for (auto message : sink.messages) ((message & 0xf0) ? synth : computer).push_back(message);
    assert(synth == std::vector<uint32_t>{Packet({9, {0x90, 60, 100}}).packed(1)});
    assert(computer == std::vector<uint32_t>({Packet({4, {0xf0, 0x7d, 1}}).packed(),
        Packet({15, {0xf8, 0, 0}}).packed(), Packet({6, {2, 0xf7, 0}}).packed()}));
    bridge.relay_packet(1, {9, {0x90, 61, 100}}, 5);
    bridge.host_connected(false); bridge.service(sink, 6);
    bridge.host_connected(true); bridge.service(sink, 7);
    assert(sink.messages.size() == 4); // No queued note reaches a replacement.
    bridge.configure_usb(false, true, 8);
    bridge.relay_packet(0, {9, {0x90, 61, 100}}, 9); bridge.service(sink, 9);
    assert(sink.messages.size() == 4);
    bridge.relay_packet(1, {9, {0x90, 61, 100}}, 10); bridge.service(sink, 10);
    assert(sink.messages.size() == 4); // A disconnected computer cannot originate traffic.
    bridge.configure_usb(true, true, 11);
    bridge.relay_packet(1, {9, {0x90, 62, 100}}, 11);
    const uint8_t explicit_note[] = {0x90, 63, 100};
    assert(bridge.pedal(1, explicit_note, sizeof(explicit_note)));
    bridge.discard_forwarded(1);
    bridge.service(sink, 12);
    assert(sink.messages.size() == 5 && sink.messages.back() == Packet({9, {0x90, 63, 100}}).packed(1));
}
int main() {
    hotplug_defers_transfers_and_publishes_latest_identity();
    descriptor_adds_named_cables_and_preserves_other_interfaces();
    relay_is_bidirectional_without_enabling_the_physical_interface();
}
