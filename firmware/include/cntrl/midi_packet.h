#pragma once
#include <cstdint>

namespace cntrl::midi {
// A complete short message, or up to three bytes of a streamed SysEx.
struct Packet {
    uint8_t cin = 0, bytes[3] = {};
    // Live input identity is internal metadata, never part of the MIDI payload.
    uint16_t source = 65535;
    unsigned size() const {
        static constexpr uint8_t lengths[] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
        return lengths[cin & 15];
    }
    uint32_t packed(unsigned cable = 0) const {
        return cin | (cable << 4) | uint32_t(bytes[0]) << 8
            | uint32_t(bytes[1]) << 16 | uint32_t(bytes[2]) << 24;
    }
};
} // namespace cntrl::midi
