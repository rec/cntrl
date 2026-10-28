#pragma once
#include "documents.h"
#include "patch_executor.h"
#include <cstdlib>
#include <new>

namespace cntrl {
// Every acquisition belongs to an invocation. Concurrent switch invocations
// share immutable copies, released only after the last invocation completes.
class StoredPatchSources : public PatchSources {
public:
    StoredPatchSources(Documents &documents, Hardware &hardware) : documents_(documents), hardware_(hardware) {}
    ~StoredPatchSources() override { release_all(); }
    void begin() override { ++users_; }
    void end() override { if (users_ && !--users_) release_all(); }
    bool acquire(const char *name, const Document *&document, uint32_t &revision) override {
        if (!name_compare(name, documents_.selected()) && documents_.document().valid()) {
            document = &documents_.document();
            revision = checksum(document->data(), document->length());
            return true;
        }
        for (Source *s = sources_; s; s = s->next) {
            if (name_compare(name, s->name)) continue;
            document = &s->document; revision = s->revision; return true;
        }
        Document source;
        if (!documents_.reference_document(name, source)) return false;
        Source *s = new (std::nothrow) Source;
        if (!s) return false;
        s->bytes = static_cast<uint8_t *>(std::malloc(source.length()));
        if (!s->bytes) { delete s; return false; }
        std::memcpy(s->bytes, source.data(), source.length());
        if (!s->document.load(s->bytes, source.length(), hardware_)) {
            std::free(s->bytes); delete s; return false;
        }
        std::snprintf(s->name, sizeof(s->name), "%s", name);
        s->revision = checksum(s->bytes, source.length());
        s->next = sources_; sources_ = s;
        document = &s->document; revision = s->revision; return true;
    }
    void release_all() {
        while (sources_) {
            Source *next = sources_->next;
            std::free(sources_->bytes); delete sources_; sources_ = next;
        }
    }
private:
    struct Source {
        Document document;
        char name[64] = {};
        uint8_t *bytes = nullptr;
        uint32_t revision = 0;
        Source *next = nullptr;
    };
    Documents &documents_;
    Hardware &hardware_;
    Source *sources_ = nullptr;
    unsigned users_ = 0;
};
}
