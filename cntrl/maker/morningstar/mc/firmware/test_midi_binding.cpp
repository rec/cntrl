#include "patch_switcher/midi_binding.h"
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
    };
    cntrl::midi::Outputs outputs{endpoints, 9};
    const MidiDriver drivers[] = {MidiDriver::din, MidiDriver::trs, MidiDriver::configurable,
        MidiDriver::configurable, MidiDriver::configurable, MidiDriver::configurable,
        MidiDriver::usb_device, MidiDriver::usb_device, MidiDriver::usb_host};
    const unsigned indices[] = {0, 0, 0, 1, 2, 3, 0, 1, 0};
    for (unsigned permutation = 0; permutation < 9; ++permutation) {
        for (unsigned i = 0; i < 9; ++i) endpoints[i].route = (8 - i + permutation) % 9;
        for (unsigned i = 0; i < 9; ++i) {
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
}
