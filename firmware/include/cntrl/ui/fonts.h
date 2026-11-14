#pragma once
#include "font_assets.h"
#include <algorithm>

namespace cntrl::ui {
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
    bool supports(unsigned face, unsigned size, uint32_t asset = 0) const override {
        font_assets::View view; return find(face, size, asset, view);
    }
    Bounds bounds(unsigned face, unsigned size, uint32_t asset, const char *text) const override {
        font_assets::View view; Bounds result;
        if (!find(face, size, asset, view)) return result;
        view.bounds(text, result.left, result.top, result.width, result.height, face == 255 ? 0 : view.entry.bytes[14]);
        return result;
    }
    bool glyph(unsigned face, unsigned size, uint32_t asset, const char *text, unsigned index, Glyph &result) const override {
        font_assets::View view; return find(face, size, asset, view) && make_glyph(view, text, index, result, face == 255);
    }
private:
    const EmbeddedFont *fonts_;
    unsigned count_;
    bool find(unsigned face, unsigned size, uint32_t asset, font_assets::View &view) const {
        if (face == 255) {
            const auto *loaded = font_assets::cache.get(asset, size);
            if (!loaded) return false;
            view.entry = loaded->entry; view.data = loaded->data.data;
            return true;
        }
        for (unsigned f = 0; f < count_; ++f) {
            const auto &font = fonts_[f];
            if (font.face != face || font.length < 8 || std::memcmp(font.bytes, "CFN2", 4)) continue;
            unsigned offset = 8, count = cntrl::u32(font.bytes + 4);
            for (unsigned i = 0; i < count; ++i) {
                font_assets::Entry entry;
                if (offset > font.length || font.length - offset < sizeof(entry.bytes)) return false;
                std::memcpy(entry.bytes, font.bytes + offset, sizeof(entry.bytes)); offset += sizeof(entry.bytes);
                if (!entry.valid() || entry.length() > font.length - offset) return false;
                if (entry.size() == size) {
                    view.entry = entry; view.data = font.bytes + offset;
                    return true;
                }
                offset += entry.length();
            }
        }
        return false;
    }
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
