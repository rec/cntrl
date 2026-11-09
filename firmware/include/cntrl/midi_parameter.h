#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

namespace cntrl::midi {
// One logical parameter, before splitting it into MIDI 1.0 messages.
struct Parameter {
    enum Kind : uint8_t { none, cc7, cc14, bend, pressure, key_pressure, nrpn7, nrpn14, rpn14, program };
    uint8_t kind = none, channel = 0;
    uint16_t address = 0;
    int32_t value = 0;
    static constexpr int unknown = std::numeric_limits<int32_t>::min();
    int minimum() const { return kind == bend ? -8192 : kind == program ? 1 : 0; }
    int maximum() const {
        return kind == bend ? 8191 : kind == program ? 128
            : kind == cc14 || kind == nrpn14 || kind == rpn14 ? 16383 : 127;
    }
    unsigned type() const {
        return kind == bend ? 14 : kind == pressure ? 13 : kind == key_pressure ? 10
            : kind == program ? 12 : kind == nrpn7 || kind == nrpn14 ? 16 : kind == rpn14 ? 17 : 11;
    }
    unsigned value_field() const { return kind == program ? 6 : kind == pressure || kind == key_pressure ? 7 : 5; }
    bool valid() const {
        if (!kind || kind > program || channel > 15 || value < minimum() || value > maximum()) return false;
        if (kind == cc7 || kind == cc14) {
            if (address > (kind == cc14 ? 31 : 119)) return false;
            switch (address) {
            case 0: case 6: case 32: case 38: case 64: case 65: case 66: case 67:
            case 68: case 69: case 84: case 88: case 96: case 97: case 98: case 99: case 100: case 101: return false;
            default: return true;
            }
        }
        return kind == key_pressure ? address < 128 : kind == nrpn7 || kind == nrpn14 ? address < 16384
            : kind == rpn14 ? address == 1 : address == 0;
    }
    bool same(const Parameter &other) const { return kind == other.kind && channel == other.channel && address == other.address; }
    bool overlaps(const Parameter &other) const {
        if (channel != other.channel) return false;
        if ((kind == cc7 || kind == cc14) && (other.kind == cc7 || other.kind == cc14))
            return address == other.address || (kind == cc14 && address + 32 == other.address)
                || (other.kind == cc14 && other.address + 32 == address);
        if ((kind == nrpn7 || kind == nrpn14) && (other.kind == nrpn7 || other.kind == nrpn14)) return address == other.address;
        return same(other);
    }
    int field(unsigned id, unsigned input) const {
        if (id == 0) return int(type());
        if (id == 1) return channel + 1;
        if (id == 8) return int(input);
        if (id == value_field()) return value;
        if ((id == 4 && (kind == cc7 || kind == cc14)) || (id == 2 && kind == key_pressure)
            || (id == 9 && (kind == nrpn7 || kind == nrpn14 || kind == rpn14))) return address;
        return unknown;
    }
    bool set(unsigned id, int v) {
        if (id == value_field()) value = std::clamp(v, minimum(), maximum());
        else if (id == 1 && v >= 1 && v <= 16) channel = uint8_t(v - 1);
        else if (id == 0) return v == int(type());
        else if (id == 4 || id == 2 || id == 9) {
            if (field(id, 0) == unknown || v < 0 || v > 16383) return false;
            address = uint16_t(v);
        } else return false;
        return valid();
    }
    unsigned messages() const { return kind == cc14 ? 2 : kind == nrpn7 ? 5 : kind == nrpn14 || kind == rpn14 ? 6 : 1; }
    bool decode_messages(const uint8_t *bytes, unsigned size) {
        if (size != 6 && size != 15 && size != 18) return false;
        channel = bytes[0] & 15;
        if (size == 6) {
            kind = cc14; address = bytes[1]; value = bytes[2] * 128 + bytes[5];
        } else {
            kind = size == 15 ? nrpn7 : bytes[1] == 101 ? rpn14 : nrpn14;
            address = uint16_t(bytes[2] * 128 + bytes[5]);
            value = size == 15 ? bytes[8] : bytes[8] * 128 + bytes[11];
        }
        if (!valid() || messages() * 3 != size) return false;
        for (unsigned i = 0; i < messages(); ++i) {
            uint8_t expected[3] = {}; message(i, expected);
            for (unsigned j = 0; j < 3; ++j) if (bytes[i * 3 + j] != expected[j]) return false;
        }
        return true;
    }
    // Return bytes directly so this header does not depend on the transport.
    unsigned message(unsigned index, uint8_t (&bytes)[3]) const {
        bytes[0] = uint8_t((kind == bend ? 0xe0 : kind == pressure ? 0xd0 : kind == key_pressure ? 0xa0 : kind == program ? 0xc0 : 0xb0) | channel);
        bytes[1] = uint8_t(address); bytes[2] = uint8_t(value);
        if (kind == bend) { unsigned raw = unsigned(value + 8192); bytes[1] = uint8_t(raw & 127); bytes[2] = uint8_t(raw >> 7); }
        else if (kind == pressure || kind == program) bytes[1] = uint8_t(value - (kind == program));
        else if (kind == cc14) { bytes[1] = uint8_t(address + index * 32); bytes[2] = uint8_t(index ? value & 127 : value >> 7); }
        else if (kind == nrpn7 || kind == nrpn14 || kind == rpn14) {
            unsigned data_end = kind == nrpn7 ? 3 : 4;
            if (index < 2) { bytes[1] = uint8_t((kind == rpn14 ? 101 : 99) - index); bytes[2] = uint8_t(index ? address & 127 : address >> 7); }
            else if (index < data_end) { bytes[1] = uint8_t(index == 2 ? 6 : 38); bytes[2] = uint8_t(kind == nrpn7 ? value : index == 2 ? value >> 7 : value & 127); }
            else { bytes[1] = uint8_t(index == data_end ? 101 : 100); bytes[2] = 127; }
        }
        return kind == pressure || kind == program ? 2 : 3;
    }
    bool conflicts(const uint8_t *bytes, unsigned size) const {
        if (!size) return false;
        if (bytes[0] == 0xf0 || bytes[0] == 0xff) return true;
        if (bytes[0] >= 0xf0 || (bytes[0] & 15) != channel) return false;
        unsigned type = bytes[0] >> 4;
        if (type == 12) return true; // A new program can invalidate every parameter baseline.
        if (type == 11 && (bytes[1] == 121 || bytes[1] == 0 || bytes[1] == 32)) return true;
        if (type == 11 && (kind == cc7 || kind == cc14))
            return bytes[1] == address || (kind == cc14 && bytes[1] == address + 32);
        if (type == 11 && (kind == nrpn7 || kind == nrpn14 || kind == rpn14))
            return bytes[1] == 6 || bytes[1] == 38 || (bytes[1] >= 96 && bytes[1] <= 101);
        return type == 14 ? kind == bend : type == 13 ? kind == pressure : type == 10 && kind == key_pressure && bytes[1] == address;
    }
};

struct ParameterTransaction {
    Parameter parameter;
    uint64_t token = 0;
    unsigned position = 0;
    uint64_t origin = 0;
};

struct ParameterObserver {
    virtual ~ParameterObserver() = default;
    virtual bool parameter_write(unsigned output, const uint8_t *bytes, unsigned size, const Parameter *logical, uint64_t origin) = 0;
};
} // namespace cntrl::midi
