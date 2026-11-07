#pragma once
#include <cstring>
#include <cstdint>

namespace cntrl::midi {
// The setup wire format stores endpoint masks in sixteen bits. Route IDs
// are a dense permutation of 0 through endpoint count minus one.
constexpr unsigned max_routes = 16, no_port = 255;
enum class OutputTransport { physical, usb_device, usb_host };
struct OutputConnection {
    const char *name, *label;
    unsigned route;
    OutputTransport transport;
    bool input = false, output = true;
    unsigned port = no_port;
};
struct Outputs {
    const OutputConnection *data = nullptr;
    unsigned count = 0;
    const OutputConnection *begin() const { return data; }
    const OutputConnection *end() const { return count ? data + count : data; }
    const OutputConnection *find(const char *name) const {
        for (const auto &connection : *this) if (!std::strcmp(connection.name, name)) return &connection;
        return nullptr;
    }
    const OutputConnection *route(unsigned index) const {
        for (const auto &connection : *this) if (connection.route == index) return &connection;
        return nullptr;
    }
    unsigned route_count() const {
        unsigned result = 0;
        for (const auto &connection : *this) if (connection.route >= result) result = connection.route + 1;
        return result;
    }
    unsigned mask(bool inputs = false) const {
        unsigned result = 0;
        for (const auto &connection : *this)
            if (inputs ? connection.input : connection.output) result |= 1u << connection.route;
        return result;
    }
    unsigned transport_mask(OutputTransport transport) const {
        unsigned result = 0;
        for (const auto &connection : *this) if (connection.transport == transport) result |= 1u << connection.route;
        return result;
    }
};
template<class Available>
unsigned available_outputs(Outputs outputs, Available available) {
    unsigned mask = 0;
    for (const auto &connection : outputs)
        if (connection.output && available(connection.name)) mask |= 1u << connection.route;
    return mask;
}

enum class TrsMode : uint8_t { off, midi_a, midi_b, expression, aux };
inline bool midi_output(TrsMode mode) { return mode == TrsMode::midi_a || mode == TrsMode::midi_b; }
} // namespace cntrl::midi
