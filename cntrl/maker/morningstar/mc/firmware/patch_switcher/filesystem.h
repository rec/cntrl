#pragma once
#include <SD.h>
#include <cntrl/filesystem.h>

namespace cntrl::device {
class SdFile : public files::File {
public:
    bool open(const char *path, files::Mode mode) override {
        close();
        oflag_t flags = mode == files::Mode::read ? O_RDONLY : O_CREAT | O_RDWR;
        if (mode == files::Mode::exclusive) flags |= O_EXCL;
        if (mode == files::Mode::truncate) flags |= O_TRUNC;
        file_ = SD.sdfs.open(path, flags);
        return bool(file_);
    }
    void close() override { file_.close(); }
    bool opened() const override { return bool(file_); }
    uint32_t size() const override { return const_cast<FsFile &>(file_).size(); }
    bool read(uint32_t offset, uint8_t *data, unsigned length) override {
        return file_ && file_.seekSet(offset) && file_.read(data, length) == int(length);
    }
    bool write(uint32_t offset, const uint8_t *data, unsigned length) override {
        return file_ && file_.seekSet(offset) && file_.write(data, length) == length;
    }
    bool sync() override { return file_ && file_.sync(); }
private:
    FsFile file_;
};
class SdDirectory : public files::Directory {
public:
    bool open(const char *path) override {
        close(); failed_ = false; directory_ = SD.sdfs.open(path, O_RDONLY);
        return directory_ && directory_.isDirectory();
    }
    bool next(char *name, unsigned capacity, bool &is_directory) override {
        FsFile entry;
        if (!entry.openNext(&directory_, O_RDONLY)) { failed_ = directory_.getError(); return false; }
        bool named = entry.getName(name, capacity);
        size_ = entry.size(); is_directory = entry.isDirectory(); entry.close();
        if (!named) failed_ = true;
        return named;
    }
    void close() override { directory_.close(); }
    uint64_t entry_size() const override { return size_; }
    bool failed() const override { return failed_; }
private:
    FsFile directory_;
    uint64_t size_ = 0;
    bool failed_ = false;
};
class SdFileSystem : public files::FileSystem {
public:
    bool exists(const char *path) override { return SD.sdfs.exists(path); }
    bool make_directory(const char *path) override { return SD.sdfs.mkdir(path, true); }
    bool rename(const char *from, const char *to) override { return SD.sdfs.rename(from, to); }
    bool remove(const char *path) override { return SD.sdfs.remove(path); }
    bool available() const override { return SD.sdfs.card() && !SD.sdfs.card()->errorCode(); }
    std::unique_ptr<files::Directory> directory() override { return std::make_unique<SdDirectory>(); }
};
} // namespace cntrl::device
