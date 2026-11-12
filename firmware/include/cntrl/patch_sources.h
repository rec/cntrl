#pragma once
#include "patch_executor.h"
#include "setup_table.h"
#include "checked_list.h"

namespace cntrl {
// In-flight invocations share cached views. Release their pins after the last
// invocation ends, without discarding the LRU contents.
class StoredPatchSources : public PatchSources {
public:
    explicit StoredPatchSources(SetupTable &setups) : setups_(setups) {}
    ~StoredPatchSources() override { release_all(); }
    void begin() override { ++users_; }
    void end() override { if (users_ && !--users_) release_all(); }
    uint32_t names_signature() const override { return setups_.names_signature(); }
    bool acquire(uint16_t index, const Document *&document, uint32_t &revision) override {
        if (index >= setups_.count()) return false;
        document = setups_.get(index);
        if (!document) return false;
        if (std::find(pinned_.begin(), pinned_.end(), index) == pinned_.end()) {
            if (!pinned_.append(index)) return false;
            setups_.pin(index);
        }
        revision = setups_.revision(index); return true;
    }
    void release_all() {
        for (uint16_t index : pinned_) setups_.unpin(index);
        pinned_.release();
    }
private:
    SetupTable &setups_;
    CheckedList<uint16_t> pinned_;
    unsigned users_ = 0;
};
} // namespace cntrl
