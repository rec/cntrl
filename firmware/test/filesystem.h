#pragma once
#include <cntrl/filesystem.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

class TestFileSystem : public cntrl::files::FileSystem {
public:
    explicit TestFileSystem(std::filesystem::path root) : root_(std::move(root)) {}
    std::filesystem::path path(const char *name) const { return root_ / std::filesystem::u8path(name).relative_path(); }
    bool exists(const char *name) override { return std::filesystem::exists(path(name)); }
    bool make_directory(const char *name) override {
        std::error_code error;
        std::filesystem::create_directories(path(name), error);
        return !error;
    }
    bool rename(const char *from, const char *to) override {
        std::error_code error; std::filesystem::rename(path(from), path(to), error); return !error;
    }
    std::unique_ptr<cntrl::files::Directory> directory() override;
    bool remove(const char *name) override { return std::filesystem::remove(path(name)); }
private:
    std::filesystem::path root_;
};

class TestFile : public cntrl::files::File {
public:
    explicit TestFile(TestFileSystem &filesystem) : filesystem_(filesystem) {}
    bool open(const char *name, cntrl::files::Mode mode) override {
        close(); path_ = filesystem_.path(name);
        bool exists = std::filesystem::exists(path_);
        if (mode == cntrl::files::Mode::exclusive && exists) return false;
        if (mode != cntrl::files::Mode::read && !exists) {
            std::ofstream create(path_, std::ios::binary); if (!create) return false;
        }
        auto flags = std::ios::binary | std::ios::in;
        if (mode != cntrl::files::Mode::read) flags |= std::ios::out;
        if (mode == cntrl::files::Mode::truncate) flags |= std::ios::trunc;
        file_.open(path_, flags); size_ = file_ ? uint32_t(std::filesystem::file_size(path_)) : 0; return bool(file_);
    }
    void close() override { file_.close(); file_.clear(); }
    bool opened() const override { return file_.is_open(); }
    uint32_t size() const override { return size_; }
    int read_some(uint32_t offset, uint8_t *bytes, unsigned length) override {
        file_.clear(); file_.seekg(offset);
        if (!file_) return cntrl::files::position_error;
        file_.read(reinterpret_cast<char *>(bytes), length); return int(file_.gcount());
    }
    bool read(uint32_t offset, uint8_t *bytes, unsigned length) override { return read_some(offset, bytes, length) == int(length); }
    int write_some(uint32_t offset, const uint8_t *bytes, unsigned length) override {
        file_.clear(); file_.seekp(offset);
        if (!file_) return cntrl::files::position_error;
        file_.write(reinterpret_cast<const char *>(bytes), length);
        if (!file_) return 0;
        size_ = std::max(size_, offset + length); return int(length);
    }
    bool write(uint32_t offset, const uint8_t *bytes, unsigned length) override { return write_some(offset, bytes, length) == int(length); }
    bool resize(uint32_t length) override {
        file_.flush(); if (!file_) return false;
        std::error_code error; std::filesystem::resize_file(path_, length, error);
        if (error) return false;
        size_ = length; return true;
    }
    bool sync() override { file_.flush(); return bool(file_); }
private:
    TestFileSystem &filesystem_;
    std::filesystem::path path_;
    std::fstream file_;
    uint32_t size_ = 0;
};

class TestDirectory : public cntrl::files::Directory {
public:
    explicit TestDirectory(TestFileSystem &filesystem) : filesystem_(filesystem) {}
    bool open(const char *name) override {
        std::error_code error;
        cursor_ = std::filesystem::directory_iterator(filesystem_.path(name), error);
        failed_ = bool(error); return !error;
    }
    bool next(char *name, unsigned capacity, bool &is_directory) override {
        while (cursor_ != std::filesystem::directory_iterator()) {
            auto entry = *cursor_++; auto text = entry.path().filename().u8string();
            if (text.size() + 1 > capacity) { failed_ = true; return false; }
            std::memcpy(name, text.c_str(), text.size() + 1);
            is_directory = entry.is_directory();
            size_ = is_directory ? 0 : entry.file_size(); return true;
        }
        return false;
    }
    void close() override { cursor_ = std::filesystem::directory_iterator(); }
    uint64_t entry_size() const override { return size_; }
    bool failed() const override { return failed_; }
private:
    TestFileSystem &filesystem_;
    std::filesystem::directory_iterator cursor_;
    uint64_t size_ = 0;
    bool failed_ = false;
};

inline std::unique_ptr<cntrl::files::Directory> TestFileSystem::directory() { return std::make_unique<TestDirectory>(*this); }
