#pragma once
#include <cntrl/hardware.h>
#include <cstring>

namespace cntrl::ui {
class DoomArtwork final : public BitmapSource {
public:
    static constexpr unsigned width = 160, height = 128;
    bool read_bitmap(uint32_t, unsigned offset, uint16_t *out, unsigned count) const override {
        const unsigned first = offset / sizeof(uint16_t);
        if (offset % sizeof(uint16_t) || first > width * height || count > width * height - first) return false;
        std::memcpy(out, pixels + first, count * sizeof(uint16_t));
        return true;
    }
private:
    inline static constexpr uint16_t pixels[]
#ifdef __IMXRT1062__
        __attribute__((section(".progmem.doom_artwork")))
#endif
        = {
#include "doom_pixels.inc"
    };
    static_assert(sizeof(pixels) == width * height * sizeof(uint16_t));
};
inline constexpr DoomArtwork doom_artwork{};
} // namespace cntrl::ui
