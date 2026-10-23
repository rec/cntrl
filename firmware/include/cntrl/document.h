#pragma once
#include "hardware.h"
#include <cstring>

namespace cntrl {
inline uint16_t u16(const uint8_t *p) { return p[0] | uint16_t(p[1]) << 8; }
inline uint32_t u32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline void put32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(n >> (8 * i));
}
inline uint32_t checksum(const uint8_t *p, unsigned length) {
    uint32_t crc = 0xffffffff;
    for (unsigned i = 0; i < length; ++i) {
        crc ^= p[i];
        for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
inline bool text_valid(const uint8_t *p, unsigned length) {
    if (!p[0]) return false;
    bool ended = false;
    for (unsigned i = 0; i < length; ++i) {
        if (p[i] == 0) ended = true;
        else if (ended || p[i] < 32 || p[i] > 126) return false;
    }
    return ended;
}

class Document {
public:
    bool load(const uint8_t *bytes, unsigned length, const Hardware &hardware) {
        if (length < 24 || length > max_document || std::memcmp(bytes, "CSW1", 4)
            || u16(bytes + 4) != 2 || u16(bytes + 6) < 1
            || u32(bytes + 8) != length || u16(bytes + 18) > 255
            || checksum(bytes + 24, length - 24) != u32(bytes + 20)) return false;
        unsigned ni = u16(bytes + 12), nc = u16(bytes + 14), np = u16(bytes + 16);
        if (!ni || ni > max_instruments || !nc || nc > max_categories || !np
            || 24 + ni * 68 + nc * 68 + np * 76 != length) return false;
        for (unsigned i = 0; i < ni; ++i) {
            const uint8_t *p = bytes + 24 + i * 68;
            if (!text_valid(p, 32) || !text_valid(p + 32, 32) || p[64] > 15
                || p[65] || p[66] || p[67]
                || !hardware.supports(reinterpret_cast<const char *>(p + 32))) return false;
            for (unsigned j = 0; j < i; ++j)
                if (!std::strcmp(reinterpret_cast<const char *>(p),
                                 reinterpret_cast<const char *>(bytes + 24 + j * 68))) return false;
        }
        unsigned next = 0;
        for (unsigned c = 0; c < nc; ++c) {
            const uint8_t *p = bytes + 24 + ni * 68 + c * 68;
            unsigned start = u16(p + 64), count = u16(p + 66);
            if (!text_valid(p, 64) || !count || start != next || start + count > np) return false;
            for (unsigned j = start; j < start + count; ++j) {
                const uint8_t *q = bytes + 24 + ni * 68 + nc * 68 + j * 76;
                if (u16(q) >= ni || u16(q + 2) != c || q[4] > 127
                    || q[9] || q[10] || q[11] || !text_valid(q + 12, 64)) return false;
                for (unsigned k = 5; k < 9; ++k) if (q[k] > 127 && q[k] != 255) return false;
            }
            next += count;
        }
        if (next != np) return false;
        data_ = bytes; length_ = length; instruments_ = ni; categories_ = nc; patches_ = np;
        return true;
    }
    bool valid() const { return data_ != nullptr; }
    unsigned length() const { return length_; }
    const uint8_t *data() const { return data_; }
    unsigned categories() const { return categories_; }
    unsigned instruments() const { return instruments_; }
    unsigned patches() const { return patches_; }
    unsigned chord_ms() const { return u16(data_ + 6); }
    uint8_t brightness() const { return data_[18]; }
    const uint8_t *instrument(unsigned i) const { return data_ + 24 + i * 68; }
    const uint8_t *category(unsigned i) const { return data_ + 24 + instruments_ * 68 + i * 68; }
    const uint8_t *patch(unsigned i) const { return data_ + 24 + instruments_ * 68 + categories_ * 68 + i * 76; }
private:
    const uint8_t *data_ = nullptr;
    unsigned length_ = 0, instruments_ = 0, categories_ = 0, patches_ = 0;
};
} // namespace cntrl
