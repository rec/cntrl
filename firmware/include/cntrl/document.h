#pragma once
#include "hardware.h"
#include "midi_bridge.h"
#include <cstring>
#include <cstdio>
#include <cmath>

namespace cntrl {
constexpr unsigned pages_offset = 16;
constexpr unsigned trs_modes_offset = pages_offset + 2 * PageSettings::clock_outputs;
constexpr unsigned scrolling_offset = trs_modes_offset + 4;
constexpr unsigned metadata_header_size = scrolling_offset + 14;
constexpr unsigned clock_section_size = 2 * (PageSettings::count - PageSettings::clock_outputs);
constexpr unsigned document_version = 34;
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
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(document_load)
#endif
    bool load(const uint8_t *bytes, unsigned length, const Hardware &hardware) {
        if (length < 76 || std::memcmp(bytes, "CSW1", 4)
            || u16(bytes + 4) != document_version || u16(bytes + 6) < 1
            || u32(bytes + 8) != length || u32(bytes + 24) > 2147483000u
            || checksum(bytes + 28, length - 28) != u32(bytes + 20)) return false;
        unsigned ni = u16(bytes + 12), nc = u16(bytes + 14), np = u16(bytes + 16);
        unsigned meta = 76 + ni * 68 + nc * 68 + np * 82;
        if (ni > max_instruments || nc > max_patch_cycles || bool(nc) != bool(np)
            || meta + metadata_header_size + ni * 64 + np * 4 > length
            || std::memcmp(bytes + meta, "CMTA", 4)) return false;
        for (unsigned i = 0; i < 4; ++i) {
            auto mode = midi::TrsMode(bytes[meta + trs_modes_offset + i]);
            if (mode > midi::TrsMode::aux || !hardware.supports_trs_mode(i, mode)) return false;
        }
        unsigned nb = u16(bytes + meta + 4);
        if (!u16(bytes + meta + 6) || !u16(bytes + meta + scrolling_offset)) return false;
        for (unsigned i = 0; i < 3; ++i) if (bytes[meta + scrolling_offset + 2 + i] > 3) return false;
        if (bytes[meta + scrolling_offset + 5] > 1) return false;
        if (u16(bytes + meta + 8) < 2 || bytes[meta + 10] > bytes[meta + 11]
            || !bytes[meta + 11] || bytes[meta + 11] > 128) return false;
        if (!u16(bytes + meta + 12)) return false;
        PageSettings right;
        for (unsigned i = 0; i < PageSettings::clock_outputs; ++i) right.words[i] = u16(bytes + meta + pages_offset + 2 * i);
        using R = PageSettings;
        if (!right[R::refresh] || !right[R::clock_timeout] || !right[R::clocks_per_quarter]
            || right[R::clocks_per_quarter] > 960 || !right[R::clock_beats] || right[R::clock_beats] > 16
            || !right[R::tap_intervals] || right[R::tap_intervals] > 8 || right[R::minimum_taps] < 2
            || right[R::minimum_taps] > right[R::tap_intervals] + 1 || !right[R::minimum_interval]
            || right[R::minimum_interval] > right[R::reset_gap] || !right[R::flash_fraction] || right[R::flash_fraction] > 1000
            || !right[R::diameter] || !right[R::annulus] || right[R::title_height] < 8 || right[R::tempo_height] < 8
            || unsigned(right[R::title_height]) * 3 / 2 + 4 + right[R::tempo_height] + right[R::diameter] + 2u * right[R::annulus] + 2u * right[R::padding] > hardware.display_height()
            || unsigned(right[R::diameter]) + 2u * right[R::annulus] + 2u * right[R::padding] > hardware.display_width()
            || (right[R::tempo_font] != 8 && right[R::tempo_font] != 9) || (right[R::status_font] > 2 && right[R::status_font] != 16) || (right[R::title_font] > 2 && right[R::title_font] != 16)
            || !right[R::digits] || right[R::digits] > 4 || right[R::decimals] > 2) return false;
        unsigned names = meta + metadata_header_size + ni * 64 + nb * 8 + np * 4;
        if (bytes[19] & 0x80) {
            if (names + expression_section_size > length) return false;
            for (unsigned i = 0; i < 4; ++i) {
                const uint8_t *p = bytes + names + i * expression_record_size;
                if (!p[32]) continue;
                if (midi::TrsMode(bytes[meta + trs_modes_offset + i]) != midi::TrsMode::expression || !text_valid(p, 32)
                    || !output_valid(bytes + meta + trs_modes_offset, p, hardware)
                    || (!std::strcmp(reinterpret_cast<const char *>(p), "usb-device") && !(bytes[19] & 8))
                    || (!std::strcmp(reinterpret_cast<const char *>(p), "usb-host") && !(bytes[19] & 16))
                    || p[32] > 16 || p[33] > 127 || p[34] > p[35] || p[35] > 127
                    || p[36] > 1 || p[37] > 2) return false;
            }
            names += expression_section_size;
        }
        if (bytes[19] & 0x20) {
            if (names + clock_section_size > length) return false;
            read_clock(right, bytes + names);
            names += clock_section_size;
            if (right[R::clock_external] > 1 || !right[R::slot1] || (right[R::clock_outputs] & ~0x17fu)
                || ((right[R::clock_outputs] & (1u << 6)) && !(bytes[19] & 8))
                || ((right[R::clock_outputs] & (1u << 8)) && !(bytes[19] & 16))) return false;
            for (const auto &port : midi::output_connections)
                if ((right[R::clock_outputs] & (1u << port.route))
                    && !output_valid(bytes + meta + trs_modes_offset, reinterpret_cast<const uint8_t *>(port.name), hardware)) return false;
            for (const auto &port : midi::usb_output_connections)
                if ((right[R::clock_outputs] & (1u << port.route)) && !hardware.supports(port.name)) return false;
            for (unsigned i = R::slot1; i <= R::slot4; ++i) {
                unsigned bpm = right.words[i];
                if (bpm && (bpm < 20 || bpm > 600 || (i > R::slot1 && !right.words[i - 1]))) return false;
            }
        }
        if (names + 4 <= length && !std::memcmp(bytes + names, "CKBD", 4)) {
            if (!hardware.supports_keyboard() || names + 6 > length || !u16(bytes + names + 4)) return false;
            unsigned count = u16(bytes + names + 4);
            if (count > (length - names - 6) / 39) return false;
            for (unsigned i = 0; i < count; ++i) {
                const uint8_t *p = bytes + names + 6 + i * 39;
                if (!text_valid(p, 32) || !p[33]) return false;
                for (unsigned j = 33; j < 39; ++j) if (p[j] && (p[j] < 4 || p[j] > 115)) return false;
            }
            names += 6 + count * 39;
        }
        unsigned gestures = names;
        if (bytes[19] & 0x40) names += 100;
        unsigned hooks = names;
        bool has_hooks = names + 4 <= length && !std::memcmp(bytes + names, "CHOK", 4);
        if (has_hooks) names += 29;
        unsigned shifted = names;
        bool has_shifted = names + 4 <= length && !std::memcmp(bytes + names, "CSHF", 4);
        if (has_shifted) names += 124;
        const unsigned layout = names;
        if (names + 6 > length || std::memcmp(bytes + names, "CLAY", 4)
            || bytes[names + 4] > AppLayout::max_apps || bytes[names + 5] > 32) return false;
        unsigned apps = bytes[names + 4], page_count = bytes[names + 5];
        names += 6 + apps * 35 + page_count * 2;
        if (names > length) return false;
        for (unsigned i = 0; i < apps; ++i) {
            const uint8_t *p = bytes + layout + 6 + i * 35;
            if (!text_valid(p, 32) || p[32] > 2 || p[33] > 2 || p[34] > 2
                || p[32] == p[33] || p[32] == p[34] || p[33] == p[34]) return false;
            for (unsigned j = 0; j < i; ++j)
                if (!std::strcmp(reinterpret_cast<const char *>(p), reinterpret_cast<const char *>(bytes + layout + 6 + j * 35))) return false;
        }
        for (unsigned i = 0; i < page_count; ++i) {
            const uint8_t *p = bytes + layout + 6 + apps * 35 + i * 2;
            if (p[0] > 10 || p[1] > 3 || (p[0] == 10 && p[1])) return false;
        }
        if (names + 8 > length || std::memcmp(bytes + names, "CMOV", 4)) return false;
        unsigned movie_count = unsigned(u16(bytes + names + 4)) + u16(bytes + names + 6);
        if (movie_count > (length - names - 8) / 4) return false;
        if (bool(bytes[19] & 1) != bool(u16(bytes + names + 4))) return false;
        names += 8 + movie_count * 4;
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
        if (has_hooks) {
            for (unsigned b = 0; b < 5; ++b) {
                const uint8_t *p = bytes + hooks + 4 + b * 5;
                bool assigned = false;
                for (unsigned i = 0; i < switch_count; ++i)
                    if (bytes[switch_start + i * 72] == b) assigned = true;
                for (unsigned j = 0; j < 2; ++j)
                    if (u16(p + j * 2) != 65535 && (!assigned || u16(p + j * 2) >= u16(bytes + graph + 8))) return false;
                if (p[4] > 1 || (p[4] && u16(p) == 65535)) return false;
            }
        }
        if (bytes[19] & 0x40) {
            for (unsigned b = 0; b < 5; ++b) {
                const uint8_t *p = bytes + gestures + b * 20;
                bool assigned = false;
                for (unsigned i = 0; i < switch_count; ++i) {
                    const uint8_t *s = bytes + switch_start + i * 72;
                    if (s[0] == b && s[1] == 0) assigned = true;
                }
                if (!gestures_valid(p, u16(bytes + graph + 8), assigned)) return false;
            }
        }
        if (has_shifted) {
            for (unsigned b = 0; b < 5; ++b) {
                const uint8_t *p = bytes + shifted + 4 + b * 24, *record = nullptr;
                for (unsigned i = 0; i < switch_count; ++i)
                    if (bytes[switch_start + i * 72] == b) record = bytes + switch_start + i * 72;
                bool configured = u16(p) != 65535;
                unsigned count = u16(bytes + graph + 8);
                if (configured && (!record || u16(p) >= count
                    || (record[1] == 0 ? u16(p + 2) != 65535 : u16(p + 2) >= count))) return false;
                if (!configured && u16(p + 2) != 65535) return false;
                if (!gestures_valid(p + 4, count, configured && record && record[1] == 0)) return false;
            }
        }
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
                if ((p[0] > 7 && (p[0] < 16 || p[0] > 23) && p[0] != 255)
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
                || !output_valid(bytes + meta + trs_modes_offset, p + 32, hardware)) return false;
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
        data_ = bytes; length_ = length; instruments_ = ni; patch_cycles_ = nc; patches_ = np;
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
    unsigned patch_cycles() const { return patch_cycles_; }
    unsigned instruments() const { return instruments_; }
    unsigned patches() const { return patches_; }
    unsigned navigation_hold_ms() const { return u16(data_ + 6); }
    bool fast_switches() const { return data_[metadata() + scrolling_offset + 5] != 0; }
    unsigned app_page_ram2() const { return u32(data_ + metadata() + scrolling_offset + 6); }
    uint32_t names_signature() const { return u32(data_ + metadata() + scrolling_offset + 10); }
    uint8_t brightness() const { return data_[18]; }
    unsigned pulse_delay_ms() const { return u16(data_ + metadata() + 14); }
    unsigned pulse_period_ms() const { return u16(data_ + metadata() + 8); }
    uint8_t pulse_minimum() const { return data_[metadata() + 10]; }
    uint8_t pulse_maximum() const { return data_[metadata() + 11]; }
    unsigned navigation_repeat_ms() const { return u16(data_ + metadata() + 12); }
    PageSettings page_settings() const {
        PageSettings settings;
        for (unsigned i = 0; i < PageSettings::clock_outputs; ++i) settings.words[i] = u16(data_ + metadata() + pages_offset + 2 * i);
        if (data_[19] & 0x80) for (unsigned i = 0; i < 4; ++i) {
            const uint8_t *p = bank(patches()) + i * expression_record_size;
            auto &a = settings.expression[i];
            std::memcpy(a.output, p, sizeof(a.output));
            a.channel = p[32]; a.controller = p[33]; a.minimum = p[34]; a.maximum = p[35];
            a.reverse = p[36]; a.curve = ExpressionCurve(p[37]);
        }
        settings.words[PageSettings::slot1] = 120;
        if (data_[19] & 0x20) read_clock(settings, bank(patches()) + ((data_[19] & 0x80) ? expression_section_size : 0));
        if (keyboard_size()) {
            settings.keyboard_count = u16(keyboard_section() + 4);
            settings.keyboard = keyboard_section() + 6;
        }
        return settings;
    }
    bool splash() const { return data_[19] & 1; }
    bool animation_color_cycle() const { return data_[19] & 4; }
    bool usb_device_midi() const { return data_[19] & 8; }
    const uint8_t *keyboard_section() const {
        return bank(patches()) + ((data_[19] & 0x80) ? expression_section_size : 0) + ((data_[19] & 0x20) ? clock_section_size : 0);
    }
    unsigned keyboard_size() const { return !std::memcmp(keyboard_section(), "CKBD", 4) ? 6 + u16(keyboard_section() + 4) * 39 : 0; }
    const uint8_t *switch_hook_section() const {
        return bank(patches()) + ((data_[19] & 0x80) ? expression_section_size : 0) + ((data_[19] & 0x20) ? clock_section_size : 0) + keyboard_size() + ((data_[19] & 0x40) ? 100 : 0);
    }
    const uint8_t *switch_hooks(unsigned button) const {
        const uint8_t *p = switch_hook_section();
        if (std::memcmp(p, "CHOK", 4)) return nullptr;
        p += 4 + button * 5;
        return u16(p) != 65535 || u16(p + 2) != 65535 ? p : nullptr;
    }
    const uint8_t *switch_shifted(unsigned button) const {
        const uint8_t *p = switch_hook_section();
        if (!std::memcmp(p, "CHOK", 4)) p += 29;
        if (std::memcmp(p, "CSHF", 4)) return nullptr;
        p += 4 + button * 24;
        return u16(p) != 65535 ? p : nullptr;
    }
    unsigned page_section_size() const {
        return unsigned(movie_section() - bank(patches())) + 8 + 4 * (movies(false) + movies(true));
    }
    const uint8_t *movie_section() const {
        const auto layout = app_layout();
        return layout.records + 6 + layout.apps() * 35 + layout.pages() * 2;
    }
    unsigned movies(bool idle) const { return u16(movie_section() + (idle ? 6 : 4)); }
    const uint8_t *movie_keys(bool idle) const { return movie_section() + 8 + (idle ? 4 * movies(false) : 0); }
    AppLayout app_layout() const {
        const uint8_t *p = switch_hook_section();
        if (!std::memcmp(p, "CHOK", 4)) p += 29;
        if (!std::memcmp(p, "CSHF", 4)) p += 124;
        return AppLayout{p};
    }
    const uint8_t *switch_gestures(unsigned button) const {
        if (!(data_[19] & 0x40)) return nullptr;
        const uint8_t *p = switch_hook_section() - 100 + button * 20;
        return u16(p + 12) ? p : nullptr;
    }
    const uint8_t *trs_modes() const { return data_ + metadata() + trs_modes_offset; }
    bool usb_host_midi() const { return data_[19] & 16; }
    bool is_midi_interface() const { return data_[19] & 2; }
    uint32_t screensaver_timeout() const { return u32(data_ + 24); }
    unsigned scroll_ms(bool vertical = false) const { return u16(data_ + metadata() + (vertical ? scrolling_offset : 6)); }
    unsigned scroll_direction(unsigned action) const { return data_[metadata() + scrolling_offset + 2 + action]; }
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
            line.centered = (p[0] >= 2 && p[0] <= 5) || (p[0] >= 16 && p[0] <= 23);
            line.stippled = already_sent && p[1];
            if (p[0] == 22) std::snprintf(line.text, sizeof(line.text), "%s", state);
            else field_text(line.text, sizeof(line.text), p[0], index, u16(p + 6));
        }
    }
    const uint8_t *instrument(unsigned i) const { return data_ + 76 + i * 68; }
    const uint8_t *patch_cycle(unsigned i) const { return data_ + 76 + instruments_ * 68 + i * 68; }
    const uint8_t *patch(unsigned i) const { return data_ + 76 + instruments_ * 68 + patch_cycles_ * 68 + i * 82; }
    unsigned metadata() const { return 76 + instruments_ * 68 + patch_cycles_ * 68 + patches_ * 82; }
    unsigned mappings() const { return u16(data_ + metadata() + 4); }
    const uint8_t *mapping(unsigned i) const { return data_ + metadata() + metadata_header_size + instruments_ * 64 + i * 8; }
    const uint8_t *bank(unsigned i) const { return mapping(mappings()) + i * 4; }
private:
    static bool gestures_valid(const uint8_t *p, unsigned count, bool assigned) {
        unsigned debounce = u16(p + 12);
        if (debounce && (!assigned || debounce > 1000 || u16(p + 14) < debounce
            || u16(p + 16) < debounce || u16(p + 18) < debounce
            || (u16(p + 4) != 65535 && u16(p + 2) == 65535 && u16(p + 10) == 65535)
            || (u16(p + 8) != 65535 && u16(p + 6) == 65535))) return false;
        for (unsigned j = 0; j < 6; ++j)
            if (u16(p + j * 2) != 65535 && (!debounce || u16(p + j * 2) >= count)) return false;
        return true;
    }
    static void read_clock(PageSettings &settings, const uint8_t *p) {
        for (unsigned i = PageSettings::clock_outputs; i < PageSettings::count; ++i, p += 2) settings.words[i] = u16(p);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(document_graph_valid)
#endif
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
            if (!label_valid(n, 64) || u16(n + 64) >= count) return false;
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
                    || !output_valid(bytes + metadata + trs_modes_offset, p, hardware)
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
                if (n[1] || length != 5 || p[0] < 1 || p[0] > 12
                    || (p[0] >= 11 && !hardware.switch_capacity())
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
            case 8: {
                double speed = 0;
                if (n[1] || length != 120 || (p[0] && !text_valid(p, 96)) || !u16(p + 96) || (u16(p + 96) & ~0x17f)
                    || p[118] > 1 || int8_t(p[119]) == -128
                    || ((u16(p + 96) & (1u << 6)) && !(bytes[19] & 8))
                    || ((u16(p + 96) & (1u << 8)) && !(bytes[19] & 16))) return false;
                for (unsigned j = 0; j < 96 && p[j]; ++j) if (p[j] == '/' || p[j] == '\\') return false;
                std::memcpy(&speed, p + 110, 8);
                if (!std::isfinite(speed) || speed <= 0) return false;
                for (const auto &out : midi::output_connections)
                    if ((u16(p + 96) & (1u << out.route)) && !output_valid(bytes + metadata + trs_modes_offset, reinterpret_cast<const uint8_t *>(out.name), hardware)) return false;
                for (const auto &out : midi::usb_output_connections)
                    if ((u16(p + 96) & (1u << out.route)) && !hardware.supports(out.name)) return false;
                break;
            }
            case 9:
                if (n[1] || length != 4 || !u16(p) || !u16(p + 2) || (u16(p + 2) & ~0x143)) return false;
                break;
            case 7:
                if (n[1] || length != 4 || !u32(p) || u32(p) > 65535) return false;
                break;
            case 6:
                if (n[1] || length != 66 || !label_valid(p + 2, 64)) return false;
                if (u16(p) == 65535) {
                    bool found = false;
                    for (unsigned j = 0; j < named; ++j)
                        if (!std::strcmp(reinterpret_cast<const char *>(p + 2), reinterpret_cast<const char *>(g + names + j * 66))) found = true;
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
                if (node[0] != 6 || u16(node + 4) != 65535) break;
                bool found = false;
                for (unsigned j = 0; j < named; ++j)
                    if (!std::strcmp(reinterpret_cast<const char *>(node + 6), reinterpret_cast<const char *>(g + names + j * 66))) {
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
    static bool output_valid(const uint8_t *modes, const uint8_t *output, const Hardware &hardware) {
        auto name = reinterpret_cast<const char *>(output);
        auto *route = midi::find_output(name);
        return hardware.supports(name) && (!route || route->route < 2 || route->route >= 6
            || midi::midi_output(midi::TrsMode(modes[route->route - 2])));
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
        case 0: source = patch_cycle(u16(p + 2)); break;
        case 1: source = p + 12; break;
        case 6: if (u16(p) == 65535) return; source = instrument(u16(p)); break;
        case 2: std::snprintf(text, size, "%u", unsigned(p[4]) + 1); return;
        case 3: if (u32(bank(index)) == 0xffffffffu) return; std::snprintf(text, size, "%lu", (unsigned long)u32(bank(index))); return;
        case 4: if (p[7] == 255) return; std::snprintf(text, size, "%u", unsigned(p[7])); return;
        case 5: if (p[8] == 255) return; std::snprintf(text, size, "%d", int(p[8]) - 64); return;
        case 16: std::snprintf(text, size, "%u", unsigned(brightness())); return;
        case 17: case 18: std::snprintf(text, size, "%s", (code == 17 ? splash() : is_midi_interface()) ? "true" : "false"); return;
        case 19: std::snprintf(text, size, "%lu", (unsigned long)(screensaver_timeout() / 1000)); return;
        case 20: case 21: case 23: {
            unsigned ms = code == 20 ? navigation_hold_ms() : scroll_ms(code == 23);
            if (ms % 1000) {
                std::snprintf(text, size, "%u.%03u", ms / 1000, ms % 1000);
                unsigned end = unsigned(std::strlen(text));
                while (end && text[end - 1] == '0') text[--end] = 0;
            } else std::snprintf(text, size, "%u", ms / 1000);
            return;
        }
        }
        std::snprintf(text, size, "%s", reinterpret_cast<const char *>(source));
    }
    const uint8_t *data_ = nullptr;
    unsigned length_ = 0, instruments_ = 0, patch_cycles_ = 0, patches_ = 0;
    unsigned styles_ = 0, pages_ = 0, texts_ = 0;
    unsigned graph_ = 0;
};
} // namespace cntrl
