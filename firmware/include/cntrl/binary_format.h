#pragma once
#include <cstdint>
#include <cstring>

namespace cntrl::binary {
constexpr uint16_t development_format = 0x8000u | 44u;
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
#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(persistent_open)
#endif
inline bool open(const uint8_t *data, unsigned length, Kind kind, View &view) {
    view = {};
    if (!enveloped(data, length)) { view.length = length; return true; }
    if (length < header_size || read16(data + 4) != development_format
        || read16(data + 6) != unsigned(kind) || read16(data + 8) != header_size || read16(data + 10)
        || read32(data + 12) != length - header_size
        || length - header_size > maximum_payload || crc(data, length, true) != read32(data + 16)) return false;
    unsigned cursor = header_size, records = 0;
    bool core = false;
    while (cursor < length) {
        if (++records > 4096) return false;
        if (length - cursor < record_size) return false;
        unsigned tag = read16(data + cursor), flags = read16(data + cursor + 2), size = read32(data + cursor + 4);
        cursor += record_size;
        if (!tag || (flags & ~1u) || size > length - cursor) return false;
        if (tag == 1) {
            if (core || flags != 1 || cursor != header_size + record_size) return false;
            view.offset = cursor; view.length = size; core = true;
        } else {
            if (flags & 1) return false;
            view.opaque = true;
        }
        cursor += size;
    }
    return core;
}
inline void seal(uint8_t *data, unsigned length) {
    if (enveloped(data, length) && length >= header_size) write32(data + 16, crc(data, length, true));
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
