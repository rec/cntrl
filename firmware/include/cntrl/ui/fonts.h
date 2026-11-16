#pragma once
#include "font_assets.h"
#include <algorithm>

namespace cntrl::ui {
inline constexpr unsigned localized_face = 253;
inline constexpr unsigned status_face = 19, status_size = 13;
struct Bounds { int left = 0, top = 0, width = 0, height = 0; };
struct Glyph {
    const uint8_t *pixels = nullptr;
    unsigned width = 0, height = 0;
    int x = 0, y = 0;
};
struct EmbeddedFont { unsigned face; const uint8_t *bytes; unsigned length; };
struct Fonts {
    virtual ~Fonts() = default;
    virtual bool supports(unsigned face, unsigned size, uint32_t asset = 0) const = 0;
    virtual Bounds bounds(unsigned face, unsigned size, uint32_t asset, const char *text) const = 0;
    // Glyph positions are relative to the saved text cursor. Embedded faces
    // include their cap-height origin; asset runs use the saved baseline.
    // The caller adds TextLine.graphics.top, including its stored font metric
    // or explicit layout offset. It must not add another universal baseline.
    virtual bool glyph(unsigned face, unsigned size, uint32_t asset, const char *text, unsigned index, Glyph &result) const = 0;
};
class AssetFonts final : public Fonts {
public:
    AssetFonts(const EmbeddedFont *fonts, unsigned count) : fonts_(fonts), count_(count) {}
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(fonts_valid_localized)
#endif
    static bool valid_localized(const uint8_t *bytes, unsigned length) {
        if (!bytes || length < 8 || length > font_assets::max_file || std::memcmp(bytes, "CFN2", 4)) return false;
        const unsigned count = cntrl::u32(bytes + 4); unsigned offset = 8; bool sizes[256] = {};
        if (!count || count > 255) return false;
        for (unsigned i = 0; i < count; ++i) {
            font_assets::Entry entry;
            if (offset > length || length - offset < sizeof(entry.bytes)) return false;
            std::memcpy(entry.bytes, bytes + offset, sizeof(entry.bytes)); offset += sizeof(entry.bytes);
            if (!entry.valid() || sizes[entry.size()] || entry.length() > length - offset
                || !font_assets::valid_pixels(entry, bytes + offset)) return false;
            sizes[entry.size()] = true; offset += entry.length();
        }
        return offset == length;
    }
    // The active language pack owns these bytes until its renderer is invalidated.
    static void localized(const uint8_t *bytes, unsigned length) {
        localized_bytes_ = bytes; localized_length_ = length;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(fonts_supports)
#endif
    bool supports(unsigned face, unsigned size, uint32_t asset = 0) const override {
        font_assets::View view; return find(face, size, asset, view);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(fonts_bounds)
#endif
    Bounds bounds(unsigned face, unsigned size, uint32_t asset, const char *text) const override {
        font_assets::View view; Bounds result;
        if (!find(face, size, asset, view)) return result;
        if (face == status_face) return status_bounds(view, text);
        view.bounds(text, result.left, result.top, result.width, result.height, face == 255 ? 0 : view.entry.bytes[14]);
        return result;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(fonts_glyph)
#endif
    bool glyph(unsigned face, unsigned size, uint32_t asset, const char *text, unsigned index, Glyph &result) const override {
        font_assets::View view;
        if (!find(face, size, asset, view)) return false;
        if (face == status_face) {
            const char *cursor = text; uint32_t code; int x = 0;
            for (unsigned i = 0; utf8::next(cursor, code); ++i) {
                font_assets::View selected;
                const auto *c = status_character(view, code, selected);
                if (!c) return false;
                if (i == index) {
                    const auto *g = selected.data + cntrl::u16(c + 4) * 16;
                    result.width = cntrl::u16(g + 4); result.height = cntrl::u16(g + 6);
                    result.x = x + font_assets::signed16(g + 8);
                    result.y = view.entry.bytes[14] - font_assets::signed16(g + 10);
                    result.pixels = selected.data + selected.entry.index_length() + cntrl::u32(g + 12);
                    return true;
                }
                x += font_assets::signed16(c + 6);
            }
            return false;
        }
        return make_glyph(view, text, index, result, face == 255);
    }
private:
    const EmbeddedFont *fonts_;
    unsigned count_;
    inline static const uint8_t *localized_bytes_ = nullptr;
    inline static unsigned localized_length_ = 0;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(fonts_status_character)
#endif
    static const uint8_t *status_character(const font_assets::View &base, unsigned code, font_assets::View &selected) {
        selected = base;
        if (const auto *c = selected.character(code)) return c;
        if (localized_bytes_ && find_size(localized_bytes_, localized_length_, status_size, selected))
            return selected.character(code);
        return nullptr;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(fonts_status_bounds)
#endif
    static Bounds status_bounds(const font_assets::View &base, const char *text) {
        Bounds result; int cursor = 0, right = 0; bool first = true;
        uint32_t code;
        while (utf8::next(text, code)) {
            font_assets::View selected;
            const auto *c = status_character(base, code, selected);
            if (!c) return result;
            const auto *g = selected.data + cntrl::u16(c + 4) * 16;
            const unsigned w = cntrl::u16(g + 4), h = cntrl::u16(g + 6);
            const int top = base.entry.bytes[14] - font_assets::signed16(g + 10);
            if (w && h) {
                if (first) { result.top = top; result.height = int(h); first = false; }
                else {
                    const int bottom = std::max(result.top + result.height, top + int(h));
                    result.top = std::min(result.top, top); result.height = bottom - result.top;
                }
            }
            result.left = std::min(result.left, cursor + font_assets::signed16(g + 8));
            right = std::max(right, cursor + font_assets::signed16(g + 8) + int(w));
            cursor += font_assets::signed16(c + 6);
        }
        result.width = std::max(cursor, right) - result.left;
        return result;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(fonts_find)
#endif
    bool find(unsigned face, unsigned size, uint32_t asset, font_assets::View &view) const {
        if (face == 255) {
            const auto *loaded = font_assets::cache.get(asset, size);
            if (!loaded) return false;
            view.entry = loaded->entry; view.data = loaded->data.data;
            return true;
        }
        if (face == localized_face) {
            if (localized_bytes_) return size != status_size && find_size(localized_bytes_, localized_length_, size, view);
            face = 0;
        }
        for (unsigned f = 0; f < count_; ++f) {
            const auto &font = fonts_[f];
            if (font.face == face && find_size(font.bytes, font.length, size, view)) return true;
        }
        return false;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(fonts_find_size)
#endif
    static bool find_size(const uint8_t *bytes, unsigned length, unsigned size, font_assets::View &view) {
        if (length < 8 || std::memcmp(bytes, "CFN2", 4)) return false;
        unsigned offset = 8, count = cntrl::u32(bytes + 4);
        for (unsigned i = 0; i < count; ++i) {
            font_assets::Entry entry;
            if (offset > length || length - offset < sizeof(entry.bytes)) return false;
            std::memcpy(entry.bytes, bytes + offset, sizeof(entry.bytes)); offset += sizeof(entry.bytes);
            if (!entry.valid() || entry.length() > length - offset) return false;
            if (entry.size() == size) { view.entry = entry; view.data = bytes + offset; return true; }
            offset += entry.length();
        }
        return false;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(fonts_make_glyph)
#endif
    static bool make_glyph(const font_assets::View &font, const char *text, unsigned index, Glyph &result, bool asset) {
        unsigned number; int x, y;
        if (!font.position(text, index, number, x, y)) return false;
        const auto *g = font.data + number * 16;
        result.width = cntrl::u16(g + 4); result.height = cntrl::u16(g + 6);
        result.x = x + font_assets::signed16(g + 8);
        result.y = y + (asset ? 0 : font.entry.bytes[14]) - font_assets::signed16(g + 10);
        result.pixels = font.data + font.entry.index_length() + cntrl::u32(g + 12);
        return true;
    }
};
} // namespace cntrl::ui
