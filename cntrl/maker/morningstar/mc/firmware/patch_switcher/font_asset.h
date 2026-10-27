#pragma once
#include <cntrl/document.h>
#include <ILI9341_fonts.h>
#include <cstdlib>
#include <memory>

namespace font_assets {
constexpr unsigned max_file = 256 * 1024, max_loaded = 96 * 1024, max_fonts = 32;
struct Entry {
    uint8_t bytes[23] = {};
    unsigned size() const { return bytes[0]; }
    unsigned index_length() const { return cntrl::u32(bytes + 15); }
    unsigned data_length() const { return cntrl::u32(bytes + 19); }
    unsigned length() const { return index_length() + data_length(); }
    bool valid() const {
        const uint8_t expected[] = {1, 0, 32, 126, 0, 0};
        if (!size() || std::memcmp(bytes + 1, expected, 6) || !bytes[13] || bytes[14] > 128) return false;
        for (unsigned i = 7; i < 13; ++i) if (!bytes[i] || bytes[i] > 16) return false;
        return index_length() == (95u * bytes[7] + 7) / 8 && data_length()
            && data_length() <= max_loaded && length() <= max_loaded;
    }
    ILI9341_t3_font_t font(const uint8_t *data) const {
        return {data, nullptr, data + index_length(),
            bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
            bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14]};
    }
};
inline unsigned bits(const uint8_t *data, unsigned offset, unsigned count) {
    unsigned value = 0;
    for (unsigned i = 0; i < count; ++i) value = (value << 1) | ((data[(offset + i) / 8] >> (7 - (offset + i) % 8)) & 1);
    return value;
}
inline bool valid_pixels(const Entry &e, const uint8_t *data) {
    if (!e.valid()) return false;
    const uint8_t *pixels = data + e.index_length();
    unsigned limit = e.data_length() * 8;
    for (unsigned c = 0; c < 95; ++c) {
        unsigned bit = bits(data, c * e.bytes[7], e.bytes[7]) * 8;
        unsigned header = 3;
        for (unsigned i = 8; i < 13; ++i) header += e.bytes[i];
        if (bit > limit || header > limit - bit || bits(pixels, bit, 3)) return false;
        bit += 3;
        unsigned width = bits(pixels, bit, e.bytes[8]); bit += e.bytes[8];
        unsigned height = bits(pixels, bit, e.bytes[9]); bit += e.bytes[9];
        if (width > 160 || height > 128) return false;
        bit += e.bytes[10] + e.bytes[11];
        if (bits(pixels, bit, e.bytes[12]) > 255) return false;
        bit += e.bytes[12];
        if (height * (width + 1) > limit - bit) return false;
        for (unsigned y = 0; y < height; ++y, bit += width + 1)
            if (bits(pixels, bit, 1)) return false;
    }
    return true;
}
struct Loaded {
    uint32_t key = 0;
    Entry entry;
    std::unique_ptr<uint8_t[]> data;
    ILI9341_t3_font_t font = {};
};
class Cache {
public:
    void begin() { clear(pending_); count_ = used_ = 0; }
    bool contains(uint32_t key, unsigned size) const {
        for (const auto &f : pending_) if (f.key == key && f.entry.size() == size) return true;
        return false;
    }
    bool add(uint32_t key, const Entry &entry, std::unique_ptr<uint8_t[]> data) {
        if (count_ == max_fonts || entry.length() > max_loaded - used_ || !valid_pixels(entry, data.get())) return false;
        auto &f = pending_[count_++]; f.key = key; f.entry = entry;
        f.data = std::move(data); f.font = entry.font(f.data.get()); used_ += entry.length(); return true;
    }
    void finish(bool commit) {
        if (commit) for (unsigned i = 0; i < max_fonts; ++i) std::swap(active_[i], pending_[i]);
        begin();
    }
    const ILI9341_t3_font_t *get(uint32_t key, unsigned size) const {
        for (const auto &f : active_) if (f.key == key && f.entry.size() == size) return &f.font;
        return nullptr;
    }
private:
    Loaded active_[max_fonts], pending_[max_fonts];
    unsigned count_ = 0, used_ = 0;
    static void clear(Loaded *fonts) { for (unsigned i = 0; i < max_fonts; ++i) fonts[i] = Loaded(); }
};
inline Cache cache;
} // namespace font_assets
