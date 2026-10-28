#pragma once
#include "hardware.h"
#include "midi_bridge.h"
#include <cstring>
#include <cstdio>

namespace cntrl {
constexpr unsigned metadata_header_size = 8;
constexpr unsigned document_version = 17;
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

inline bool utf8_valid(const uint8_t *p, unsigned length) {
    for (unsigned i = 0; i < length;) {
        uint32_t c = p[i++];
        if (c < 128) { if (c < 32 || c == 127) return false; continue; }
        unsigned n; uint32_t minimum;
        if (c >= 0xc2 && c <= 0xdf) { n = 1; c &= 31; minimum = 128; }
        else if (c >= 0xe0 && c <= 0xef) { n = 2; c &= 15; minimum = 2048; }
        else if (c >= 0xf0 && c <= 0xf4) { n = 3; c &= 7; minimum = 65536; }
        else return false;
        if (n > length - i) return false;
        while (n--) { unsigned b = p[i++]; if ((b & 0xc0) != 0x80) return false; c = (c << 6) | (b & 63); }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
    }
    return true;
}
inline bool label_valid(const uint8_t *p, unsigned length) {
    unsigned end = 0;
    while (end < length && p[end]) ++end;
    if (!end || end == length || !utf8_valid(p, end)) return false;
    for (unsigned i = end; i < length; ++i) if (p[i]) return false;
    return true;
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
        if (ni > max_instruments || nc > max_categories || bool(nc) != bool(np)
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
        unsigned graph = end;
        if (!graph_valid(bytes, length, graph, ni, np, meta, nb, hardware)) return false;
        unsigned switch_count = u16(bytes + graph + 10);
        unsigned switch_start = graph + 16 + u16(bytes + graph + 8) * 4 + np * 2;
        end += u32(bytes + graph + 4);
        if (end + 16 > length || std::memcmp(bytes + end, "CDSP", 4)) return false;
        unsigned styles = end + 16, ns = u16(bytes + end + 4), pages = u16(bytes + end + 6);
        unsigned nt = u16(bytes + end + 8), page_bytes = u16(bytes + end + 10);
        unsigned page_start = styles + ns * 16, texts = page_start + page_bytes;
        if (!pages || texts > length) return false;
        for (unsigned i = 0; i < switch_count; ++i) if (u16(bytes + switch_start + i * 72 + 70) >= pages) return false;
        for (unsigned i = 0; i < ns; ++i)
            if (!graphics_valid(bytes + styles + i * 16, hardware)) return false;
        unsigned cursor = page_start;
        for (unsigned i = 0; i < pages; ++i) {
            if (cursor >= texts || bytes[cursor] > max_lines) return false;
            unsigned count = bytes[cursor++];
            if (cursor + count * 8 > texts) return false;
            for (unsigned j = 0; j < count; ++j, cursor += 8) {
                const uint8_t *p = bytes + cursor;
                if ((p[0] > 7 && (p[0] < 16 || p[0] > 22) && p[0] != 255)
                    || p[1] > 1 || u16(p + 2) >= ns || u16(p + 6) >= nt) return false;
                const uint8_t *g = bytes + styles + u16(p + 2) * 16;
                if (u16(p + 4) < g[2] || u16(p + 4) >= hardware.display_width() - g[2]) return false;
            }
        }
        if (cursor != texts) return false;
        for (unsigned i = 0; i < nt; ++i) {
            if (cursor + 2 > length) return false;
            unsigned size = u16(bytes + cursor); cursor += 2;
            if (!size || size > 64 || cursor + size > length || !label_valid(bytes + cursor, size)) return false;
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
            if (!label_valid(p, 64) || !count || start != next || start + count > np) return false;
            for (unsigned j = start; j < start + count; ++j) {
                const uint8_t *q = bytes + 76 + ni * 68 + nc * 68 + j * 82;
                bool function = u16(q) == 65535;
                if (function ? ((q[9] && (!hardware.supports_function(q[9]) || q[9] != 1 || !button_pair_valid(q[10]))) || (!q[9] && q[10]) || q[11] || q[4] || q[5] != 255 || q[6] != 255 || q[7] != 255 || q[8] != 255 || u16(q + 2) != c || !label_valid(q + 12, 64)) : (u16(q) >= ni || u16(q + 2) != c || q[4] > 127
                    || q[9] || q[10] || q[11] || !label_valid(q + 12, 64))) return false;
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
        graph_ = graph;
        return true;
    }
    bool valid() const { return data_ != nullptr; }
    unsigned switches() const { return u16(data_ + graph_ + 10); }
    const uint8_t *switch_assignment(unsigned i) const { return data_ + graph_ + 16 + nodes() * 4 + patches_ * 2 + i * 72; }
    unsigned nodes() const { return u16(data_ + graph_ + 8); }
    unsigned graph_metadata() const { return graph_; }
    const uint8_t *node(unsigned i) const { return data_ + graph_ + u32(data_ + graph_ + 16 + i * 4); }
    unsigned browser_root(unsigned i) const { return u16(data_ + graph_ + 16 + nodes() * 4 + i * 2); }
    unsigned named_patches() const { return u16(data_ + graph_ + 14); }
    const uint8_t *named_patch(unsigned i) const { return switch_assignment(switches()) + i * 66; }
    int named_patch(const char *key) const {
        for (unsigned i = 0; i < named_patches(); ++i)
            if (!std::strcmp(key, reinterpret_cast<const char *>(named_patch(i)))) return int(u16(named_patch(i) + 64));
        return -1;
    }
    unsigned display_metadata() const { return graph_ + u32(data_ + graph_ + 4); }
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
    bool prepare_fonts(Hardware &hardware) const {
        hardware.begin_fonts();
        for (unsigned i = 0; i < 3u + u16(data_ + styles_ - 12); ++i) {
            const uint8_t *p = i < 3 ? data_ + 28 + i * 16 : data_ + styles_ + (i - 3) * 16;
            if (p[0] == 255 && !hardware.stage_font(u32(p + 12), p[1])) {
                hardware.finish_fonts(false); return false;
            }
        }
        return true;
    }
    void page(Pane &view, unsigned index, unsigned slot, bool already_sent) const {
        unsigned page = u16(patch(index) + 76 + slot * 2);
        page_record(view, page, index, already_sent);
    }
    void page_record(Pane &view, unsigned page, unsigned index, bool already_sent, const char *state = "") const {
        const uint8_t *p = data_ + pages_;
        while (page--) p += 1 + p[0] * 8;
        unsigned count = *p++;
        for (unsigned i = 0; i < count; ++i, p += 8) {
            TextLine &line = view.lines[i];
            line.graphics = read_graphics(data_ + styles_ + u16(p + 2) * 16);
            line.x = u16(p + 4);
            line.centered = (p[0] >= 2 && p[0] <= 5) || (p[0] >= 16 && p[0] <= 22);
            line.stippled = already_sent && p[1];
            if (p[0] == 22) std::snprintf(line.text, sizeof(line.text), "%s", state);
            else field_text(line.text, sizeof(line.text), p[0], index, u16(p + 6));
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
    static bool graph_valid(const uint8_t *bytes, unsigned length, unsigned start,
                            unsigned instruments, unsigned browser_count, unsigned metadata,
                            unsigned mappings, const Hardware &hardware) {
        if (start > length || length - start < 16 || std::memcmp(bytes + start, "CPAT", 4)) return false;
        const uint8_t *g = bytes + start;
        unsigned size = u32(g + 4), count = u16(g + 8), switches = u16(g + 10), named = u16(g + 14);
        unsigned browser = 16 + count * 4, assignments = browser + browser_count * 2;
        unsigned names = assignments + switches * 72, first = names + named * 66;
        if (size > length - start || size < first || u16(g + 12) != browser_count
            || switches > hardware.switch_capacity()) return false;
        for (unsigned i = 0; i < browser_count; ++i) if (u16(g + browser + i * 2) >= count) return false;
        unsigned buttons = 0;
        for (unsigned i = 0; i < switches; ++i) {
            const uint8_t *s = g + assignments + i * 72;
            if (s[0] >= 5 || s[1] > 2 || u16(s + 2) >= count || (buttons & (1u << s[0]))
                || !label_valid(s + 6, 64) || (s[1] ? u16(s + 4) >= count : u16(s + 4) != 65535)) return false;
            buttons |= 1u << s[0];
        }
        for (unsigned i = 0; i < named; ++i) {
            const uint8_t *n = g + names + i * 66;
            if (!text_valid(n, 64) || u16(n + 64) >= count) return false;
            for (unsigned j = 0; j < i; ++j) if (!std::strcmp(reinterpret_cast<const char *>(n),
                reinterpret_cast<const char *>(g + names + j * 66))) return false;
        }
        for (unsigned i = 0; i < count; ++i) {
            unsigned offset = u32(g + 16 + i * 4);
            if (offset != first || offset > size || size - offset < 4) return false;
            const uint8_t *n = g + offset, *p = n + 4;
            unsigned length = u16(n + 2);
            if (length > size - offset - 4) return false;
            first = offset + 4 + length;
            switch (n[0]) {
            case 1:
                if (n[1] || length != 11 || u16(p) >= instruments || p[2] > 127) return false;
                for (unsigned j = 3; j < 7; ++j) if (p[j] > 127 && p[j] != 255) return false;
                {
                    uint32_t bank = u32(p + 7);
                    bool mapped = bank == 0xffffffffu && p[3] == 255 && p[4] == 255;
                    for (unsigned j = 0; j < mappings; ++j) {
                        const uint8_t *m = bytes + metadata + metadata_header_size + instruments * 64 + j * 8;
                        if (u16(m) == u16(p) && u32(m + 2) == bank && m[6] == p[3] && m[7] == p[4]) mapped = true;
                    }
                    if (!mapped) return false;
                }
                break;
            case 2:
                if ((n[1] != 0 && n[1] != 1 && n[1] != 2 && n[1] != 4 && n[1] != 9)
                    || length <= 32 || !text_valid(p, 32)
                    || !hardware.supports(reinterpret_cast<const char *>(p))
                    || !(n[1] == 4 ? midi::stream_valid(p + 32, length - 32) : midi::message_valid(p + 32, length - 32))) return false;
                if (n[1] == 0 && (length != 35 || p[32] >> 4 != 11)) return false;
                if (n[1] == 2 && (length != 35 || p[32] >> 4 != 14)) return false;
                if (n[1] == 9 && p[32] != 0xf0) return false;
                if (n[1] == 4) {
                    if (length != 44 || p[32] >> 4 != 11) return false;
                    constexpr unsigned controllers[] = {99, 98, 6, 38};
                    for (unsigned j = 0; j < 4; ++j)
                        if (p[32 + j * 3] != p[32] || p[33 + j * 3] != controllers[j]) return false;
                }
                break;
            case 3:
                if (n[1] || length != 5 || p[0] < 1 || p[0] > 10
                    || (p[0] == 1 && u32(p + 1) > 255) || (p[0] == 2 && u32(p + 1) > 1)
                    || (p[0] > 2 && u32(p + 1))) return false;
                break;
            case 4:
                if (n[1] || length != 1 || !button_pair_valid(p[0]) || !hardware.supports_function(1)) return false;
                break;
            case 5:
                if (n[1] || length < 2 || length != 2u + u16(p) * 2u) return false;
                for (unsigned j = 0; j < u16(p); ++j) if (u16(p + 2 + j * 2) >= count) return false;
                break;
            case 6:
                if (n[1] || length != 128 || (p[0] && !text_valid(p, 64)) || !text_valid(p + 64, 64)) return false;
                if (!p[0]) {
                    bool found = false;
                    for (unsigned j = 0; j < named; ++j)
                        if (!std::strcmp(reinterpret_cast<const char *>(p + 64), reinterpret_cast<const char *>(g + names + j * 66))) found = true;
                    if (!found) return false;
                }
                break;
            default: return false;
            }
        }
        if (first != size) return false;
        for (unsigned i = 0; i < browser_count; ++i) {
            unsigned root = u16(g + browser + i * 2), visited = 0;
            const uint8_t *node = nullptr;
            while (visited++ <= count) {
                node = g + u32(g + 16 + root * 4);
                if (node[0] != 6 || node[4]) break;
                bool found = false;
                for (unsigned j = 0; j < named; ++j)
                    if (!std::strcmp(reinterpret_cast<const char *>(node + 68), reinterpret_cast<const char *>(g + names + j * 66))) {
                        root = u16(g + names + j * 66 + 64); found = true; break;
                    }
                if (!found) return false;
            }
            if (visited > count + 1) return false;
            const uint8_t *cache = bytes + 76 + instruments * 68 + u16(bytes + 14) * 68 + i * 82;
            if (node[0] == 1) {
                const uint8_t *bank = bytes + metadata + metadata_header_size + instruments * 64 + mappings * 8 + i * 4;
                if (u16(cache) != u16(node + 4) || std::memcmp(cache + 4, node + 6, 5)
                    || u32(bank) != u32(node + 11)) return false;
            } else if (node[0] == 4) {
                if (u16(cache) != 65535 || cache[9] != 1 || cache[10] != node[4]) return false;
            } else if (u16(cache) != 65535 || cache[9]) return false;
        }
        return true;
    }
    static bool graphics_valid(const uint8_t *p, const Hardware &hardware) {
        return (p[0] == 255 ? u32(p + 12) && hardware.supports_font_asset(u32(p + 12), p[1])
                           : !u32(p + 12) && hardware.supports_font(p[0], p[1])) && !p[3]
            && 2u * p[2] < hardware.display_width()
            && 2u * p[2] + u16(p + 8) <= hardware.display_height()
            && u16(p + 8) && u16(p + 10) < u16(p + 8);
    }
    static ScreenGraphics read_graphics(const uint8_t *p) {
        return {p[0], p[1], p[2], 0, u16(p + 4), u16(p + 6), u16(p + 8), u16(p + 10), u32(p + 12)};
    }
    void field_text(char *text, unsigned size, unsigned code, unsigned index, unsigned literal) const {
        const uint8_t *p = patch(index);
        const uint8_t *source = data_ + texts_;
        while (literal--) source += 2 + u16(source);
        source += 2;
        if (u16(p) == 65535 && code >= 2 && code <= 6) return;
        switch (code) {
        case 0: source = category(u16(p + 2)); break;
        case 1: source = p + 12; break;
        case 6: if (u16(p) == 65535) return; source = instrument(u16(p)); break;
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
    unsigned graph_ = 0;
};
} // namespace cntrl
