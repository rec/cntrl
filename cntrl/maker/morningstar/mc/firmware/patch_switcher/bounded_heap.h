#pragma once
#include <cstddef>
#include <cstdint>

namespace cntrl::device {
// One arena for SDK and application allocations. Bitmap lookup, splitting and
// coalescing touch a fixed number of records, independent of fragmentation.
// Copying/zeroing user data is deliberately outside that metadata bound.
class BoundedHeap {
public:
    bool initialize(void *memory, std::size_t bytes);
    void *allocate(std::size_t bytes, std::size_t alignment = 8, bool checked_allocation = false);
    void release(void *memory);
    void *resize(void *memory, std::size_t bytes);
    unsigned charge(const void *memory) const;
    unsigned usable(const void *memory) const;
    bool checked(const void *memory) const;
    unsigned capacity() const { return capacity_; }
    unsigned used() const { return used_; }
    unsigned peak() const { return peak_; }
    unsigned free_blocks() const { return free_blocks_; }
    unsigned largest_free() const;
    bool inspect(unsigned &largest) const;
    unsigned maximum_metadata_steps() const { return maximum_steps_; }
private:
    struct Header { uint32_t size_flags, previous; };
    struct Links { uint32_t next, previous; };
    static constexpr unsigned levels = 32, subdivisions = 8, minimum = 16;
    static constexpr uint32_t none = UINT32_MAX;
    uint8_t *base_ = nullptr;
    unsigned capacity_ = 0, used_ = 0, peak_ = 0, free_blocks_ = 0;
    unsigned steps_ = 0, maximum_steps_ = 0;
    uint32_t first_ = 0, second_[levels]{}, heads_[levels][subdivisions]{};
    static unsigned highest(uint32_t value);
    static unsigned lowest(uint32_t value);
    static void mapping(unsigned bytes, unsigned &first, unsigned &second);
    Header &header(unsigned offset) const;
    Links &links(unsigned offset) const;
    unsigned size(unsigned offset) const;
    void insert(unsigned offset);
    void remove(unsigned offset);
    unsigned find(unsigned bytes);
    void split(unsigned offset, unsigned bytes);
    void finish();
};
}
