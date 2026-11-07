#pragma once
#include <cntrl/midi_outputs.h>

namespace cntrl::device {
// Names bind fixed hardware sockets; logical route IDs may be reordered freely.
enum class MidiDriver { missing, din, trs, configurable, usb_device, usb_host };
struct MidiBinding { MidiDriver driver = MidiDriver::missing; unsigned index = 0; };
inline MidiBinding midi_binding(const midi::OutputConnection &endpoint) {
    switch (endpoint.transport) {
    case midi::OutputTransport::physical:
        if (endpoint.port < 4) return {MidiDriver::configurable, endpoint.port};
        if (!std::strcmp(endpoint.name, "din")) return {MidiDriver::din, 0};
        if (!std::strcmp(endpoint.name, "trs")) return {MidiDriver::trs, 0};
        break;
    case midi::OutputTransport::usb_device:
        if (!std::strcmp(endpoint.name, "usb-device")) return {MidiDriver::usb_device, 0};
        if (!std::strcmp(endpoint.name, "usb-return")) return {MidiDriver::usb_device, 1};
        break;
    case midi::OutputTransport::usb_host:
        if (!std::strcmp(endpoint.name, "usb-host")) return {MidiDriver::usb_host, 0};
        break;
    }
    return {};
}
inline const midi::OutputConnection *midi_endpoint(midi::Outputs endpoints, MidiDriver driver, unsigned index = 0) {
    for (const auto &endpoint : endpoints) {
        const auto binding = midi_binding(endpoint);
        if (binding.driver == driver && binding.index == index) return &endpoint;
    }
    return nullptr;
}
} // namespace cntrl::device
