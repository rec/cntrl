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
        if (length < 76 || length > max_document || std::memcmp(bytes, "CSW1", 4)
            || u16(bytes + 4) != 10 || u16(bytes + 6) < 1
            || u32(bytes + 8) != length || bytes[19] > 7 || u32(bytes + 24) > 2147483000u
            || checksum(bytes + 28, length - 28) != u32(bytes + 20)) return false;
        unsigned ni = u16(bytes + 12), nc = u16(bytes + 14), np = u16(bytes + 16);
        unsigned meta = 76 + ni * 68 + nc * 74 + np * 82;
        if (!ni || ni > max_instruments || !nc || nc > max_categories || !np
            || meta + 40 + ni * 64 + np * 4 > length
            || std::memcmp(bytes + meta, "CMTA", 4)
            || !text_valid(bytes + meta + 8, 32)) return false;
        unsigned nb = u16(bytes + meta + 4);
        unsigned names = meta + 40 + ni * 64 + nb * 8 + np * 4;
        if (names + 4 > length || std::memcmp(bytes + names, "CNAM", 4)) return false;
        unsigned end = names + 4;
        for (unsigned i = 0; i < np * 3; ++i) {
            if (end + 2 > length) return false;
            unsigned size = u16(bytes + end); end += 2;
            if (size == 65535) continue;
            if (end + size > length) return false;
            end += size;
        }
        unsigned layouts = 0;
        if (bytes[19] & 4) {
            if (end + 4 + (nc + np) * 8 != length || std::memcmp(bytes + end, "CFNT", 4)) return false;
            layouts = end + 4;
            unsigned pad = bytes[30];
            if (hardware.display_height() <= 2 * pad + 2) return false;
            unsigned row = (hardware.display_height() - 2 * pad - 2) / 2;
            for (unsigned i = 0; i < nc + np; ++i) {
                const uint8_t *p = bytes + layouts + i * 8;
                if (!hardware.supports_font(bytes[28], p[0]) || p[1]
                    || !u16(p + 4) || u16(p + 4) > row || u16(p + 2) >= u16(p + 4)
                    || u16(p + 6) != (i < nc ? pad : pad + row + 2)) return false;
            }
        } else if (end != length) return false;
        for (unsigned i = 0; i < ni; ++i)
            if (!text_valid(bytes + meta + 40 + i * 64, 32)
                || !text_valid(bytes + meta + 72 + i * 64, 32)) return false;
        for (unsigned b = 0; b < nb; ++b) {
            const uint8_t *m = bytes + meta + 40 + ni * 64 + b * 8;
            if (u16(m) >= ni || u32(m + 2) == 0xffffffffu
                || (m[6] > 127 && m[6] != 255) || (m[7] > 127 && m[7] != 255)
                || (m[6] == 255 && m[7] == 255)) return false;
            for (unsigned j = 0; j < b; ++j) {
                const uint8_t *other = bytes + meta + 40 + ni * 64 + j * 8;
                if (u16(m) == u16(other) && u32(m + 2) == u32(other + 2)) return false;
            }
        }
        for (unsigned s = 0; s < 3; ++s) {
            const uint8_t *p = bytes + 28 + s * 16;
            if (!hardware.supports_font(p[0], p[1]) || p[0] != bytes[28] || p[1] != bytes[29] || p[2] != bytes[30] || p[3] || u16(p + 14)
                || 2u * p[2] >= hardware.display_width()
                || 2u * p[2] + 2u * u16(p + 8) + 2 > hardware.display_height()
                || !u16(p + 8) || u16(p + 10) >= u16(p + 8)) return false;
        }
        for (unsigned i = 0; i < ni; ++i) {
            const uint8_t *p = bytes + 76 + i * 68;
            if (!text_valid(p, 32) || !text_valid(p + 32, 32) || p[64] > 15
                || p[65] || p[66] || p[67]
                || !hardware.supports(reinterpret_cast<const char *>(p + 32))) return false;
            for (unsigned j = 0; j < i; ++j)
                if (!std::strcmp(reinterpret_cast<const char *>(p),
                                 reinterpret_cast<const char *>(bytes + 76 + j * 68))) return false;
        }
        unsigned next = 0;
        for (unsigned c = 0; c < nc; ++c) {
            const uint8_t *p = bytes + 76 + ni * 68 + c * 74;
            unsigned start = u16(p + 64), count = u16(p + 66);
            if (!text_valid(p, 64) || !count || start != next || start + count > np) return false;
            for (unsigned s = 0; s < 3; ++s)
                if (u16(p + 68 + s * 2) < bytes[30 + s * 16]
                    || u16(p + 68 + s * 2) >= hardware.display_width() - bytes[30 + s * 16]) return false;
            for (unsigned j = start; j < start + count; ++j) {
                const uint8_t *q = bytes + 76 + ni * 68 + nc * 74 + j * 82;
                if (u16(q) >= ni || u16(q + 2) != c || q[4] > 127
                    || q[9] || q[10] || q[11] || !text_valid(q + 12, 64)) return false;
                for (unsigned k = 5; k < 9; ++k) if (q[k] > 127 && q[k] != 255) return false;
                uint32_t bank = u32(bytes + meta + 40 + ni * 64 + nb * 8 + j * 4);
                bool mapped = bank == 0xffffffffu && q[5] == 255 && q[6] == 255;
                for (unsigned b = 0; b < nb; ++b) {
                    const uint8_t *m = bytes + meta + 40 + ni * 64 + b * 8;
                    if (u16(m) == u16(q) && u32(m + 2) == bank && m[6] == q[5] && m[7] == q[6]) mapped = true;
                }
                if (!mapped) return false;
                for (unsigned s = 0; s < 3; ++s)
                    if (u16(q + 76 + s * 2) < bytes[30 + s * 16]
                        || u16(q + 76 + s * 2) >= hardware.display_width() - bytes[30 + s * 16]) return false;
            }
            next += count;
        }
        if (next != np) return false;
        data_ = bytes; length_ = length; instruments_ = ni; categories_ = nc; patches_ = np;
        layouts_ = layouts;
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
    bool splash() const { return data_[19] & 1; }
    bool usb_midi_interface() const { return data_[19] & 2; }
    uint32_t screensaver_timeout() const { return u32(data_ + 24); }
    std::optional<unsigned> transition_ms() const {
        unsigned value = u16(data_ + metadata() + 6);
        return value ? std::optional<unsigned>(value) : std::nullopt;
    }
    ScreenGraphics graphics(unsigned slot) const {
        const uint8_t *p = data_ + 28 + slot * 16;
        return {p[0], p[1], p[2], 0, u16(p + 4), u16(p + 6), u16(p + 8), u16(p + 10), u16(p + 12)};
    }
    Pane::LineLayout line(unsigned index) const {
        if (!layouts_) return {};
        const uint8_t *p = data_ + layouts_ + index * 8;
        return {p[0], u16(p + 2), u16(p + 4), u16(p + 6)};
    }
    const uint8_t *instrument(unsigned i) const { return data_ + 76 + i * 68; }
    const uint8_t *category(unsigned i) const { return data_ + 76 + instruments_ * 68 + i * 74; }
    const uint8_t *patch(unsigned i) const { return data_ + 76 + instruments_ * 68 + categories_ * 74 + i * 82; }
    unsigned metadata() const { return 76 + instruments_ * 68 + categories_ * 74 + patches_ * 82; }
    unsigned mappings() const { return u16(data_ + metadata() + 4); }
    const uint8_t *mapping(unsigned i) const { return data_ + metadata() + 40 + instruments_ * 64 + i * 8; }
    const uint8_t *bank(unsigned i) const { return mapping(mappings()) + i * 4; }
private:
    const uint8_t *data_ = nullptr;
    unsigned length_ = 0, instruments_ = 0, categories_ = 0, patches_ = 0, layouts_ = 0;
};
} // namespace cntrl
