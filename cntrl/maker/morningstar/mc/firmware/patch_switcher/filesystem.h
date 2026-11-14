#ifndef CNTRL_DEVICE_FILESYSTEM_H
#define CNTRL_DEVICE_FILESYSTEM_H
#include <SD.h>
#include <cntrl/filesystem.h>
#include <cntrl/memory.h>
#include <cntrl/deployment.h>

namespace cntrl::device {
class SdFile : public files::File, public CheckedHeapAllocation {
public:
    bool open(const char *path, files::Mode mode) override {
        close();
        char mapped[224]; if (!persistent_path(path, mapped)) return false;
        oflag_t flags = mode == files::Mode::read ? O_RDONLY : O_CREAT | O_RDWR;
        if (mode == files::Mode::exclusive) flags |= O_EXCL;
        if (mode == files::Mode::truncate) flags |= O_TRUNC;
        file_ = SD.sdfs.open(mapped, flags);
        return bool(file_);
    }
    void close() override { file_.close(); }
    bool opened() const override { return bool(file_); }
    uint32_t size() const override { return const_cast<FsFile &>(file_).size(); }
    int read_some(uint32_t offset, uint8_t *data, unsigned length) override {
        if (!file_) return -1;
        if (!file_.seekSet(offset)) return files::position_error;
        return file_.read(data, length);
    }
    bool read(uint32_t offset, uint8_t *data, unsigned length) override {
        return read_some(offset, data, length) == int(length);
    }
    int write_some(uint32_t offset, const uint8_t *data, unsigned length) override {
        if (!file_) return 0;
        if (!file_.seekSet(offset)) return files::position_error;
        return int(file_.write(data, length));
    }
    bool write(uint32_t offset, const uint8_t *data, unsigned length) override {
        return write_some(offset, data, length) == int(length);
    }
    bool resize(uint32_t length) override { return file_ && file_.truncate(length); }
    bool sync() override { return file_ && file_.sync(); }
private:
    FsFile file_;
};
class SdDirectory : public files::Directory, public CheckedHeapAllocation {
public:
    bool open(const char *path) override {
        char mapped[224]; if (!persistent_path(path, mapped)) return false;
        close(); failed_ = false; directory_ = SD.sdfs.open(mapped, O_RDONLY);
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
    bool exists(const char *path) override {
        char mapped[224]; return persistent_path(path, mapped) && SD.sdfs.exists(mapped);
    }
    bool make_directory(const char *path) override {
        char mapped[224]; return persistent_path(path, mapped) && SD.sdfs.mkdir(mapped, true);
    }
    bool rename(const char *from, const char *to) override {
        char first[224], second[224];
        return persistent_path(from, first) && persistent_path(to, second) && SD.sdfs.rename(first, second);
    }
    bool remove(const char *path) override {
        char mapped[224]; return persistent_path(path, mapped) && SD.sdfs.remove(mapped);
    }
    bool available() const override { return SD.sdfs.card() && !SD.sdfs.card()->errorCode(); }
    std::unique_ptr<files::Directory> directory() override { return std::unique_ptr<files::Directory>(new (std::nothrow) SdDirectory); }
    std::unique_ptr<files::File> file() override { return std::unique_ptr<files::File>(new (std::nothrow) SdFile); }
};
} // namespace cntrl::device
#endif
