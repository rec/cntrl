#pragma once
#include <SD.h>
#include <cstdio>
#include <memory>

// Pull one entry per acknowledged request, retaining only the directory stack.
class StorageListing {
public:
    bool begin() {
        current_.reset(new Level);
        index_ = 0;
        if (current_->directory.open("/", O_RDONLY)) return true;
        current_.reset(); return false;
    }
    void cancel() { current_.reset(); }
    void next(char *line, unsigned capacity) {
        while (current_) {
            FsFile entry;
            if (!entry.openNext(&current_->directory, O_RDONLY)) {
                if (current_->directory.getError()) { fail(line, capacity); return; }
                auto parent = std::move(current_->parent);
                current_ = std::move(parent); continue;
            }
            char name[256];
            if (!entry.getName(name, sizeof(name))) { fail(line, capacity); return; }
            unsigned id = ++index_;
            uint64_t size = entry.size();
            unsigned used = std::snprintf(line, capacity, "CNTRL ENTRY %u %u %c %08lx%08lx ",
                id, current_->id, entry.isDir() ? 'D' : 'F',
                (unsigned long)(size >> 32), (unsigned long)(size & 0xffffffff));
            for (const unsigned char *p = reinterpret_cast<const unsigned char *>(name); *p; ++p)
                used += std::snprintf(line + used, capacity - used, "%02x", *p);
            if (entry.isDir()) {
                std::unique_ptr<Level> child(new Level);
                if (!child->directory.open(&current_->directory, name, O_RDONLY)) { fail(line, capacity); return; }
                child->id = id; child->parent = std::move(current_); current_ = std::move(child);
            }
            return;
        }
        std::snprintf(line, capacity, "CNTRL STORAGE END %u", index_);
    }
private:
    struct Level {
        FsFile directory;
        unsigned id = 0;
        std::unique_ptr<Level> parent;
    };
    std::unique_ptr<Level> current_;
    unsigned index_ = 0;
    void fail(char *line, unsigned capacity) {
        cancel(); std::snprintf(line, capacity, "CNTRL ERROR storage-list");
    }
};
