#pragma once
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace cntrl::binary {
constexpr uint16_t development_format = 0x8000u | 46u;
constexpr bool supported_format(uint16_t format) { return format == development_format || format == (0x8000u | 44u) || format == (0x8000u | 45u); }
enum class Kind : uint16_t { setup = 1, bundle = 2, songs = 3 };
constexpr unsigned header_size = 20, record_size = 8, maximum_payload = 16u * 1024u * 1024u;
inline uint16_t read16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
inline uint32_t read32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline void write32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(n >> (i * 8));
}
#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(persistent_crc)
#endif
inline uint32_t crc(const uint8_t *data, unsigned length, bool envelope = false) {
    uint32_t value = 0xffffffffu;
    for (unsigned i = 0; i < length; ++i) {
        value ^= envelope && i >= 16 && i < 20 ? 0 : data[i];
        for (unsigned b = 0; b < 8; ++b) value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1)));
    }
    return ~value;
}
inline bool enveloped(const uint8_t *data, unsigned length) {
    return length >= 4 && !std::memcmp(data, "CNBF", 4);
}
struct View {
    unsigned offset = 0, length = 0;
    bool opaque = false;
};
inline bool prefix(uint8_t (&bytes)[28], Kind kind, unsigned core_length, unsigned extensions = 0) {
    if (core_length > maximum_payload - record_size || extensions > maximum_payload - record_size - core_length) return false;
    std::memset(bytes, 0, sizeof(bytes)); std::memcpy(bytes, "CNBF", 4);
    bytes[4] = uint8_t(development_format & 0xff); bytes[5] = uint8_t(development_format >> 8);
    bytes[6] = uint8_t(kind); bytes[8] = header_size;
    write32(bytes + 12, record_size + core_length + extensions);
    bytes[20] = 1; bytes[22] = 1; write32(bytes + 24, core_length); return true;
}
// A bounded inspection step reads at most 512 bytes and allocates nothing.
// The same reader is used for large files and small in-memory documents.
class Inspector {
public:
    Inspector(unsigned length = 0, Kind kind = Kind::setup, bool wire_checksum = false)
        : length_(length), kind_(kind), wire_(wire_checksum) {}
    bool done() const { return phase_ == Phase::done; }
    const View &view() const { return view_; }
    uint32_t checksum() const { return ~wire_crc_; }
    template<class Reader>
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(persistent_inspect)
#endif
    bool step(Reader &reader) {
        uint8_t bytes[512];
        if (phase_ == Phase::done) return true;
        if (phase_ == Phase::header) {
            if (length_ > maximum_payload + header_size) return false;
            unsigned n = std::min(length_, header_size);
            if (n && !reader.read(0, bytes, n)) return false;
            envelope_ = enveloped(bytes, n); view_.length = length_;
            if (envelope_) {
                if (n != header_size || !supported_format(read16(bytes + 4))
                    || read16(bytes + 6) != unsigned(kind_) || read16(bytes + 8) != header_size || read16(bytes + 10)
                    || read32(bytes + 12) != length_ - header_size) return false;
                expected_ = read32(bytes + 16);
            }
            phase_ = !envelope_ && !wire_ ? Phase::done : Phase::hash; return true;
        }
        if (phase_ == Phase::hash) {
            unsigned n = std::min(512u, length_ - position_);
            if (n && !reader.read(position_, bytes, n)) return false;
            for (unsigned i = 0; i < n; ++i) {
                if (wire_) update(wire_crc_, bytes[i]);
                update(envelope_crc_, envelope_ && position_ + i >= 16 && position_ + i < 20 ? 0 : bytes[i]);
            }
            position_ += n;
            if (position_ == length_) {
                if (envelope_ && ~envelope_crc_ != expected_) return false;
                phase_ = envelope_ ? Phase::records : Phase::done; position_ = header_size;
            }
            return true;
        }
        if (position_ == length_) { phase_ = Phase::done; return records_ != 0; }
        if (++records_ > 4096 || length_ - position_ < record_size || !reader.read(position_, bytes, record_size)) return false;
        unsigned tag = read16(bytes), flags = read16(bytes + 2), n = read32(bytes + 4);
        position_ += record_size;
        if (!tag || flags & ~1u || n > length_ - position_) return false;
        if (records_ == 1) {
            if (tag != 1 || flags != 1) return false;
            view_.offset = position_; view_.length = n;
        } else {
            if (tag == 1 || flags) return false;
            view_.opaque = true;
        }
        position_ += n; return true;
    }
private:
    enum class Phase { header, hash, records, done };
    Phase phase_ = Phase::header;
    unsigned length_, position_ = 0, records_ = 0;
    Kind kind_;
    View view_;
    uint32_t wire_crc_ = 0xffffffffu, envelope_crc_ = 0xffffffffu, expected_ = 0;
    bool envelope_ = false, wire_ = false;
    static void update(uint32_t &crc, uint8_t byte) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
};
template<class Reader>
#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(persistent_open_reader)
#endif
inline bool open_reader(Reader &reader, unsigned length, Kind kind, View &view, uint32_t *checksum = nullptr) {
    Inspector inspector(length, kind, checksum != nullptr);
    while (!inspector.done()) if (!inspector.step(reader)) return false;
    view = inspector.view(); if (checksum) *checksum = inspector.checksum(); return true;
}
#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(persistent_open)
#endif
inline bool open(const uint8_t *data, unsigned length, Kind kind, View &view) {
    struct MemoryReader {
        const uint8_t *data;
        bool read(unsigned offset, uint8_t *destination, unsigned n) {
            std::memcpy(destination, data + offset, n); return true;
        }
    } reader{data};
    view = {}; return open_reader(reader, length, kind, view);
}
inline void seal(uint8_t *data, unsigned length) {
    if (enveloped(data, length) && length >= header_size) {
        data[4] = uint8_t(development_format & 0xff); data[5] = uint8_t(development_format >> 8);
        write32(data + 16, crc(data, length, true));
    }
}
#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(persistent_seal_setup)
#endif
inline bool seal_setup(uint8_t *data, unsigned length) {
    unsigned offset = enveloped(data, length) ? header_size + record_size : 0;
    if (offset > length || length - offset < 28) return false;
    unsigned core_length = offset ? read32(data + header_size + 4) : length;
    if (core_length < 28 || core_length > length - offset) return false;
    write32(data + offset + 20, crc(data + offset + 28, core_length - 28));
    seal(data, length); return true;
}
} // namespace cntrl::binary
