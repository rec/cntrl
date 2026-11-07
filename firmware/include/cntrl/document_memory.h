#pragma once
#include <cstdlib>
#include <cstdint>
#include <utility>

#ifndef CNTRL_DOCUMENT_MEMORY_BYTES
#define CNTRL_DOCUMENT_MEMORY_BYTES (192u * 1024u)
#endif

namespace cntrl {
constexpr unsigned document_memory_budget = CNTRL_DOCUMENT_MEMORY_BYTES;
static_assert(document_memory_budget >= 116 && document_memory_budget <= 0xffffffffu - 100);
inline unsigned document_memory_used = 0;

// All resident, staged and referenced document bytes share the firmware budget.
class DocumentBuffer {
public:
    uint8_t *data = nullptr;
    unsigned size = 0;
    DocumentBuffer() = default;
    DocumentBuffer(const DocumentBuffer &) = delete;
    DocumentBuffer &operator=(const DocumentBuffer &) = delete;
    ~DocumentBuffer() { clear(); }
    bool resize(unsigned length) {
        if (length == size) return true;
        if (!length) { clear(); return true; }
        if (length > document_memory_budget - document_memory_used + size) return false;
        void *memory = std::realloc(data, length);
        if (!memory) return false;
        document_memory_used = document_memory_used - size + length;
        data = static_cast<uint8_t *>(memory); size = length;
        return true;
    }
    void clear() {
        std::free(data); data = nullptr;
        document_memory_used -= size; size = 0;
    }
    void swap(DocumentBuffer &other) {
        std::swap(data, other.data); std::swap(size, other.size);
    }
};
}
