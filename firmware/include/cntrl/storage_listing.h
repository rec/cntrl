#pragma once
#include "filesystem.h"
#include "checked_array.h"
#include <cstdio>
#include <climits>
#include <cstring>
#include <memory>

namespace cntrl {
// Pull one entry per acknowledged request, retaining only the directory stack.
class StorageListing {
public:
    struct Detail { const char *key; const char *value; };
    explicit StorageListing(files::FileSystem &filesystem) : filesystem_(filesystem) {}
    ~StorageListing() { cancel(); }
    // Detail strings belong to the caller and remain valid until cancel/END.
    bool begin(const Detail *details = nullptr, unsigned count = 0) {
        CheckedObject<Level> candidate;
        if (!candidate.create() || !candidate->path.resize(2)) return false;
        candidate->path.data[0] = '/'; candidate->path.data[1] = 0;
        candidate->directory = filesystem_.directory();
        if (!candidate->directory || !candidate->directory->open("/")) return false;
        cancel(); current_ = std::move(candidate); index_ = 0;
        details_ = details; detail_count_ = count; detail_index_ = 0; return true;
    }
    void cancel() {
        while (current_) { auto parent = std::move(current_->parent); current_.reset(); current_ = std::move(parent); }
        details_ = nullptr; detail_count_ = detail_index_ = 0;
    }
    void next(char *line, unsigned capacity) {
        if (detail_index_ < detail_count_) {
            const auto &detail = details_[detail_index_++];
            unsigned used = 0;
            const char prefix[] = "CNTRL DETAIL ";
            for (char c : prefix) if (c) {
                if (used + 1 >= capacity) { fail(line, capacity); return; }
                line[used++] = c;
            }
            if (!hex(line, capacity, used, detail.key) || used + 1 >= capacity) { fail(line, capacity); return; }
            line[used++] = ' ';
            if (!hex(line, capacity, used, detail.value)) { fail(line, capacity); return; }
            line[used] = 0; return;
        }
        while (current_) {
            char name[256]; bool directory = false;
            if (!current_->directory->next(name, sizeof(name), directory)) {
                if (current_->directory->failed()) { fail(line, capacity); return; }
                auto parent = std::move(current_->parent); current_ = std::move(parent); continue;
            }
            unsigned id = ++index_; uint64_t size = current_->directory->entry_size();
            int used = std::snprintf(line, capacity, "CNTRL ENTRY %u %u %c %08lx%08lx ",
                id, current_->id, directory ? 'D' : 'F',
                (unsigned long)(size >> 32), (unsigned long)(size & 0xffffffff));
            if (used < 0 || unsigned(used) >= capacity) { fail(line, capacity); return; }
            for (const unsigned char *p = reinterpret_cast<const unsigned char *>(name); *p; ++p) {
                if (unsigned(used) + 2 >= capacity) { fail(line, capacity); return; }
                used += std::snprintf(line + used, capacity - unsigned(used), "%02x", *p);
            }
            if (directory) {
                CheckedObject<Level> child;
                const unsigned parent = current_->path.size - 1, length = unsigned(std::strlen(name));
                const unsigned separator = parent == 1 ? 0 : 1;
                if (uint64_t(parent) + separator + length + 1 > UINT_MAX || !child.create()
                    || !child->path.resize(parent + separator + length + 1)) { fail(line, capacity); return; }
                child->directory = filesystem_.directory();
                std::memcpy(child->path.data, current_->path.data, parent);
                if (separator) child->path.data[parent] = '/';
                std::memcpy(child->path.data + parent + separator, name, length + 1);
                if (!child->directory || !child->directory->open(reinterpret_cast<const char *>(child->path.data))) { fail(line, capacity); return; }
                child->id = id; child->parent = std::move(current_); current_ = std::move(child);
            }
            return;
        }
        std::snprintf(line, capacity, "CNTRL STORAGE END %u", index_);
    }
private:
    struct Level {
        Level() = default;
        Level(Level &&) = default;
        Level &operator=(Level &&) = default;
        std::unique_ptr<files::Directory> directory;
        MemoryBuffer path;
        unsigned id = 0;
        CheckedObject<Level> parent;
        ~Level() { if (directory) directory->close(); }
    };
    files::FileSystem &filesystem_;
    CheckedObject<Level> current_;
    unsigned index_ = 0;
    const Detail *details_ = nullptr;
    unsigned detail_count_ = 0, detail_index_ = 0;
    static bool hex(char *line, unsigned capacity, unsigned &used, const char *value) {
        if (!value || !*value) return false;
        for (const unsigned char *p = reinterpret_cast<const unsigned char *>(value); *p; ++p) {
            if (used + 2 >= capacity) return false;
            used += unsigned(std::snprintf(line + used, capacity - used, "%02x", *p));
        }
        return true;
    }
    void fail(char *line, unsigned capacity) { cancel(); std::snprintf(line, capacity, "CNTRL ERROR storage-list"); }
};
} // namespace cntrl
