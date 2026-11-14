#pragma once
#include "filesystem.h"
#include "hardware.h"
#include "checked_list.h"
#include <cstdio>

namespace cntrl {
// Numbered journal slots share one writer and retain independent read handles.
class SlotStorage : public Storage {
public:
    SlotStorage(files::FileSystem &filesystem, files::File &writer,
        unsigned count, const char *directory, const bool &ready)
        : filesystem_(filesystem), writer_(writer), count_(count), directory_(directory), ready_(ready) {}
    unsigned size(unsigned slot) override { auto *reader = open(slot); return reader ? reader->size() : 0; }
    bool read(unsigned slot, unsigned offset, uint8_t *data, unsigned length) override {
        auto *reader = open(slot); return reader && reader->read(offset, data, length);
    }
    bool begin(unsigned slot) override {
        if (slot >= count_ || !ready_ || !filesystem_.available()
            || (!filesystem_.exists(directory_) && !filesystem_.make_directory(directory_))) return false;
        for (auto &reader : readers_) if (reader.slot == slot) reader.file->close();
        writer_.close(); position_ = 0;
        char path[160]; return filename(slot, path) && writer_.open(path, files::Mode::truncate);
    }
    bool write(const uint8_t *data, unsigned length) override {
        if (!writer_.opened() || !writer_.write(position_, data, length)) return false;
        position_ += length; return true;
    }
    bool finish() override {
        bool good = writer_.opened() && writer_.sync(); writer_.close(); return good;
    }
    void close() {
        writer_.close();
        for (auto &reader : readers_) reader.file->close();
    }
private:
    files::FileSystem &filesystem_; files::File &writer_;
    struct Reader { unsigned slot; std::unique_ptr<files::File> file; };
    CheckedList<Reader> readers_;
    unsigned count_;
    const char *directory_; const bool &ready_;
    unsigned position_ = 0;
    bool filename(unsigned slot, char (&path)[160]) const {
        int n = std::snprintf(path, sizeof(path), "%s/%u.bin", directory_, slot);
        return n > 0 && unsigned(n) < sizeof(path);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(slot_storage_open)
#endif
    files::File *open(unsigned slot) {
        if (slot >= count_ || !ready_ || !filesystem_.available()) return nullptr;
        for (auto &reader : readers_) if (reader.slot == slot) {
            if (!reader.file->opened()) {
                char path[160]; if (!filename(slot, path) || !reader.file->open(path, files::Mode::read)) return nullptr;
            }
            return reader.file.get();
        }
        auto file = filesystem_.file(); char path[160];
        if (!file || !filename(slot, path) || !file->open(path, files::Mode::read)) return nullptr;
        auto *reader = readers_.append({slot, std::move(file)});
        return reader ? reader->file.get() : nullptr;
    }
};
} // namespace cntrl
