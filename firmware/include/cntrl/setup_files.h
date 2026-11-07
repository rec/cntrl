#pragma once
#include "filesystem.h"
#include "hardware.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cntrl {
// File naming, history enumeration and commit policy. File handles and their
// memory placement belong to the implementation supplying the services.
class SetupFiles : public SetupStorage {
public:
    SetupFiles(files::FileSystem &filesystem, files::File &reader,
        files::File &writer, files::Directory &history)
        : filesystem_(filesystem), reader_(reader), writer_(writer), history_(history) {}
    bool ready = false;
    bool available() const override { return ready && filesystem_.available(); }
    void choose_revision(const char *name, uint64_t number) override {
        std::snprintf(directory_, sizeof(directory_), "/cntrl/history/%s/%08lx%08lx", name,
            (unsigned long)(number >> 32), (unsigned long)(number & 0xffffffff));
        revision_ = true;
    }
    void revision_scan(const char *name, uint64_t before, bool records_only) override {
        std::snprintf(history_path_, sizeof(history_path_), "/cntrl/history/%s", name);
        history_.close(); history_.open(history_path_);
        history_before_ = before; history_records_ = records_only;
    }
    bool revision_scan_step(uint64_t &best) override {
        char candidate[32]; bool directory = false;
        if (!history_.next(candidate, sizeof(candidate), directory)) { history_.close(); return true; }
        if (directory && std::strlen(candidate) == 16) {
            char *end; uint64_t n = std::strtoull(candidate, &end, 16);
            if (!*end && n < history_before_ && n > best) {
                bool record = true;
                if (history_records_) {
                    char path[144]; std::snprintf(path, sizeof(path), "%s/%s/0.bin", history_path_, candidate);
                    record = reader_.open(path, files::Mode::read) && reader_.size() >= 208;
                    reader_.close();
                    if (!record) {
                        std::snprintf(path, sizeof(path), "%s/%s/1.bin", history_path_, candidate);
                        record = reader_.open(path, files::Mode::read) && reader_.size() >= 208;
                        reader_.close();
                    }
                }
                if (record) best = n;
            }
        }
        return false;
    }
    bool remove_revision(const char *name, uint64_t number) override {
        choose_revision(name, number); char path[144];
        for (unsigned slot = 0; slot < 2; ++slot) {
            filename(slot, path);
            if (filesystem_.exists(path) && !filesystem_.remove(path)) return false;
        }
        return true;
    }
    void choose(const char *name) override {
        revision_ = false;
        std::snprintf(directory_, sizeof(directory_), name[0] ? "/cntrl/setups/%s" : "/cntrl/selection", name);
    }
    bool next(const char *, char *) override { return false; }
    void catalogue_begin() override {}
    bool catalogue_step(char *name) override { name[0] = 0; return true; }
    unsigned size(unsigned slot) override {
        if (!ready) return 0;
        char path[144]; filename(slot, path);
        unsigned result = reader_.open(path, files::Mode::read) ? reader_.size() : 0;
        reader_.close(); return result;
    }
    bool read(unsigned slot, unsigned offset, uint8_t *data, unsigned length) override {
        if (!ready) return false;
        char path[144]; filename(slot, path);
        bool result = reader_.open(path, files::Mode::read) && reader_.read(offset, data, length);
        reader_.close(); return result;
    }
    bool begin(unsigned slot) override {
        if (!ready || (!filesystem_.exists(directory_) && !filesystem_.make_directory(directory_))) return false;
        char path[144]; filename(slot, path); position_ = 0;
        return writer_.open(path, revision_ && slot == 0 ? files::Mode::exclusive : files::Mode::truncate);
    }
    bool write(const uint8_t *data, unsigned length) override {
        if (!writer_.write(position_, data, length)) return false;
        position_ += length; return true;
    }
    bool finish() override {
        bool synced = writer_.opened() && writer_.sync(); writer_.close(); return synced;
    }
    bool flush() override { return writer_.opened() && writer_.sync(); }
    bool patch(unsigned offset, const uint8_t *data, unsigned length) override {
        return writer_.opened() && writer_.write(offset, data, length);
    }
private:
    files::FileSystem &filesystem_;
    files::File &reader_, &writer_;
    files::Directory &history_;
    uint64_t history_before_ = 0;
    unsigned position_ = 0;
    bool history_records_ = false, revision_ = false;
    char directory_[128] = {}, history_path_[96] = {};
    void filename(unsigned slot, char (&path)[144]) const { std::snprintf(path, sizeof(path), "%s/%u.bin", directory_, slot); }
};
} // namespace cntrl
