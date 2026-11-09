#include "patch_switcher/midi_binding.h"
#include <cntrl/usb_midi_forward.h>
#include <cassert>

int main() {
    using cntrl::device::MidiDriver;
    using cntrl::midi::OutputTransport;
    cntrl::midi::OutputConnection endpoints[] = {
        {"din", "DIN", 0, OutputTransport::physical, true, true},
        {"trs", "TRS", 1, OutputTransport::physical, true, true},
        {"jack-a", "A", 2, OutputTransport::physical, false, true, 0},
        {"jack-b", "B", 3, OutputTransport::physical, false, true, 1},
        {"jack-c", "C", 4, OutputTransport::physical, false, true, 2},
        {"jack-d", "D", 5, OutputTransport::physical, false, true, 3},
        {"usb-device", "Device", 6, OutputTransport::usb_device, true, true},
        {"usb-return", "Return", 7, OutputTransport::usb_device, false, false},
        {"usb-host", "Host", 8, OutputTransport::usb_host, true, true},
        {"usb-host-return", "Hosted return", 9, OutputTransport::usb_device, false, false},
    };
    cntrl::midi::Outputs outputs{endpoints, 10};
    const MidiDriver drivers[] = {MidiDriver::din, MidiDriver::trs, MidiDriver::configurable,
        MidiDriver::configurable, MidiDriver::configurable, MidiDriver::configurable,
        MidiDriver::usb_device, MidiDriver::usb_device, MidiDriver::usb_host, MidiDriver::usb_device};
    const unsigned indices[] = {0, 0, 0, 1, 2, 3, 0, 1, 0, 2};
    for (unsigned permutation = 0; permutation < 10; ++permutation) {
        for (unsigned i = 0; i < 10; ++i) endpoints[i].route = (9 - i + permutation) % 10;
        for (unsigned i = 0; i < 10; ++i) {
            const auto *endpoint = outputs.route(endpoints[i].route);
            assert(endpoint);
            auto binding = cntrl::device::midi_binding(*endpoint);
            assert(binding.driver == drivers[i] && binding.index == indices[i]);
            const auto *input_or_forward = cntrl::device::midi_endpoint(outputs, drivers[i], indices[i]);
            assert(input_or_forward && input_or_forward->route == endpoints[i].route);
        }
        // Serial2's forwarding cable remains1 even when usb-return has route0.
        const auto *return_endpoint = cntrl::device::midi_endpoint(outputs, MidiDriver::usb_device, 1);
        assert(return_endpoint && !return_endpoint->output);
    }
    cntrl::midi::OutputConnection unknown{"unknown", "Unknown", 0, OutputTransport::physical};
    assert(cntrl::device::midi_binding(unknown).driver == MidiDriver::missing);
    assert(!cntrl::device::midi_endpoint(outputs, MidiDriver::configurable, 4));

    // Existing two bidirectional and four output-only ports retain their cable
    // numbers; the hosted port is computer output6 and computer input2.
    const uint8_t descriptor[] = {
        9, 2, 188, 0, 2, 1, 0, 0x80, 50,
        9, 4, 0, 0, 2, 1, 3, 0, 0,
        7, 0x24, 1, 0, 1, 127, 0,
        6, 0x24, 2, 1, 1, 5, 6, 0x24, 2, 2, 2, 5,
        9, 0x24, 3, 1, 3, 1, 2, 1, 5, 9, 0x24, 3, 2, 4, 1, 1, 1, 5,
        6, 0x24, 2, 1, 5, 6, 6, 0x24, 2, 2, 6, 6,
        9, 0x24, 3, 1, 7, 1, 6, 1, 6, 9, 0x24, 3, 2, 8, 1, 5, 1, 6,
        6, 0x24, 2, 1, 9, 7, 9, 0x24, 3, 2, 10, 1, 9, 1, 7,
        6, 0x24, 2, 1, 11, 8, 9, 0x24, 3, 2, 12, 1, 11, 1, 8,
        6, 0x24, 2, 1, 13, 9, 9, 0x24, 3, 2, 14, 1, 13, 1, 9,
        6, 0x24, 2, 1, 15, 10, 9, 0x24, 3, 2, 16, 1, 15, 1, 10,
        9, 5, 3, 2, 0, 2, 0, 0, 0, 10, 0x25, 1, 6, 1, 5, 9, 11, 13, 15,
        9, 5, 0x83, 2, 0, 2, 0, 0, 0, 6, 0x25, 1, 2, 3, 7,
        9, 4, 1, 0, 0, 3, 0, 0, 0,
    };
    static_assert(sizeof(descriptor) == 188);
    uint8_t expanded[220];
    assert(cntrl::midi::append_usb_midi_port(descriptor, sizeof(descriptor), expanded, sizeof(expanded), 11) == 220);
    unsigned endpoints_seen = 0;
    for (unsigned i = 9; i < sizeof(expanded); i += expanded[i]) {
        if (expanded[i + 1] != 0x25) continue;
        const unsigned count = expanded[i + 3];
        assert(count == (endpoints_seen ? 3u : 7u));
        assert(expanded[i + 4 + count - 1] == (endpoints_seen ? 19 : 17));
        ++endpoints_seen;
    }
    assert(endpoints_seen == 2);
}
