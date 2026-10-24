#pragma once
#include "animation.h"
#include "color_cycle.h"
#include <cntrl/hardware.h>
#include <SD.h>
#include "displays.h"

namespace cntrl::splash {
// SD.begin(BUILTIN_SDCARD) remains the application's responsibility.
class SdSlots : public cntrl::Storage {
public:
    bool ready = false;
    unsigned size(unsigned slot) override {
        return open(slot) ? unsigned(readers_[slot].size()) : 0;
    }
    bool read(unsigned slot, unsigned offset, uint8_t *data, unsigned length) override {
        return open(slot) && readers_[slot].seekSet(offset)
            && readers_[slot].read(data, length) == int(length);
    }
    bool begin(unsigned slot) override {
        if (!ready) return false;
        readers_[slot].close();
        writer_ = SD.sdfs.open(names_[slot], O_RDWR | O_CREAT | O_TRUNC);
        return bool(writer_);
    }
    bool write(const uint8_t *data, unsigned length) override {
        return writer_ && writer_.write(data, length) == length;
    }
    bool finish() override {
        if (!writer_) return false;
        bool synced = writer_.sync();
        writer_.close();
        return synced;
    }
private:
    bool open(unsigned slot) {
        if (!ready) return false;
        if (!readers_[slot]) readers_[slot] = SD.sdfs.open(names_[slot], O_RDONLY);
        return bool(readers_[slot]);
    }
    FsFile readers_[2], writer_;
    const char *names_[2] = {"/cntrl-splash-0.bin", "/cntrl-splash-1.bin"};
};

// The two identical outer rows share a single physical pixel transfer.
class Panels : public Output {
public:
    void row(uint8_t panels, unsigned y, const uint8_t *pixels) override {
        // Sample both walks at the same frame boundary; hold them across rows.
        if (panels == 0b101 && y == 0) {
            uint32_t now = millis();
            for (unsigned i = 0; i < 2; ++i) tints_[i] = walks_[i].at(now);
        }
        Tint tint = tints_[panels == 0b010 ? 1 : 0];
        uint8_t colored[row_bytes];
        for (unsigned x = 0; x < width; ++x) {
            uint16_t pixel = uint16_t(pixels[2 * x]) << 8 | pixels[2 * x + 1];
            pixel = tint_white(pixel, tint);
            colored[2 * x] = uint8_t(pixel >> 8);
            colored[2 * x + 1] = uint8_t(pixel);
        }
        write_display_group(panels, 0, y, colored, width);
    }
private:
    Tint tints_[2] = {{63, 63, 63}, {63, 63, 63}};
    ColorWalk walks_[2] = {ColorWalk(0x9e3779b9), ColorWalk(0x243f6a89)};
};
} // namespace cntrl::splash
