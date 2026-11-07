#pragma once
#include <cntrl/filesystem.h>
#include <cstring>
#include <filesystem>
#include <fstream>

class TestFileSystem : public cntrl::files::FileSystem {
public:
    explicit TestFileSystem(std::filesystem::path root) : root_(std::move(root)) {}
    std::filesystem::path path(const char *name) const { return root_ / std::filesystem::path(name).relative_path(); }
    bool exists(const char *name) override { return std::filesystem::exists(path(name)); }
    bool make_directory(const char *name) override {
        std::error_code error;
        std::filesystem::create_directories(path(name), error);
        return !error;
    }
    bool rename(const char *from, const char *to) override {
        std::error_code error; std::filesystem::rename(path(from), path(to), error); return !error;
    }
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
        file_.open(path_, flags); return bool(file_);
    }
    void close() override { file_.close(); file_.clear(); }
    bool opened() const override { return file_.is_open(); }
    uint32_t size() const override { return uint32_t(std::filesystem::file_size(path_)); }
    bool read(uint32_t offset, uint8_t *bytes, unsigned length) override {
        file_.clear(); file_.seekg(offset);
        file_.read(reinterpret_cast<char *>(bytes), length); return file_.gcount() == length;
    }
    bool write(uint32_t offset, const uint8_t *bytes, unsigned length) override {
        file_.clear(); file_.seekp(offset);
        file_.write(reinterpret_cast<const char *>(bytes), length); return bool(file_);
    }
    bool sync() override { file_.flush(); return bool(file_); }
private:
    TestFileSystem &filesystem_;
    std::filesystem::path path_;
    std::fstream file_;
};

class TestDirectory : public cntrl::files::Directory {
public:
    explicit TestDirectory(TestFileSystem &filesystem) : filesystem_(filesystem) {}
    bool open(const char *name) override {
        std::error_code error;
        cursor_ = std::filesystem::directory_iterator(filesystem_.path(name), error);
        return !error;
    }
    bool next(char *name, unsigned capacity, bool &is_directory) override {
        while (cursor_ != std::filesystem::directory_iterator()) {
            auto entry = *cursor_++; auto text = entry.path().filename().string();
            if (text.size() + 1 > capacity) continue;
            std::memcpy(name, text.c_str(), text.size() + 1);
            is_directory = entry.is_directory(); return true;
        }
        return false;
    }
    void close() override { cursor_ = std::filesystem::directory_iterator(); }
private:
    TestFileSystem &filesystem_;
    std::filesystem::directory_iterator cursor_;
};
