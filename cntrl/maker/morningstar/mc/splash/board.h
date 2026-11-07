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
        return open(slot) ? unsigned(reader_.size()) : 0;
    }
    bool read(unsigned slot, unsigned offset, uint8_t *data, unsigned length) override {
        return open(slot) && reader_.seekSet(offset)
            && reader_.read(data, length) == int(length);
    }
    bool begin(unsigned slot) override {
        if (!ready) return false;
        if (!SD.sdfs.exists("/cntrl/movies") && !SD.sdfs.mkdir("/cntrl/movies", true)) return false;
        writer_.close(); writer_key_ = slot;
        writer_ = SD.sdfs.open(temporary_, O_RDWR | O_CREAT | O_TRUNC);
        return bool(writer_);
    }
    bool write(const uint8_t *data, unsigned length) override {
        return writer_ && writer_.write(data, length) == length;
    }
    bool finish() override {
        if (!writer_) return false;
        uint8_t trailer[4];
        bool synced = writer_.fileSize() >= 20 && writer_.seekSet(writer_.fileSize() - 4)
            && writer_.read(trailer, 4) == 4 && !std::memcmp(trailer, "DONE", 4) && writer_.sync();
        writer_.close();
        char path[48]; filename(writer_key_, path);
        return synced && !SD.sdfs.exists(path) && SD.sdfs.rename(temporary_, path);
    }
private:
    bool open(unsigned slot) {
        if (!ready) return false;
        if (!reader_ || reader_key_ != slot) {
            reader_.close(); reader_key_ = slot;
            char path[48]; filename(slot, path); reader_ = SD.sdfs.open(path, O_RDONLY);
        }
        return bool(reader_);
    }
    static void filename(uint32_t key, char *path) { std::snprintf(path, 48, "/cntrl/movies/%08lx.bin", (unsigned long)key); }
    FsFile reader_, writer_;
    uint32_t reader_key_ = 0, writer_key_ = 0;
    const char *temporary_ = "/cntrl/movies/upload.tmp";
};

// The two identical outer rows share a single physical pixel transfer.
class Panels : public Output {
public:
    void set_color_cycle(bool enabled) { color_cycle_ = enabled; }
    void row(uint8_t panels, unsigned y, const uint8_t *pixels) override {
        if (!color_cycle_) {
            write_display_group(panels, 0, y, pixels, width);
            return;
        }
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
    bool color_cycle_ = false;
    Tint tints_[2] = {{63, 63, 63}, {63, 63, 63}};
    ColorWalk walks_[2] = {ColorWalk(0x9e3779b9), ColorWalk(0x243f6a89)};
};
} // namespace cntrl::splash
