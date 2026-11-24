#pragma once
#include "display.h"
#include <cntrl/hardware.h>
#include <cntrl/memon_settings.h>

namespace cntrl::ui {
// Draw the two cap positions from the sprite's 96-pixel geometry. Keeping the
// bevel and cap separate lets the same artwork use the setup's RGB palette.
class MemonArtwork final : public BitmapSource {
public:
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(memon_artwork_configure)
#endif
    void configure(const MemonSettings &settings) {
        for (unsigned tile = 0; tile < 4; ++tile) {
            for (unsigned on = 0; on < 2; ++on) {
                const int delta = on ? settings.color_offset : -int(settings.color_offset);
                for (unsigned layer = 0; layer < 3; ++layer) {
                    constexpr int bevel[] = {-62, 22, 0};
                    cap_[tile][on][layer] = color(settings.colors[tile], delta + bevel[layer]);
                }
            }
        }
        background_ = color(settings.colors[5]);
    }

    Bitmap bitmap(Viewport view, unsigned lit) const {
        return {this, uint32_t(view.width << 18 | view.height << 4 | lit), 0,
                uint16_t(view.width), uint16_t(view.height)};
    }

    static unsigned tile_size(Viewport view) { return std::min(view.width, view.height) / 2; }

#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(memon_artwork_read)
#endif
    bool read_bitmap(uint32_t generation, unsigned offset, uint16_t *pixels, unsigned count) const override {
        const unsigned width = generation >> 18, height = (generation >> 4) & 0x3fff;
        const unsigned size = tile_size({width, height});
        if (!size || offset / 2 > width * height || count > width * height - offset / 2) return false;
        const unsigned left = (width - 2 * size) / 2, top = (height - 2 * size) / 2;
        for (unsigned i = 0; i < count; ++i) {
            const unsigned index = offset / 2 + i;
            const unsigned x = index % width - left, y = index / width - top;
            if (x >= 2 * size || y >= 2 * size) { pixels[i] = background_; continue; }
            const unsigned tile = (y / size) * 2 + x / size;
            const bool on = generation & (1u << tile);
            pixels[i] = pixel(x % size * 96 / size, y % size * 96 / size, tile, on);
        }
        return true;
    }

    uint16_t label_color(unsigned tile, bool lit) const {
        const auto face = cap_[tile][lit][2];
        const unsigned brightness = 54 * (face >> 11) * 2
            + 183 * ((face >> 5) & 63) + 19 * (face & 31) * 2;
        return brightness > 6400 ? 0 : 0xffff;
    }

    static constexpr uint16_t color(const uint8_t (&rgb)[3], int delta = 0) {
        return uint16_t((std::clamp(rgb[0] + delta, 0, 255) >> 3) << 11
                      | (std::clamp(rgb[1] + delta, 0, 255) >> 2) << 5
                      | (std::clamp(rgb[2] + delta, 0, 255) >> 3));
    }

private:
    struct Layer {
        int left, top, right, bottom, radius;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(memon_artwork_covers)
#endif
        bool covers(int x, int y) const {
            if (x < left || x > right || y < top || y > bottom) return false;
            const int dx = x - std::clamp(x, left + radius, right - radius);
            const int dy = y - std::clamp(y, top + radius, bottom - radius);
            return dx * dx + dy * dy <= radius * radius;
        }
    };

    static constexpr Layer frame[] = {
        {9,9,86,86,3}, {7,7,88,88,4}, {5,5,90,90,5},
        {3,4,92,93,5}, {2,2,93,92,6}, {1,1,94,94,7},
    };
    static constexpr uint8_t metal[6][3] = {
        {16,18,20}, {36,40,43}, {104,110,113},
        {75,80,84}, {149,155,157}, {52,56,59},
    };
    static constexpr Layer caps[2][3] = {
        {{13,12,82,77,3}, {11,10,84,79,4}, {11,10,84,85,4}},
        {{13,17,82,83,3}, {11,15,84,84,4}, {11,13,84,84,4}},
    };
    uint16_t cap_[4][2][3] = {};
    uint16_t background_ = 0;

    uint16_t pixel(unsigned x, unsigned y, unsigned tile, bool on) const {
        for (unsigned i = 0; i < 3; ++i)
            if (caps[on][i].covers(x, y)) return cap_[tile][on][2 - i];
        for (unsigned i = 0; i < 6; ++i)
            if (frame[i].covers(x, y)) return color(metal[i]);
        return background_;
    }
};
} // namespace cntrl::ui
