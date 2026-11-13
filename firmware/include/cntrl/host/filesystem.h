#pragma once
#include <cntrl/filesystem.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace cntrl::host {
class FileSystem;
class File final : public files::File {
public:
    explicit File(FileSystem &filesystem) : filesystem_(filesystem) {}
    bool open(const char *, files::Mode) override;
    void close() override { stream_.close(); stream_.clear(); }
    bool opened() const override { return stream_.is_open(); }
    uint32_t size() const override { return size_; }
    int read_some(uint32_t offset, uint8_t *bytes, unsigned length) override {
        stream_.clear(); stream_.seekg(offset);
        if (!stream_) return files::position_error;
        stream_.read(reinterpret_cast<char *>(bytes), length);
        return int(stream_.gcount());
    }
    bool read(uint32_t offset, uint8_t *bytes, unsigned length) override { return read_some(offset, bytes, length) == int(length); }
    int write_some(uint32_t offset, const uint8_t *bytes, unsigned length) override {
        stream_.clear(); stream_.seekp(offset);
        if (!stream_) return files::position_error;
        stream_.write(reinterpret_cast<const char *>(bytes), length);
        if (!stream_) return -1;
        size_ = std::max(size_, offset + length); return int(length);
    }
    bool write(uint32_t offset, const uint8_t *bytes, unsigned length) override { return write_some(offset, bytes, length) == int(length); }
    bool sync() override { stream_.flush(); return bool(stream_); }
    bool resize(uint32_t size) override {
        if (!sync()) return false;
        std::error_code error; std::filesystem::resize_file(path_, size, error);
        if (error) return false;
        size_ = size; return true;
    }
private:
    FileSystem &filesystem_;
    std::fstream stream_;
    std::filesystem::path path_;
    uint32_t size_ = 0;
};
class Directory final : public files::Directory {
public:
    explicit Directory(FileSystem &filesystem) : filesystem_(filesystem) {}
    bool open(const char *) override;
    bool next(char *name, unsigned capacity, bool &directory) override {
        if (cursor_ == std::filesystem::directory_iterator{}) return false;
        auto entry = *cursor_++;
        std::string value = entry.path().filename().string();
        if (value.size() >= capacity) { name[0] = 0; directory = false; return true; }
        std::memcpy(name, value.c_str(), value.size() + 1); directory = entry.is_directory();
        size_ = directory ? 0 : entry.file_size(); return true;
    }
    void close() override { cursor_ = {}; }
    uint64_t entry_size() const override { return size_; }
private:
    FileSystem &filesystem_;
    std::filesystem::directory_iterator cursor_;
    uint64_t size_ = 0;
};
class FileSystem final : public files::FileSystem {
public:
    explicit FileSystem(std::filesystem::path root) : root_(std::filesystem::absolute(root)) { std::filesystem::create_directories(root_); }
    std::filesystem::path path(const char *name) const {
        auto relative = std::filesystem::u8path(name).relative_path().lexically_normal();
        for (const auto &part : relative) if (part == "..") throw std::runtime_error("session path escapes storage root");
        return root_ / relative;
    }
    bool exists(const char *name) override { return std::filesystem::exists(path(name)); }
    bool make_directory(const char *name) override { return std::filesystem::create_directories(path(name)) || std::filesystem::is_directory(path(name)); }
    bool rename(const char *from, const char *to) override {
        std::error_code error; std::filesystem::rename(path(from), path(to), error); return !error;
    }
    bool remove(const char *name) override { std::error_code error; return std::filesystem::remove(path(name), error) && !error; }
    std::unique_ptr<files::File> file() override { return std::make_unique<File>(*this); }
    std::unique_ptr<files::Directory> directory() override { return std::make_unique<Directory>(*this); }
private:
    std::filesystem::path root_;
};
inline bool File::open(const char *name, files::Mode mode) {
    close(); path_ = filesystem_.path(name);
    bool exists = std::filesystem::exists(path_);
    if (mode == files::Mode::exclusive && exists) return false;
    if (mode == files::Mode::read && !exists) return false;
    if (mode != files::Mode::read && !exists) { std::ofstream create(path_, std::ios::binary); if (!create) return false; }
    auto flags = std::ios::binary | std::ios::in;
    if (mode != files::Mode::read) flags |= std::ios::out;
    if (mode == files::Mode::truncate) flags |= std::ios::trunc;
    stream_.open(path_, flags);
    size_ = stream_ ? uint32_t(std::filesystem::file_size(path_)) : 0;
    return bool(stream_);
}
inline bool Directory::open(const char *name) {
    close(); auto path = filesystem_.path(name);
    if (!std::filesystem::is_directory(path)) return false;
    cursor_ = std::filesystem::directory_iterator(path); return true;
}
} // namespace cntrl::host
