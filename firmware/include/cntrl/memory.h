#pragma once
#include <cstdlib>
#include <cstdint>
#include <utility>

namespace cntrl {
// The target initializes this from its linker's remaining heap size before use.
inline unsigned memory_budget = 0;
inline unsigned memory_used = 0;

// Resident documents, temporary buffers and font assets share the linked heap.
class MemoryBuffer {
public:
    uint8_t *data = nullptr;
    unsigned size = 0;
    MemoryBuffer() = default;
    MemoryBuffer(const MemoryBuffer &) = delete;
    MemoryBuffer &operator=(const MemoryBuffer &) = delete;
    MemoryBuffer(MemoryBuffer &&other) noexcept { swap(other); }
    MemoryBuffer &operator=(MemoryBuffer &&other) noexcept {
        clear(); swap(other); return *this;
    }
    ~MemoryBuffer() { clear(); }
    bool resize(unsigned length) {
        if (length == size) return true;
        if (!length) { clear(); return true; }
        if (length > memory_budget - memory_used + size) return false;
        void *memory = std::realloc(data, length);
        if (!memory) return false;
        memory_used = memory_used - size + length;
        data = static_cast<uint8_t *>(memory); size = length;
        return true;
    }
    void clear() {
        std::free(data); data = nullptr;
        memory_used -= size; size = 0;
    }
    void swap(MemoryBuffer &other) {
        std::swap(data, other.data); std::swap(size, other.size);
    }
};
}
