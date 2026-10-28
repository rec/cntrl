#pragma once
#include <cntrl/document.h>
#include <memory>

namespace font_assets {
constexpr unsigned max_file = 256 * 1024, max_loaded = 96 * 1024, max_fonts = 32;
inline int signed16(const uint8_t *p) { return int16_t(cntrl::u16(p)); }
struct Entry {
    uint8_t bytes[23] = {};
    unsigned size() const { return bytes[0]; }
    unsigned glyphs() const { return cntrl::u16(bytes + 7); }
    unsigned runs() const { return cntrl::u16(bytes + 9); }
    unsigned characters() const { return cntrl::u16(bytes + 11); }
    unsigned index_length() const { return cntrl::u32(bytes + 15); }
    unsigned data_length() const { return cntrl::u32(bytes + 19); }
    unsigned length() const { return index_length() + data_length(); }
    bool valid() const {
        const uint8_t expected[] = {2, 0, 0, 0, 0, 0};
        return size() && !std::memcmp(bytes + 1, expected, 6) && bytes[13] && bytes[14] <= 128
            && index_length() <= max_loaded && data_length() <= max_loaded
            && length() <= max_loaded && glyphs() * 16 + characters() * 8 <= index_length();
    }
};
inline bool numeric(unsigned c) { return c == '-' || (c >= '0' && c <= '9'); }
inline bool valid_pixels(const Entry &e, const uint8_t *data) {
    if (!e.valid()) return false;
    unsigned end = 0; uint32_t previous = 0;
    for (unsigned i = 0; i < e.glyphs(); ++i) {
        const auto *g = data + i * 16;
        unsigned w = cntrl::u16(g + 4), h = cntrl::u16(g + 6), start = cntrl::u32(g + 12);
        if ((i && cntrl::u32(g) <= previous) || w > 160 || h > 128 || start != end) return false;
        end = start + ((w + 7) / 8) * h;
        if (end > e.data_length()) return false;
        previous = cntrl::u32(g);
    }
    if (end != e.data_length()) return false;
    unsigned offset = e.glyphs() * 16; bool seen[11] = {};
    for (unsigned i = 0; i < e.characters(); ++i, offset += 8) {
        const auto *c = data + offset; unsigned code = cntrl::u32(c);
        if (!numeric(code) || cntrl::u16(c + 4) >= e.glyphs() || signed16(c + 6) < 0) return false;
        unsigned slot = code == '-' ? 10 : code - '0';
        if (seen[slot]) return false;
        seen[slot] = true;
    }
    for (unsigned i = 0; i < e.runs(); ++i) {
        if (offset + 8 > e.index_length()) return false;
        const auto *r = data + offset;
        unsigned n = cntrl::u16(r), count = cntrl::u16(r + 6); offset += 8;
        if (n > 63 || n + count * 6 > e.index_length() - offset || signed16(r + 4) < 0
            || !cntrl::utf8_valid(data + offset, n)) return false;
        offset += n;
        for (unsigned j = 0; j < count; ++j, offset += 6)
            if (cntrl::u16(data + offset) >= e.glyphs()) return false;
    }
    return offset == e.index_length();
}
struct Loaded {
    uint32_t key = 0;
    Entry entry;
    std::unique_ptr<uint8_t[]> data;
    const uint8_t *run(const char *text) const {
        unsigned offset = entry.glyphs() * 16 + entry.characters() * 8;
        unsigned length = std::strlen(text);
        for (unsigned i = 0; i < entry.runs(); ++i) {
            const auto *r = data.get() + offset; unsigned n = cntrl::u16(r);
            if (n == length && !std::memcmp(r + 8, text, n)) return r;
            offset += 8 + n + cntrl::u16(r + 6) * 6;
        }
        return nullptr;
    }
    const uint8_t *character(unsigned code) const {
        for (unsigned i = 0; i < entry.characters(); ++i) {
            const auto *c = data.get() + entry.glyphs() * 16 + i * 8;
            if (cntrl::u32(c) == code) return c;
        }
        return nullptr;
    }
    bool position(const char *text, unsigned index, unsigned &glyph, int &x, int &y) const {
        if (const auto *r = run(text)) {
            if (index >= cntrl::u16(r + 6)) return false;
            const auto *p = r + 8 + cntrl::u16(r) + index * 6;
            glyph = cntrl::u16(p); x = signed16(p + 2); y = signed16(p + 4); return true;
        }
        x = y = 0;
        for (unsigned i = 0; text[i]; ++i) {
            const auto *c = character(uint8_t(text[i]));
            if (!c) return false;
            if (i == index) { glyph = cntrl::u16(c + 4); return true; }
            x += signed16(c + 6);
        }
        return false;
    }
    void bounds(const char *text, int &left, int &width) const {
        left = width = 0;
        if (const auto *r = run(text)) { left = signed16(r + 2); width = signed16(r + 4); return; }
        int cursor = 0, right = 0;
        for (unsigned i = 0; text[i]; ++i) {
            const auto *c = character(uint8_t(text[i])); if (!c) return;
            const auto *g = data.get() + cntrl::u16(c + 4) * 16;
            int l = cursor + signed16(g + 8), r = l + cntrl::u16(g + 4);
            if (l < left) left = l;
            if (r > right) right = r;
            cursor += signed16(c + 6);
        }
        width = (cursor > right ? cursor : right) - left;
    }
};
class Cache {
public:
    void begin() { clear(pending_); count_ = used_ = 0; }
    bool contains(uint32_t key, unsigned size) const {
        for (const auto &f : pending_) if (f.key == key && f.entry.size() == size) return true;
        return false;
    }
    bool verified(uint32_t key) const {
        for (const auto &f : pending_) if (f.key == key) return true;
        return false;
    }
    bool fits(const Entry &entry) const { return count_ < max_fonts && entry.length() <= max_loaded - used_; }
    bool reuse(uint32_t key, unsigned size) {
        for (const auto &f : active_) if (f.key == key && f.entry.size() == size && fits(f.entry)) {
            auto &p = pending_[count_++]; p.key = key; p.entry = f.entry;
            used_ += f.entry.length(); return true;
        }
        return false;
    }
    bool add(uint32_t key, const Entry &entry, std::unique_ptr<uint8_t[]> data) {
        if (!fits(entry) || !valid_pixels(entry, data.get())) return false;
        auto &f = pending_[count_++]; f.key = key; f.entry = entry;
        f.data = std::move(data); used_ += entry.length(); return true;
    }
    void finish(bool commit) {
        if (commit) {
            // Borrowed pending faces acquire ownership before old faces are freed.
            for (auto &p : pending_) if (p.key && !p.data)
                for (auto &a : active_) if (a.key == p.key && a.entry.size() == p.entry.size()) {
                    p.data = std::move(a.data); break;
                }
            for (unsigned i = 0; i < max_fonts; ++i) std::swap(active_[i], pending_[i]);
        }
        begin();
    }
    const Loaded *get(uint32_t key, unsigned size) const {
        for (const auto &f : active_) if (f.key == key && f.entry.size() == size) return &f;
        return nullptr;
    }
private:
    Loaded active_[max_fonts], pending_[max_fonts];
    unsigned count_ = 0, used_ = 0;
    static void clear(Loaded *fonts) { for (unsigned i = 0; i < max_fonts; ++i) fonts[i] = Loaded(); }
};
inline Cache cache;
} // namespace font_assets
