#pragma once
#include <array>
#include <cstring>

namespace cntrl::midi {
enum class OutputTransport { physical, usb_device, usb_host };
struct OutputConnection {
    const char *name, *label;
    unsigned route;
    OutputTransport transport;
};

// Enabled physical MIDI outputs. Omniports are not MIDI outputs yet.
inline constexpr std::array<OutputConnection, 2> output_connections = {{
    {"din", "DIN", 0, OutputTransport::physical},
    {"trs", "TRS", 1, OutputTransport::physical},
}};
inline constexpr std::array<OutputConnection, 2> usb_output_connections = {{
    {"usb-device", "USB device", 6, OutputTransport::usb_device},
    {"usb-host", "USB host", 8, OutputTransport::usb_host},
}};

inline const OutputConnection *find_output(const char *name) {
    for (const auto &c : output_connections) if (!std::strcmp(c.name, name)) return &c;
    for (const auto &c : usb_output_connections) if (!std::strcmp(c.name, name)) return &c;
    return nullptr;
}
inline const OutputConnection *physical_output(unsigned route) {
    for (const auto &c : output_connections) if (c.route == route) return &c;
    return nullptr;
}
} // namespace cntrl::midi
