#pragma once
#include <filesystem>
#include <fstream>
#include <string>

constexpr int O_RDONLY = 0, O_RDWR = 1, O_CREAT = 2, O_TRUNC = 4;
class FsFile {
public:
    FsFile() = default;
    FsFile(const std::filesystem::path &path, int flags) : path_(path) {
        auto mode = std::ios::binary | std::ios::in;
        if (flags & O_RDWR) mode |= std::ios::out;
        if (flags & O_TRUNC) mode |= std::ios::trunc;
        file_.open(path, mode);
    }
    explicit operator bool() const { return file_.is_open(); }
    unsigned size() const { return unsigned(std::filesystem::file_size(path_)); }
    bool seekSet(unsigned offset) { file_.clear(); file_.seekg(offset); return bool(file_); }
    int read(void *data, unsigned length) { file_.read(static_cast<char *>(data), length); return int(file_.gcount()); }
    unsigned write(const void *data, unsigned length) { file_.write(static_cast<const char *>(data), length); return file_ ? length : 0; }
    bool sync() { file_.flush(); return bool(file_); }
    void close() { file_.close(); }
private:
    std::filesystem::path path_;
    std::fstream file_;
};
struct TestFs {
    std::filesystem::path root;
    FsFile open(const char *path, int flags) { return FsFile(root / (path + 1), flags); }
    bool exists(const char *path) { return std::filesystem::exists(root / (path + 1)); }
    bool mkdir(const char *path, bool) { return std::filesystem::create_directories(root / (path + 1)); }
    bool rename(const char *from, const char *to) {
        std::error_code error; std::filesystem::rename(root / (from + 1), root / (to + 1), error); return !error;
    }
};
inline struct TestSd { TestFs sdfs; } SD;
