#pragma once
#include <cntrl/animation/animation.h>
#include <cntrl/animation/color_cycle.h>
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
class Panels : public cntrl::animation::Output {
public:
    void set_color_cycle(bool enabled) { color_cycle_ = enabled; }
    bool begin(unsigned width, unsigned height, unsigned streams) override {
        return width == 160 && height == 128 && streams == 2;
    }
    bool ready() const override { return true; }
    bool pixels(unsigned stream, unsigned x, unsigned y, const uint8_t *pixels, unsigned count) override {
        uint8_t panels = stream == 0 ? 0b101 : 0b010;
        if (!color_cycle_) {
            write_display_group(panels, x, y, pixels, count);
            return true;
        }
        if (!stream && !x && !y) {
            uint32_t now = millis();
            for (unsigned i = 0; i < 2; ++i) tints_[i] = walks_[i].at(now);
        }
        cntrl::animation::Tint tint = tints_[stream];
        uint8_t colored[cntrl::animation::packet_bytes];
        for (unsigned i = 0; i < count; ++i) {
            uint16_t pixel = uint16_t(pixels[2 * i]) << 8 | pixels[2 * i + 1];
            pixel = cntrl::animation::tint_white(pixel, tint);
            colored[2 * i] = uint8_t(pixel >> 8);
            colored[2 * i + 1] = uint8_t(pixel);
        }
        write_display_group(panels, x, y, colored, count);
        return true;
    }
private:
    bool color_cycle_ = false;
    cntrl::animation::Tint tints_[2] = {{63, 63, 63}, {63, 63, 63}};
    cntrl::animation::ColorWalk walks_[2] = {cntrl::animation::ColorWalk(0x9e3779b9), cntrl::animation::ColorWalk(0x243f6a89)};
};
} // namespace cntrl::splash
