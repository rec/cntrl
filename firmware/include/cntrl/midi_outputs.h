#pragma once
#include <array>
#include <cstring>
#include <cstdint>

namespace cntrl::midi {
enum class OutputTransport { physical, usb_device, usb_host };
struct OutputConnection {
    const char *name, *label;
    unsigned route;
    OutputTransport transport;
};

enum class TrsMode : uint8_t { off, midi_a, midi_b, expression, aux };
inline bool midi_output(TrsMode mode) { return mode == TrsMode::midi_a || mode == TrsMode::midi_b; }
inline constexpr std::array<OutputConnection, 6> output_connections = {{
    {"din", "DIN", 0, OutputTransport::physical},
    {"trs", "TRS", 1, OutputTransport::physical},
    {"configurable-1", "TRS 1", 2, OutputTransport::physical},
    {"configurable-2", "TRS 2", 3, OutputTransport::physical},
    {"configurable-3", "TRS 3", 4, OutputTransport::physical},
    {"configurable-4", "TRS 4", 5, OutputTransport::physical},
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
