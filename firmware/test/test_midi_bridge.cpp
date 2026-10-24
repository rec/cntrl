#include <cntrl/midi_bridge.h>
#include <cassert>
#include <vector>
#include <iostream>

struct Output : cntrl::midi::Sink {
    std::vector<uint8_t> bytes[9];
    bool blocked[9] = {};
    unsigned attempts = 0;
    bool submit(unsigned route, const cntrl::midi::Packet &p) override {
        ++attempts;
        if (blocked[route]) return false;
        bytes[route].insert(bytes[route].end(), p.bytes, p.bytes + p.size()); return true;
    }
};

void input(cntrl::midi::Bridge &b, unsigned route, std::initializer_list<uint8_t> bytes, unsigned now = 0) {
    for (uint8_t x : bytes) b.receive(route, x, now);
}

void test_messages_and_running_status() {
    cntrl::midi::Bridge b; Output out;
    b.configure(true); b.connected(true);
    input(b, 6, {0x90, 60, 0xf8, 100, 61, 101, 0xc0, 12, 13, 0xf1, 2, 0xf2, 3, 4, 0xf3, 5, 0xf6});
    for (unsigned i = 0; i < 20; ++i) b.service(out, 0);
    assert((out.bytes[6] == std::vector<uint8_t>{0xf8, 0x90, 60, 100, 0x90, 61, 101, 0xc0, 12, 0xc0, 13, 0xf1, 2, 0xf2, 3, 4, 0xf3, 5, 0xf6}));
    for (unsigned channel = 0; channel < 16; ++channel) {
        for (unsigned kind = 8; kind <= 14; ++kind) {
            unsigned route = channel % 6;
            unsigned cin = kind;
            uint32_t packet = cin | route << 4 | (kind << 4 | channel) << 8 | 1 << 16;
            if (kind != 12 && kind != 13) packet |= 2 << 24;
            b.usb(packet, 0); b.service(out, 0);
            assert(out.bytes[route].back() == (kind == 12 || kind == 13 ? 1 : 2));
        }
    }
    b.usb(0x0000006f, 0); // nonexistent cable
    b.usb(0x00803c09, 0); // data byte has high bit
    assert(b.counters(0).dropped == 1);
}

void test_independent_outputs_and_pedal_reservation() {
    cntrl::midi::Bridge b; Output out;
    b.configure(true); b.connected(true); out.blocked[0] = true;
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
    assert(out.attempts <= 4 * b.destinations);
    out.blocked[0] = false; b.service(out, 0);
    assert(out.bytes[0][0] == 0xb0); // pedal precedes existing bridge backlog
}

void test_sysex_streaming_and_pedal_ownership() {
    cntrl::midi::Bridge b; Output out;
    b.configure(true); b.connected(true);
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
    b.usb(0x0201f004, 200); b.usb(0x0000f80f, 200); b.usb(0x00f70306, 200);
    for (unsigned i = 0; i < 4; ++i) b.service(out, 200);
    assert(out.bytes[0][expected.size()] == 0xf0);
    assert(out.bytes[0].back() == 0xf7);
}

void test_sysex_timeout_overflow_and_disconnect() {
    cntrl::midi::Bridge b; Output out;
    b.configure(true); b.connected(true);
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
    b.connected(false); b.service(out, 1001);
    assert((out.bytes[1] == std::vector<uint8_t>{0xc0, 9}));
    b.connected(true); b.service(out, 1001);
    assert(out.bytes[1].size() == 2);
}

void test_default_off_and_disabled_explicit_usb() {
    cntrl::midi::Bridge b; Output out;
    b.connected(true); input(b, 6, {0x90, 60, 100}); b.usb(0x00643c09, 0);
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
    assert(out.attempts - before <= b.destinations);
    assert(out.bytes[0].back() == 12);
    std::cout << "Bridge storage: " << sizeof(b) << " bytes\n";
}

void test_usb_sysex_resynchronizes_after_overflow() {
    cntrl::midi::Bridge b; Output out;
    b.configure(true); b.connected(true); out.blocked[0] = true;
    b.usb(0x0201f004, 0);
    for (unsigned i = 0; i < 300; ++i) b.usb(0x03020104, 0);
    b.usb(0x0000f705, 0);
    out.blocked[0] = false;
    b.usb(0x00f7f006, 1); b.service(out, 1);
    assert((out.bytes[0] == std::vector<uint8_t>{0xf0, 0xf7}));
}

int main() {
    test_messages_and_running_status();
    test_independent_outputs_and_pedal_reservation();
    test_sysex_streaming_and_pedal_ownership();
    test_sysex_timeout_overflow_and_disconnect();
    test_default_off_and_disabled_explicit_usb();
    test_usb_sysex_resynchronizes_after_overflow();
    std::cout << "MIDI bridge tests passed\n";
}
