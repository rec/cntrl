#pragma once
#include <cntrl/animation/animation.h>
#include <cntrl/asset_storage.h>
#include "../firmware/patch_switcher/filesystem.h"
#include <SD.h>
#include "displays.h"

namespace cntrl::splash {
class SdSlots : public cntrl::AssetStorage {
public:
    SdSlots() : AssetStorage(filesystem_, reader_, writer_) {}
private:
    cntrl::device::SdFileSystem filesystem_;
    cntrl::device::SdFile reader_, writer_;
};

// The two identical outer rows share a single physical pixel transfer.
class Panels : public cntrl::animation::Output {
public:
    bool begin(unsigned width, unsigned height, unsigned streams) override {
        return width == 160 && height == 128 && streams == 2;
    }
    bool ready() const override { return true; }
    bool pixels(unsigned stream, unsigned x, unsigned y, const uint8_t *pixels, unsigned count) override {
        uint8_t panels = stream == 0 ? 0b101 : 0b010;
        write_display_group(panels, x, y, pixels, count);
        return true;
    }
};
} // namespace cntrl::splash
