#pragma once
#include "document.h"
#include <cstdio>

namespace cntrl {
struct Revision {
    uint64_t number = 0;
    uint32_t modified = 0, length = 0;
    char label[64] = {};
};

class Revisions {
public:
    explicit Revisions(SetupStorage &storage) : storage_(storage) {}
    bool read(const char *name, uint64_t number, Revision &info, uint8_t *data) {
        storage_.choose_revision(name, number);
        uint8_t h[88], end[4];
        if (!storage_.read(0, 0, h, sizeof(h)) || std::memcmp(h, "CREV", 4)
            || u32(h + 84) != checksum(h, 84) || (h[20] ? !text_valid(h + 20, 64) : std::memcmp(h + 20, zeros_, 64) != 0)) return false;
        unsigned length = u32(h + 4);
        if (length < 116 || length > max_document || storage_.size(0) != length + 92
            || !storage_.read(0, 88 + length, end, 4) || std::memcmp(end, "DONE", 4)
            || !storage_.read(0, 88, data, length) || checksum(data, length) != u32(h + 8)) return false;
        info.number = number; info.modified = u32(h + 12); info.length = length;
        std::memcpy(info.label, h + 20, 64);
        return true;
    }
    bool next(const char *name, uint64_t before, Revision &info, uint8_t *data) {
        while (auto number = storage_.revision_before(name, before)) {
            if (read(name, number, info, data)) return true;
            before = number;
        }
        return false;
    }
    bool append(const char *name, const char *label, uint32_t modified, const uint8_t *data, unsigned length) {
        uint64_t last = storage_.revision_before(name, UINT64_MAX);
        if (last >= UINT64_MAX - 1 || std::strlen(label) > 63) return false;
        uint64_t number = last + 1;
        storage_.choose_revision(name, number);
        uint8_t h[88] = {}, verify[512];
        std::memcpy(h, "CREV", 4); put32(h + 4, length); put32(h + 8, checksum(data, length));
        put32(h + 12, modified); std::snprintf(reinterpret_cast<char *>(h + 20), 64, "%s", label);
        put32(h + 84, checksum(h, 84));
        if (!storage_.begin(0)) return false;
        bool good = storage_.write(h, sizeof(h));
        for (unsigned offset = 0; good && offset < length; offset += 512) {
            unsigned n = length - offset; if (n > 512) n = 512;
            good = storage_.write(data + offset, n);
        }
        good = good && storage_.write(reinterpret_cast<const uint8_t *>("DONE"), 4);
        good = storage_.finish() && good;
        if (!good || storage_.size(0) != length + 92) return false;
        for (unsigned offset = 0; offset < length + 92; offset += 512) {
            unsigned n = length + 92 - offset; if (n > 512) n = 512;
            if (!storage_.read(0, offset, verify, n)) return false;
            for (unsigned i = 0; i < n; ++i) {
                unsigned p = offset + i;
                uint8_t expected = p < 88 ? h[p] : p < 88 + length ? data[p - 88] : uint8_t("DONE"[p - 88 - length]);
                if (verify[i] != expected) return false;
            }
        }
        return true;
    }
private:
    SetupStorage &storage_;
    inline static const uint8_t zeros_[64] = {};
};
}
