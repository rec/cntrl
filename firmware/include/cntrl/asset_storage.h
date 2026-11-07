#pragma once
#include "filesystem.h"
#include "hardware.h"
#include <cstdio>
#include <cstring>

namespace cntrl {
// Content-addressed movie files become visible only after their committed
// trailer has been synced. An existing key is never overwritten.
class AssetStorage : public Storage {
public:
    AssetStorage(files::FileSystem &filesystem, files::File &reader, files::File &writer)
        : filesystem_(filesystem), reader_(reader), writer_(writer) {}
    bool ready = false;
    unsigned size(unsigned key) override { return open(key) ? reader_.size() : 0; }
    bool read(unsigned key, unsigned offset, uint8_t *data, unsigned length) override {
        return open(key) && reader_.read(offset, data, length);
    }
    bool begin(unsigned key) override {
        if (!ready || !filesystem_.available()
            || (!filesystem_.exists(directory_) && !filesystem_.make_directory(directory_))) return false;
        writer_.close(); writer_key_ = key; position_ = 0;
        return writer_.open(temporary_, files::Mode::truncate);
    }
    bool write(const uint8_t *data, unsigned length) override {
        if (!writer_.opened() || !writer_.write(position_, data, length)) return false;
        position_ += length; return true;
    }
    bool finish() override {
        if (!writer_.opened()) return false;
        uint8_t trailer[4]; unsigned length = writer_.size();
        bool good = length >= 20 && writer_.read(length - 4, trailer, 4)
            && !std::memcmp(trailer, "DONE", 4) && writer_.sync();
        writer_.close(); char path[48]; filename(writer_key_, path);
        return good && !filesystem_.exists(path) && filesystem_.rename(temporary_, path);
    }
private:
    files::FileSystem &filesystem_; files::File &reader_, &writer_;
    uint32_t reader_key_ = 0, writer_key_ = 0;
    unsigned position_ = 0;
    static constexpr const char *directory_ = "/cntrl/movies";
    static constexpr const char *temporary_ = "/cntrl/movies/upload.tmp";
    static void filename(uint32_t key, char (&path)[48]) {
        std::snprintf(path, sizeof(path), "/cntrl/movies/%08lx.bin", (unsigned long)key);
    }
    bool open(unsigned key) {
        if (!ready || !filesystem_.available()) return false;
        if (!reader_.opened() || reader_key_ != key) {
            reader_.close(); reader_key_ = key;
            char path[48]; filename(key, path); return reader_.open(path, files::Mode::read);
        }
        return true;
    }
};
} // namespace cntrl
