#pragma once
#include "hardware.h"
#include <cstring>
#include <cstdio>

namespace cntrl {
constexpr unsigned metadata_header_size = 8;
constexpr unsigned document_version = 12;
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
            || u16(bytes + 4) != document_version || u16(bytes + 6) < 1
            || u32(bytes + 8) != length || bytes[19] > 3 || u32(bytes + 24) > 2147483000u
            || checksum(bytes + 28, length - 28) != u32(bytes + 20)) return false;
        unsigned ni = u16(bytes + 12), nc = u16(bytes + 14), np = u16(bytes + 16);
        unsigned meta = 76 + ni * 68 + nc * 68 + np * 82;
        if (!ni || ni > max_instruments || !nc || nc > max_categories || !np
            || meta + metadata_header_size + ni * 64 + np * 4 > length
            || std::memcmp(bytes + meta, "CMTA", 4)) return false;
        unsigned nb = u16(bytes + meta + 4);
        unsigned names = meta + metadata_header_size + ni * 64 + nb * 8 + np * 4;
        if (names + 4 > length || std::memcmp(bytes + names, "CNAM", 4)) return false;
        unsigned end = names + 4;
        for (unsigned i = 0; i < np * 3; ++i) {
            if (end + 2 > length) return false;
            unsigned size = u16(bytes + end); end += 2;
            if (size == 65535) continue;
            if (end + size > length) return false;
            end += size;
        }
        if (end + 16 > length || std::memcmp(bytes + end, "CDSP", 4)) return false;
        unsigned styles = end + 16, ns = u16(bytes + end + 4), pages = u16(bytes + end + 6);
        unsigned nt = u16(bytes + end + 8), page_bytes = u16(bytes + end + 10);
        unsigned page_start = styles + ns * 16, texts = page_start + page_bytes;
        if (!pages || texts > length) return false;
        for (unsigned i = 0; i < ns; ++i)
            if (!graphics_valid(bytes + styles + i * 16, hardware)) return false;
        unsigned cursor = page_start;
        for (unsigned i = 0; i < pages; ++i) {
            if (cursor >= texts || bytes[cursor] > max_lines) return false;
            unsigned count = bytes[cursor++];
            if (cursor + count * 8 > texts) return false;
            for (unsigned j = 0; j < count; ++j, cursor += 8) {
                const uint8_t *p = bytes + cursor;
                if ((p[0] > 7 && (p[0] < 16 || p[0] > 21) && p[0] != 255)
                    || p[1] > 1 || u16(p + 2) >= ns || u16(p + 6) >= nt) return false;
                const uint8_t *g = bytes + styles + u16(p + 2) * 16;
                if (u16(p + 4) < g[2] || u16(p + 4) >= hardware.display_width() - g[2]) return false;
            }
        }
        if (cursor != texts) return false;
        for (unsigned i = 0; i < nt; ++i) {
            if (cursor + 2 > length) return false;
            unsigned size = u16(bytes + cursor); cursor += 2;
            if (!size || size > 64 || cursor + size > length || !text_valid(bytes + cursor, size)) return false;
            cursor += size;
        }
        if (cursor + u32(bytes + end + 12) != length) return false;
        for (unsigned i = 0; i < ni; ++i)
            if (!text_valid(bytes + meta + metadata_header_size + i * 64, 32)
                || !text_valid(bytes + meta + metadata_header_size + 32 + i * 64, 32)) return false;
        for (unsigned b = 0; b < nb; ++b) {
            const uint8_t *m = bytes + meta + metadata_header_size + ni * 64 + b * 8;
            if (u16(m) >= ni || u32(m + 2) == 0xffffffffu
                || (m[6] > 127 && m[6] != 255) || (m[7] > 127 && m[7] != 255)
                || (m[6] == 255 && m[7] == 255)) return false;
            for (unsigned j = 0; j < b; ++j) {
                const uint8_t *other = bytes + meta + metadata_header_size + ni * 64 + j * 8;
                if (u16(m) == u16(other) && u32(m + 2) == u32(other + 2)) return false;
            }
        }
        for (unsigned s = 0; s < 3; ++s) {
            const uint8_t *p = bytes + 28 + s * 16;
            if (!graphics_valid(p, hardware)) return false;
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
            const uint8_t *p = bytes + 76 + ni * 68 + c * 68;
            unsigned start = u16(p + 64), count = u16(p + 66);
            if (!text_valid(p, 64) || !count || start != next || start + count > np) return false;
            for (unsigned j = start; j < start + count; ++j) {
                const uint8_t *q = bytes + 76 + ni * 68 + nc * 68 + j * 82;
                if (u16(q) >= ni || u16(q + 2) != c || q[4] > 127
                    || q[9] || q[10] || q[11] || !text_valid(q + 12, 64)) return false;
                for (unsigned k = 5; k < 9; ++k) if (q[k] > 127 && q[k] != 255) return false;
                uint32_t bank = u32(bytes + meta + metadata_header_size + ni * 64 + nb * 8 + j * 4);
                bool mapped = bank == 0xffffffffu && q[5] == 255 && q[6] == 255;
                for (unsigned b = 0; b < nb; ++b) {
                    const uint8_t *m = bytes + meta + metadata_header_size + ni * 64 + b * 8;
                    if (u16(m) == u16(q) && u32(m + 2) == bank && m[6] == q[5] && m[7] == q[6]) mapped = true;
                }
                if (!mapped) return false;
                for (unsigned s = 0; s < 3; ++s) {
                    unsigned page = u16(q + 76 + s * 2);
                    if (page >= pages) return false;
                    const uint8_t *entry = bytes + page_start;
                    while (page--) entry += 1 + entry[0] * 8;
                    unsigned count = *entry++, height = 0;
                    for (unsigned k = 0; k < count; ++k, entry += 8) {
                        const uint8_t *g = bytes + styles + u16(entry + 2) * 16;
                        if (g[2] != bytes[30 + s * 16]) return false;
                        height += u16(g + 8);
                    }
                    if (height + 2u * bytes[30 + s * 16] > hardware.display_height()) return false;
                }
            }
            next += count;
        }
        if (next != np) return false;
        data_ = bytes; length_ = length; instruments_ = ni; categories_ = nc; patches_ = np;
        styles_ = styles; pages_ = page_start; texts_ = texts;
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
        return read_graphics(p);
    }
    void page(Pane &view, unsigned index, unsigned slot, bool already_sent) const {
        unsigned page = u16(patch(index) + 76 + slot * 2);
        const uint8_t *p = data_ + pages_;
        while (page--) p += 1 + p[0] * 8;
        unsigned count = *p++;
        for (unsigned i = 0; i < count; ++i, p += 8) {
            TextLine &line = view.lines[i];
            line.graphics = read_graphics(data_ + styles_ + u16(p + 2) * 16);
            line.x = u16(p + 4);
            line.centered = (p[0] >= 2 && p[0] <= 5) || (p[0] >= 16 && p[0] <= 21);
            line.stippled = already_sent && p[1];
            field_text(line.text, sizeof(line.text), p[0], index, u16(p + 6));
        }
    }
    const uint8_t *instrument(unsigned i) const { return data_ + 76 + i * 68; }
    const uint8_t *category(unsigned i) const { return data_ + 76 + instruments_ * 68 + i * 68; }
    const uint8_t *patch(unsigned i) const { return data_ + 76 + instruments_ * 68 + categories_ * 68 + i * 82; }
    unsigned metadata() const { return 76 + instruments_ * 68 + categories_ * 68 + patches_ * 82; }
    unsigned mappings() const { return u16(data_ + metadata() + 4); }
    const uint8_t *mapping(unsigned i) const { return data_ + metadata() + metadata_header_size + instruments_ * 64 + i * 8; }
    const uint8_t *bank(unsigned i) const { return mapping(mappings()) + i * 4; }
private:
    static bool graphics_valid(const uint8_t *p, const Hardware &hardware) {
        return hardware.supports_font(p[0], p[1]) && !p[3] && !u32(p + 12)
            && 2u * p[2] < hardware.display_width()
            && 2u * p[2] + u16(p + 8) <= hardware.display_height()
            && u16(p + 8) && u16(p + 10) < u16(p + 8);
    }
    static ScreenGraphics read_graphics(const uint8_t *p) {
        return {p[0], p[1], p[2], 0, u16(p + 4), u16(p + 6), u16(p + 8), u16(p + 10)};
    }
    void field_text(char *text, unsigned size, unsigned code, unsigned index, unsigned literal) const {
        const uint8_t *p = patch(index);
        const uint8_t *source = data_ + texts_;
        while (literal--) source += 2 + u16(source);
        source += 2;
        switch (code) {
        case 0: source = category(u16(p + 2)); break;
        case 1: source = p + 12; break;
        case 6: source = instrument(u16(p)); break;
        case 2: std::snprintf(text, size, "%u", unsigned(p[4]) + 1); return;
        case 3: if (u32(bank(index)) == 0xffffffffu) return; std::snprintf(text, size, "%lu", (unsigned long)u32(bank(index))); return;
        case 4: if (p[7] == 255) return; std::snprintf(text, size, "%u", unsigned(p[7])); return;
        case 5: if (p[8] == 255) return; std::snprintf(text, size, "%d", int(p[8]) - 64); return;
        case 16: std::snprintf(text, size, "%u", unsigned(brightness())); return;
        case 17: case 18: std::snprintf(text, size, "%s", (code == 17 ? splash() : usb_midi_interface()) ? "true" : "false"); return;
        case 19: std::snprintf(text, size, "%lu", (unsigned long)(screensaver_timeout() / 1000)); return;
        case 20: std::snprintf(text, size, "%u", chord_ms()); return;
        case 21: if (!transition_ms()) return; std::snprintf(text, size, "%u", *transition_ms()); return;
        }
        std::snprintf(text, size, "%s", reinterpret_cast<const char *>(source));
    }
    const uint8_t *data_ = nullptr;
    unsigned length_ = 0, instruments_ = 0, categories_ = 0, patches_ = 0;
    unsigned styles_ = 0, pages_ = 0, texts_ = 0;
};
} // namespace cntrl
