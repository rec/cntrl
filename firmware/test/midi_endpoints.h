#pragma once
#include <cntrl/midi_outputs.h>

inline constexpr cntrl::midi::OutputConnection test_midi_endpoints[] = {
    {"din", "DIN", 0, cntrl::midi::OutputTransport::physical, true},
    {"trs", "TRS", 1, cntrl::midi::OutputTransport::physical, true},
    {"configurable-1", "Port 1", 2, cntrl::midi::OutputTransport::physical, false, true, 0},
    {"configurable-2", "Port 2", 3, cntrl::midi::OutputTransport::physical, false, true, 1},
    {"configurable-3", "Port 3", 4, cntrl::midi::OutputTransport::physical, false, true, 2},
    {"configurable-4", "Port 4", 5, cntrl::midi::OutputTransport::physical, false, true, 3},
    {"usb-device", "Device", 6, cntrl::midi::OutputTransport::usb_device, true},
    {"usb-return", "Return", 7, cntrl::midi::OutputTransport::usb_device, false, false},
    {"usb-host", "Host", 8, cntrl::midi::OutputTransport::usb_host, true},
};
inline constexpr cntrl::midi::Outputs test_midi_outputs{test_midi_endpoints, 9};
inline constexpr cntrl::midi::Outputs test_cable_outputs{test_midi_endpoints, 2};
