#include "midi_endpoints.h"
#include <cntrl/clock_generator.h>
#include <cntrl/midi_bridge.h>
#include <cntrl/midi_outputs.h>
#include <cassert>
#include <vector>
#include <iostream>

void forward(cntrl::midi::Bridge &bridge, uint32_t packed, uint32_t now) {
    unsigned route = (packed >> 4) & 15;
    const auto *connection = test_midi_outputs.route(route);
    if (!connection || connection->transport != cntrl::midi::OutputTransport::physical) return;
    bridge.forward_packet(route, {uint8_t(packed & 15), {uint8_t(packed >> 8), uint8_t(packed >> 16), uint8_t(packed >> 24)}}, now);
}

struct Output : cntrl::midi::Sink {
    std::vector<uint8_t> bytes[9];
    bool blocked[9] = {};
    unsigned attempts = 0;
    bool write_packet(unsigned route, const cntrl::midi::Packet &p) override {
        ++attempts;
        if (blocked[route]) return false;
        bytes[route].insert(bytes[route].end(), p.bytes, p.bytes + p.size()); return true;
    }
};

void input(cntrl::midi::Bridge &b, unsigned route, std::initializer_list<uint8_t> bytes, unsigned now = 0) {
    for (uint8_t x : bytes) b.receive(route, x, now);
}

void test_messages_and_running_status() {
    cntrl::midi::Bridge b{test_midi_outputs}; Output out;
    b.configure(true); b.connected(true, 0);
    input(b, 6, {0x90, 60, 0xf8, 100, 61, 101, 0xc0, 12, 13, 0xf1, 2, 0xf2, 3, 4, 0xf3, 5, 0xf6});
    for (unsigned i = 0; i < 20; ++i) b.service(out, 0);
    assert((out.bytes[6] == std::vector<uint8_t>{0xf8, 0x90, 60, 100, 0x90, 61, 101, 0xc0, 12, 0xc0, 13, 0xf1, 2, 0xf2, 3, 4, 0xf3, 5, 0xf6}));
    for (unsigned channel = 0; channel < 16; ++channel) {
        for (unsigned kind = 8; kind <= 14; ++kind) {
            unsigned route = channel % 6;
            unsigned cin = kind;
            uint32_t packet = cin | route << 4 | (kind << 4 | channel) << 8 | 1 << 16;
            if (kind != 12 && kind != 13) packet |= 2 << 24;
            forward(b, packet, 0); b.service(out, 0);
            assert(out.bytes[route].back() == (kind == 12 || kind == 13 ? 1 : 2));
        }
    }
    forward(b, 0x0000006f, 0); // nonexistent cable
    forward(b, 0x00803c09, 0); // data byte has high bit
    assert(b.counters(0).dropped == 1);
}

void test_independent_outputs_and_pedal_reservation() {
    cntrl::midi::Bridge b{test_midi_outputs}; Output out;
    b.configure(true); b.connected(true, 0); out.blocked[0] = true;
    for (unsigned i = 0; i < 1000; ++i) input(b, 0, {0x90, 60, 100});
    assert(b.counters(0).high_water == b.capacity);
    assert(b.counters(0).dropped == 1000 - b.capacity);
    uint8_t patch[] = {0xb0, 0, 1, 0xb0, 32, 2, 0xc0, 3};
    assert(b.pedal(0, patch, sizeof(patch)));
    assert(b.pedal(1, patch, sizeof(patch)));
    input(b, 1, {0x90, 64, 127});
    for (unsigned i = 0; i < 4; ++i) b.service(out, 0);
    assert(out.bytes[0].empty());
    assert((out.bytes[1] == std::vector<uint8_t>{0xb0, 0, 1, 0xb0, 32, 2, 0xc0, 3, 0x90, 64, 127}));
    assert(out.attempts <= 4 * b.destinations());
    out.blocked[0] = false; b.service(out, 0);
    assert(out.bytes[0][0] == 0xb0); // pedal precedes existing bridge backlog
}

void test_sysex_streaming_and_pedal_ownership() {
    cntrl::midi::Bridge b{test_midi_outputs}; Output out;
    b.configure(true); b.connected(true, 0);
    std::vector<uint8_t> expected{0xf0};
    input(b, 0, {0xf0, 1, 2}); b.service(out, 0);
    uint8_t patch[] = {0xc0, 12}; assert(b.pedal(0, patch, 2));
    assert(b.pedal(1, patch, 2));
    expected.insert(expected.end(), {1, 2});
    for (unsigned i = 0; i < 10000; ++i) {
        uint8_t x = i % 128; expected.push_back(x); b.receive(0, x, i / 100); b.service(out, i / 100);
    }
    b.receive(0, 0xf7, 100); expected.push_back(0xf7);
    for (unsigned i = 0; i < 5; ++i) b.service(out, 100);
    expected.insert(expected.end(), {0xc0, 12});
    assert(out.bytes[0] == expected);
    assert((out.bytes[1] == std::vector<uint8_t>{0xc0, 12}));
    assert(b.idle());
    // USB chunks, including short terminal packets and in-stream realtime.
    forward(b, 0x0201f004, 200); forward(b, 0x0000f80f, 200); forward(b, 0x00f70306, 200);
    for (unsigned i = 0; i < 4; ++i) b.service(out, 200);
    assert(out.bytes[0][expected.size()] == 0xf0);
    assert(out.bytes[0].back() == 0xf7);
}

void test_sysex_timeout_overflow_and_disconnect() {
    cntrl::midi::Bridge b{test_midi_outputs}; Output out;
    b.configure(true); b.connected(true, 0);
    input(b, 0, {0xf0, 1, 2}); b.service(out, 0);
    uint8_t patch[] = {0xc0, 9}; assert(b.pedal(0, patch, 2));
    input(b, 0, {0xf8}, 999); b.service(out, 999); b.service(out, 1000); b.service(out, 1001);
    assert((out.bytes[0] == std::vector<uint8_t>{0xf0, 1, 2, 0xf8, 0xf7, 0xc0, 9}));
    assert(b.counters(0).aborted == 1);
    input(b, 0, {3, 4, 0xf7, 0xc0, 10}); b.service(out, 1001);
    assert(out.bytes[0].back() == 10);
    input(b, 0, {0xf0, 1, 2}, 1001); b.service(out, 1001);
    out.blocked[0] = true;
    for (unsigned i = 0; i < 1000; ++i) input(b, 0, {1, 2, 3}, 1001);
    assert(b.counters(0).aborted == 2);
    out.blocked[0] = false; b.service(out, 1001);
    assert(out.bytes[0].back() == 0xf7);
    // Disconnect discards bridge backlog, but keeps physical pedal batches.
    input(b, 1, {0x90, 60, 100}); assert(b.pedal(1, patch, 2));
    b.connected(false, 0); b.service(out, 1001);
    assert((out.bytes[1] == std::vector<uint8_t>{0xc0, 9}));
    b.connected(true, 0); b.service(out, 1001);
    assert(out.bytes[1].size() == 2);
}

void test_default_off_and_disabled_explicit_usb() {
    cntrl::midi::Bridge b{test_midi_outputs}; Output out;
    b.connected(true, 0); input(b, 6, {0x90, 60, 100}); forward(b, 0x00643c09, 0);
    b.service(out, 0); assert(out.bytes[0].empty() && out.bytes[6].empty());
    uint8_t patch[] = {0xc0, 12}; assert(b.pedal(6, patch, 2)); b.service(out, 0);
    assert((out.bytes[6] == std::vector<uint8_t>{0xc0, 12}));
    b.configure(true); input(b, 6, {0xf0, 1, 2}); b.service(out, 0);
    assert(b.pedal(6, patch, 2)); b.configure(false);
    b.service(out, 0); b.service(out, 0);
    assert(out.bytes[6][out.bytes[6].size() - 3] == 0xf7);
    assert(out.bytes[6].back() == 12);
    // A stopped host consumes bounded attempts, never stalls a physical port.
    b.configure(true); out.blocked[6] = true;
    for (unsigned i = 0; i < 1000; ++i) input(b, 6, {0x90, 60, 100});
    assert(b.pedal(0, patch, 2));
    unsigned before = out.attempts;
    b.service(out, 0);
    assert(out.attempts - before <= b.destinations());
    assert(out.bytes[0].back() == 12);
    std::cout << "Bridge storage: " << sizeof(b) << " bytes\n";
}

void test_usb_sysex_resynchronizes_after_overflow() {
    cntrl::midi::Bridge b{test_midi_outputs}; Output out;
    b.configure(true); b.connected(true, 0); out.blocked[0] = true;
    forward(b, 0x0201f004, 0);
    for (unsigned i = 0; i < 300; ++i) forward(b, 0x03020104, 0);
    forward(b, 0x0000f705, 0);
    out.blocked[0] = false;
    forward(b, 0x00f7f006, 1); b.service(out, 1);
    assert((out.bytes[0] == std::vector<uint8_t>{0xf0, 0xf7}));
}

void test_malformed_pedal_batches_are_rejected_atomically() {
    cntrl::midi::Bridge b{test_midi_outputs}; Output out;
    const std::vector<std::vector<uint8_t>> invalid = {
        {0xc0}, {0x90}, {0x90, 60}, {0xc0, 1, 0x90, 60},
        {0xc0, 1, 0xf8}, {0x70, 1, 2}, {0xc0, 0x80}, {0x90, 60, 0x80},
    };
    for (const auto &bytes : invalid) {
        assert(!b.pedal(0, bytes.data(), unsigned(bytes.size())));
        b.service(out, 0);
        assert(b.idle() && out.attempts == 0);
    }
    for (unsigned kind = 8; kind <= 14; ++kind) {
        uint8_t bytes[] = {uint8_t(kind << 4), 1, 2};
        unsigned n = kind == 12 || kind == 13 ? 2 : 3;
        assert(b.pedal(0, bytes, n)); b.service(out, 0);
        assert(b.idle());
    }
    assert(out.attempts == 7);
}
void test_host_disconnect_discards_pending_selection() {
    cntrl::midi::Bridge b{test_midi_outputs}; Output out;
    uint8_t bytes[] = {0xb0, 0, 1, 0xc0, 2};
    b.host_connected(true); assert(b.pedal(8, bytes, sizeof(bytes)));
    b.service(out, 0); // Bank delivered; program still pending.
    assert(!b.idle()); b.host_connected(false);
    assert(b.idle() && b.counters(8).dropped == 1);
    b.host_connected(true); b.service(out, 1);
    assert((out.bytes[8] == std::vector<uint8_t>{0xb0, 0, 1}));
    assert(b.pedal(8, bytes, sizeof(bytes))); out.blocked[8] = true;
    b.service(out, 2); b.host_connected(false);
    assert(b.idle() && b.counters(8).dropped == 2);
}
void test_tool_handoff_waits_for_sysex_termination() {
    cntrl::midi::Bridge b{test_midi_outputs}; Output out; b.configure(true); b.connected(true, 0);
    input(b, 0, {0xf0, 1, 2}); b.service(out, 0);
    b.configure(false); out.blocked[0] = true;
    assert(!b.handoff(out));
    assert((out.bytes[0] == std::vector<uint8_t>{0xf0, 1, 2}));
    out.blocked[0] = false; assert(b.handoff(out));
    assert((out.bytes[0] == std::vector<uint8_t>{0xf0, 1, 2, 0xf7}));
    assert(b.handoff(out) && out.bytes[0].size() == 4);
}
void test_detaching_a_jack_discards_queued_bytes_and_sysex_termination() {
    cntrl::midi::Bridge b{test_midi_outputs}; Output out; b.configure(true); b.connected(true, 0);
    input(b, 2, {0xf0, 1, 2}); b.service(out, 0);
    input(b, 2, {3, 4, 5});
    b.disconnect_output(2); out.blocked[2] = true;
    assert(b.handoff(out));
    out.blocked[2] = false; b.service(out, 1);
    assert((out.bytes[2] == std::vector<uint8_t>{0xf0, 1, 2}));
}
void test_owned_sysex_streams_and_abandonment_terminates() {
    cntrl::midi::Bridge bridge{test_midi_outputs}; Output output;
    std::vector<uint8_t> bytes(257, 0x12); bytes.front() = 0xf0; bytes.back() = 0xf7;
    assert(bridge.owned_stream(5, 0, bytes.data(), unsigned(bytes.size())));
    bridge.service(output, 0);
    uint8_t ordinary[] = {0xb0, 7, 1};
    assert(bridge.owned_send(1, 0, ordinary, sizeof(ordinary)));
    for (unsigned i = 1; i < 86; ++i) bridge.service(output, i);
    assert(output.bytes[0] == bytes);
    assert(bridge.delivery(5) == cntrl::midi::Bridge::Delivery::complete);
    bridge.service(output, 87);
    assert(output.bytes[0].size() == 260);
    assert(bridge.owned_stream(5, 0, bytes.data(), unsigned(bytes.size())));
    bridge.service(output, 88); bridge.cancel_owner(5); bridge.service(output, 89);
    assert(output.bytes[0].back() == 0xf7);
    assert(bridge.delivery(5) == cntrl::midi::Bridge::Delivery::cancelled);
}
void test_owned_nrpn_is_not_interleaved_with_other_owners() {
    cntrl::midi::Bridge bridge{test_midi_outputs}; Output output;
    uint8_t nrpn[] = {0xb0, 99, 1, 0xb0, 98, 2, 0xb0, 6, 3, 0xb0, 38, 4};
    uint8_t other[] = {0xb0, 99, 7};
    assert(bridge.owned_stream(0, 0, nrpn, sizeof(nrpn)));
    bridge.service(output, 0);
    assert(bridge.owned_stream(1, 0, other, sizeof(other)));
    for (unsigned i = 1; i < 5; ++i) bridge.service(output, i);
    std::vector<uint8_t> expected(nrpn, nrpn + sizeof(nrpn));
    expected.insert(expected.end(), other, other + sizeof(other));
    assert(output.bytes[0] == expected);
    assert(!cntrl::midi::message_valid(nrpn, sizeof(nrpn)));
    assert(cntrl::midi::stream_valid(nrpn, sizeof(nrpn)));
    uint8_t invalid[] = {0xf0, 1, 0xf8, 0xf7};
    assert(!cntrl::midi::message_valid(invalid, sizeof(invalid)));
}
void test_automatic_interface_retries_without_replaying_waiting_traffic() {
    cntrl::midi::Bridge b{test_midi_outputs}; Output out;
    b.configure(true); b.connected(false, 0);
    b.connected(false, 2000); b.connected(true, 3999);
    input(b, 6, {0x90, 60, 100}, 3999); forward(b, 0x643c9009, 3999);
    b.service(out, 3999);
    assert(out.bytes[0].empty() && out.bytes[6].empty());
    // Explicit pedal USB traffic stays available during the interface wait.
    uint8_t patch[] = {0xc0, 12};
    assert(b.pedal(6, patch, 2)); b.service(out, 3999);
    assert((out.bytes[6] == std::vector<uint8_t>{0xc0, 12}));
    input(b, 6, {0xf0, 1}, 3999);
    b.connected(true, 4000);
    input(b, 6, {2, 0xf7}, 4000); b.service(out, 4000);
    assert(out.bytes[6].size() == 2); // No partial SysEx crosses the retry boundary.
    input(b, 6, {0x90, 61, 100}, 4000); forward(b, 0x643d9009, 4000); b.service(out, 4000);
    assert(out.bytes[6].size() == 5 && out.bytes[0].size() == 3);
    b.connected(false, 4100); b.connected(true, 6099);
    input(b, 6, {0xf8}, 6099); b.service(out, 6099);
    assert(out.bytes[6].size() == 5);
    b.connected(true, 6100); input(b, 6, {0xf8}, 6100); b.service(out, 6100);
    assert(out.bytes[6].size() == 6);
    b.configure(false); b.connected(true, 9000);
    input(b, 6, {0xf8}, 9000); b.service(out, 9000);
    assert(out.bytes[6].size() == 6);
    b.configure(true); input(b, 6, {0xf8}, 9001); b.service(out, 9001);
    assert(out.bytes[6].size() == 7); // Manual enable with a host is immediate.
}

void test_interface_retry_clock_wrap() {
    cntrl::midi::Bridge b{test_midi_outputs}; Output out;
    b.configure(true); b.connected(false, 0xffffff00u);
    b.connected(true, 1743); input(b, 6, {0xf8}, 1743); b.service(out, 1743);
    assert(out.bytes[6].empty());
    b.connected(true, 1744); input(b, 6, {0xf8}, 1744); b.service(out, 1744);
    assert((out.bytes[6] == std::vector<uint8_t>{0xf8}));
}

void test_usb_ports_enable_independently() {
    for (bool device : {false, true}) for (bool host : {false, true}) {
        cntrl::midi::Bridge b{test_midi_outputs}; Output out;
        b.connected(true, 0); b.host_connected(true); b.configure(true);
        b.configure_usb(device, host, 0);
        const uint8_t note[] = {0x90, 60, 100};
        assert(b.owned_send(0, 6, note, sizeof(note)) == device);
        assert(b.owned_send(1, 8, note, sizeof(note)) == host);
        assert(b.pedal(7, note, sizeof(note)) == device);
        assert(b.pedal(8, note, sizeof(note)) == host);
        forward(b, 0x643c9009, 0);
        b.service(out, 0);
        assert(out.bytes[0].empty() == !device);
        assert(out.bytes[6].empty() == !device);
        assert(out.bytes[8].empty() == !host);
        b.configure_usb(false, false, 1);
        b.connected(true, 1); b.host_connected(true);
        assert(!b.usb_enabled(6) && !b.usb_enabled(8));
        for (auto &bytes : out.bytes) bytes.clear();
        b.service(out, 1);
        assert(out.bytes[6].empty() && out.bytes[7].empty() && out.bytes[8].empty());
        b.configure_usb(true, true, 2);
        b.service(out, 2);
        assert(out.bytes[6].empty() && out.bytes[7].empty() && out.bytes[8].empty());
        assert(b.owned_send(0, 6, note, sizeof(note)));
        assert(b.owned_send(1, 8, note, sizeof(note)));
        b.service(out, 2);
        assert(!out.bytes[6].empty() && !out.bytes[8].empty());
    }
    // Disable also cancels accepted work while the cable is already absent.
    cntrl::midi::Bridge b{test_midi_outputs}; Output out;
    const uint8_t note[] = {0x90, 60, 100};
    assert(b.owned_send(0, 8, note, sizeof(note)));
    b.configure_usb(true, false, 0);
    assert(b.delivery(0) == cntrl::midi::Bridge::Delivery::cancelled);
    assert(b.pedal(0, note, sizeof(note)));
    b.service(out, 0);
    assert(!out.bytes[0].empty() && out.bytes[8].empty());
}

void test_generated_clock_under_load() {
    cntrl::ClockGenerator clock;
    Output out;
    constexpr uint32_t start = UINT32_MAX - 40000;
    clock.start(start, 3, false);
    out.blocked[1] = true;
    clock.poll(start, 3, out);
    assert(out.bytes[0] == std::vector<uint8_t>{0xfa});
    for (uint32_t t = 0; t < 500000;) {
        // Vary scheduler latency across the microsecond-counter rollover.
        t = std::min(500000u, t + ((t & 1) ? 1999u : 307u));
        clock.poll(start + t, 3, out);
        assert(out.bytes[0].size() == 1 + uint64_t(t) * 24 / 500000);
    }
    assert(clock.skipped == 0 && clock.late_us < 2000 && clock.blocked == 24);
    assert(out.bytes[1].empty());
    out.blocked[1] = false;
    clock.set_tempo(250000);
    clock.poll(start + 750000, 3, out);
    assert(clock.skipped == 23 && out.bytes[0].size() == 26);
    assert((out.bytes[1] == std::vector<uint8_t>{0xfa, 0xf8}));
    clock.poll(start + 750000, 3, out);
    assert(out.bytes[0].size() == 26);
    clock.poll(start + 760000, 1, out);
    clock.poll(start + 780000, 3, out);
    assert(out.bytes[1].size() == 2); // Reconnect never replays missed clocks.
    clock.stop();
    out.blocked[0] = true;
    clock.poll(start + 800000, 3, out);
    assert(clock.stopping() == 1);
    out.blocked[0] = false;
    clock.poll(start + 810000, 3, out);
    assert(out.bytes[0].back() == 0xfc && !clock.running() && !clock.stopping());
    clock.start(start + 820000, 1, true);
    clock.poll(start + 820000, 1, out);
    assert(out.bytes[0].back() == 0xfb);
}

void test_clock_source_handoff() {
    cntrl::midi::Bridge bridge{test_midi_outputs}; Output out;
    bridge.configure(true); bridge.connected(true, 0);
    input(bridge, 0, {0xfa, 0xf8, 0x90, 60, 100});
    bridge.clock_owner(1);
    input(bridge, 0, {0xf8, 0xfc, 0xfe});
    input(bridge, 1, {0xf8});
    for (unsigned i = 0; i < 6; ++i) bridge.service(out, i);
    assert((out.bytes[0] == std::vector<uint8_t>{0x90, 60, 100, 0xfe}));
    assert(out.bytes[1] == std::vector<uint8_t>{0xf8});
    bridge.clock_owner(0);
    input(bridge, 0, {0xfb, 0xf8});
    bridge.service(out, 7); bridge.service(out, 8);
    assert((out.bytes[0] == std::vector<uint8_t>{0x90, 60, 100, 0xfe, 0xfb, 0xf8}));
}

void test_file_sysex_waits_for_previous_stream_and_allows_realtime() {
    cntrl::midi::Bridge bridge{test_midi_outputs}; Output out;
    bridge.configure(true); bridge.connected(true, 0);
    input(bridge, 0, {0xf0, 1, 2}); bridge.service(out, 0);
    assert(!bridge.sysex_ready(1));
    input(bridge, 0, {3, 0xf7}); bridge.service(out, 0, 1);
    assert(bridge.sysex_ready(1));
    const auto size = out.bytes[0].size();
    input(bridge, 0, {0x90, 60, 100}); bridge.service(out, 0, 1);
    assert(out.bytes[0].size() == size);
    input(bridge, 0, {0xf8}); bridge.service(out, 0, 1);
    assert(out.bytes[0].size() == size + 1 && out.bytes[0].back() == 0xf8);
    bridge.service(out, 0);
    assert(out.bytes[0].size() == size + 4);
}

int main() {
    const cntrl::midi::OutputConnection connections[] = {
        {"input-only", "Input", 0, cntrl::midi::OutputTransport::physical, true, false},
        {"synth", "Synth", 1, cntrl::midi::OutputTransport::physical, false, true},
        {"offline", "Offline", 2, cntrl::midi::OutputTransport::usb_device, false, true},
    };
    assert(cntrl::midi::available_outputs({connections, 3}, [](const char *name) { return std::strcmp(name, "offline"); }) == (1u << 1));
    test_file_sysex_waits_for_previous_stream_and_allows_realtime();
    test_generated_clock_under_load();
    test_clock_source_handoff();
    test_usb_ports_enable_independently();
    test_automatic_interface_retries_without_replaying_waiting_traffic();
    test_interface_retry_clock_wrap();
    test_owned_sysex_streams_and_abandonment_terminates();
    test_owned_nrpn_is_not_interleaved_with_other_owners();
    for (const auto &connection : test_midi_outputs) {
        assert(test_midi_outputs.find(connection.name) == &connection);
        assert(test_midi_outputs.route(connection.route) == &connection);
    }
    assert(!test_midi_outputs.find("missing"));
    const cntrl::midi::OutputConnection reordered[] = {
        {"host", "Host", 0, cntrl::midi::OutputTransport::usb_host},
        {"wire", "Wire", 1, cntrl::midi::OutputTransport::physical},
        {"computer", "Computer", 2, cntrl::midi::OutputTransport::usb_device},
    };
    cntrl::midi::Bridge differently_ordered({reordered, 3});
    assert(differently_ordered.destinations() == 3);
    differently_ordered.configure_usb(false, true, 0);
    assert(differently_ordered.usb_enabled(0) && differently_ordered.usb_enabled(1));
    assert(!differently_ordered.usb_enabled(2) && !differently_ordered.usb_enabled(3));
    test_tool_handoff_waits_for_sysex_termination();
    test_detaching_a_jack_discards_queued_bytes_and_sysex_termination();
    test_host_disconnect_discards_pending_selection();
    test_malformed_pedal_batches_are_rejected_atomically();
    test_messages_and_running_status();
    test_independent_outputs_and_pedal_reservation();
    test_sysex_streaming_and_pedal_ownership();
    test_sysex_timeout_overflow_and_disconnect();
    test_default_off_and_disabled_explicit_usb();
    test_usb_sysex_resynchronizes_after_overflow();
    std::cout << "MIDI bridge tests passed\n";
}
