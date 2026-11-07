#pragma once
#include "filesystem.h"
#include "hardware.h"
#include <cstdio>

namespace cntrl {
// Numbered journal slots share one writer and retain independent read handles.
class SlotStorage : public Storage {
public:
    SlotStorage(files::FileSystem &filesystem, files::File &writer,
        files::File *const *readers, unsigned count, const char *directory, const bool &ready)
        : filesystem_(filesystem), writer_(writer), readers_(readers), count_(count), directory_(directory), ready_(ready) {}
    unsigned size(unsigned slot) override { return open(slot) ? readers_[slot]->size() : 0; }
    bool read(unsigned slot, unsigned offset, uint8_t *data, unsigned length) override {
        return open(slot) && readers_[slot]->read(offset, data, length);
    }
    bool begin(unsigned slot) override {
        if (slot >= count_ || !ready_ || !filesystem_.available()
            || (!filesystem_.exists(directory_) && !filesystem_.make_directory(directory_))) return false;
        readers_[slot]->close(); writer_.close(); position_ = 0;
        char path[160]; return filename(slot, path) && writer_.open(path, files::Mode::truncate);
    }
    bool write(const uint8_t *data, unsigned length) override {
        if (!writer_.opened() || !writer_.write(position_, data, length)) return false;
        position_ += length; return true;
    }
    bool finish() override {
        bool good = writer_.opened() && writer_.sync(); writer_.close(); return good;
    }
private:
    files::FileSystem &filesystem_; files::File &writer_;
    files::File *const *readers_; unsigned count_;
    const char *directory_; const bool &ready_;
    unsigned position_ = 0;
    bool filename(unsigned slot, char (&path)[160]) const {
        int n = std::snprintf(path, sizeof(path), "%s/%u.bin", directory_, slot);
        return n > 0 && unsigned(n) < sizeof(path);
    }
    bool open(unsigned slot) {
        if (slot >= count_ || !ready_ || !filesystem_.available()) return false;
        if (!readers_[slot]->opened()) {
            char path[160]; return filename(slot, path) && readers_[slot]->open(path, files::Mode::read);
        }
        return true;
    }
};
} // namespace cntrl
