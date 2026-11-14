#include "patch_switcher/midi_trace.h"
#include <cassert>
#include <cstring>
#include <limits>

cntrl::device::MidiTrace trace;
#define CNTRL_MIDI_REJECT(route, packet, state, now) trace.reject(route, packet, state, now)
#define CNTRL_MIDI_ACCEPT(route, packet) do { \
    if (trace.active && (route) == 0) trace.usb_valid.add((packet).bytes, (packet).size()); \
} while (false)
#include <cntrl/midi_bridge.h>

void forward(cntrl::midi::Bridge &bridge, const cntrl::midi::Packet &packet, unsigned now) {
    if (trace.active) trace.usb_raw.add(packet.bytes, packet.size());
    bridge.forward_packet(0, packet, now);
}

int main() {
    cntrl::memory_budget = 512 * 1024;
    cntrl::device::MidiBytes bytes;
    const auto *known = reinterpret_cast<const uint8_t *>("123456789");
    bytes.add(known, 4); bytes.add(known + 4, 5);
    assert(bytes.count == 9 && ~bytes.checksum == 0xcbf43926u);

    const cntrl::midi::OutputConnection endpoints[] = {
        {"din", "DIN", 0, cntrl::midi::OutputTransport::physical, true},
        {"usb-device", "USB", 1, cntrl::midi::OutputTransport::usb_device, true},
    };
    cntrl::midi::Bridge bridge{{endpoints, 2}};
    bridge.configure(true); bridge.connected(true, 0);
    const cntrl::midi::Packet end{5, {0xf7, 0, 0}, 16};
    forward(bridge, end, 1);
    assert(!trace.rejects); // Stopped diagnostics do not retain events.
    trace.reset();
    forward(bridge, end, 2);
    assert(trace.rejects == 1 && trace.rejected.cin == 5 && trace.rejected.bytes[0] == 0xf7);
    assert(trace.rejected.source == 16 && trace.rejected_route == 0 && trace.rejected_state == 0);
    assert(trace.rejected_ms == 2 && trace.rejected_offset == 0 && trace.rejected_valid_offset == 0);
    assert(trace.usb_raw.count == 1 && trace.usb_valid.count == 0);
    forward(bridge, {4, {0xf0, 0x7d, 1}, 16}, 3);
    forward(bridge, end, 4); // The same terminator is accepted inside SysEx.
    assert(trace.rejects == 1 && trace.usb_valid.count == 4 && trace.usb_raw.count == 5);
    forward(bridge, {0, {1, 2, 3}, 16}, 5);
    assert(trace.rejects == 2 && trace.rejected_ms == 2);
    trace.active = false;
    forward(bridge, end, 6);
    assert(trace.rejects == 2);

    trace.reset();
    assert(trace.active && trace.rejects == 0 && trace.usb_raw.count == 0 && ~trace.usb_valid.checksum == 0);
    trace.poll(100, 0, 0, 0);
    trace.poll(23100, 7, 1, 63);
    assert(trace.drop_delta == 7 && trace.drop_gap_us == 23000 && trace.drop_available == 63);
    assert(trace.overrun_delta == 1 && trace.overrun_gap_us == 23000);
    trace.poll(50000, 9, 2, 63);
    assert(trace.drop_delta == 7 && trace.drop_gap_us == 23000); // Retain the first loss.
    const auto maximum = std::numeric_limits<uint32_t>::max();
    trace.reset(maximum - 2, maximum, maximum - 15);
    trace.poll(32, 4, 0, 63);
    assert(trace.drop_delta == 7 && trace.overrun_delta == 1 && trace.drop_gap_us == 48);
    trace.usb_raw.count = trace.usb_valid.count = trace.din_tx.count = trace.din_rx.count = trace.usb_return.count = maximum;
    trace.usb_raw.checksum = trace.usb_valid.checksum = trace.din_tx.checksum = trace.din_rx.checksum = trace.usb_return.checksum = 0;
    trace.rejected = {15, {255, 255, 255}, 65535};
    trace.rejects = trace.rejected_route = trace.rejected_state = trace.rejected_ms = maximum;
    trace.rejected_offset = trace.rejected_valid_offset = maximum;
    trace.drop_gap_us = trace.drop_available = trace.drop_delta = maximum;
    trace.overrun_gap_us = trace.overrun_available = trace.overrun_delta = maximum;
    char response[1024];
    const int length = trace.format(response, sizeof(response), "0123456789abcdef", maximum, maximum, maximum, maximum);
    assert(length > 0 && unsigned(length) < sizeof(response));
    assert(std::strstr(response, "build=0123456789abcdef"));
    assert(std::strstr(response, "din_drops=4294967295"));
}
