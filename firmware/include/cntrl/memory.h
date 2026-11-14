#pragma once
#include <cstdlib>
#include <cstdint>
#include <utility>
#include <cstddef>
#include <new>
#include <cstring>
#include <algorithm>
#include <atomic>

namespace cntrl {
// The target initializes this from its linker's remaining heap size before use.
inline unsigned memory_budget = 0;
inline unsigned memory_used = 0;
inline unsigned memory_peak = 0, memory_allocations = 0, memory_failures = 0;
// Tests may fail the next allocation with zero, or allow N calls first.
inline int memory_fail_after = -1;
// Compiler fences keep the tag visible to linker-wrapped malloc/free even
// when the compiler treats those calls as allocation builtins.
inline unsigned memory_checked_operations = 0;
inline unsigned (*memory_actual_charge)(const void *) = nullptr;
// The target's bounded RAM2 allocator aligns chunks to 8 bytes and uses an
// 8-byte header (minimum block: 16 bytes). Native tests use this target
// admission model, independently of their operating system's malloc overhead.
constexpr uint64_t memory_charge(unsigned length) {
    return length ? ((uint64_t(length) + 7) & ~uint64_t(7)) + 8 : 0;
}
// The allocation's payload can be uninitialized. A const parameter makes GCC
// infer a payload read before inlining this metadata-only query.
inline uint64_t memory_allocation_charge(void *data, unsigned size) {
    return data && memory_actual_charge ? memory_actual_charge(data) : memory_charge(size);
}

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
        const uint64_t charge = memory_charge(length);
        const unsigned available = memory_used <= memory_budget ? memory_budget - memory_used : 0;
        if (charge > available || memory_fail_after == 0) { ++memory_failures; return false; }
        if (memory_fail_after > 0) --memory_fail_after;
        ++memory_allocations;
        ++memory_checked_operations;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        void *memory = std::malloc(length);
        std::atomic_signal_fence(std::memory_order_seq_cst);
        --memory_checked_operations;
        if (!memory) { ++memory_failures; return false; }
        const uint64_t actual = memory_allocation_charge(memory, length);
        if (!actual || actual > available) {
            ++memory_checked_operations; std::atomic_signal_fence(std::memory_order_seq_cst);
            std::free(memory);
            std::atomic_signal_fence(std::memory_order_seq_cst); --memory_checked_operations;
            ++memory_failures; return false;
        }
        const uint64_t previous = memory_allocation_charge(data, size);
        if (size && !previous) {
            ++memory_checked_operations; std::atomic_signal_fence(std::memory_order_seq_cst);
            std::free(memory);
            std::atomic_signal_fence(std::memory_order_seq_cst); --memory_checked_operations;
            ++memory_failures; return false;
        }
        memory_used += unsigned(actual);
        if (memory_used > memory_peak) memory_peak = memory_used;
        if (size) std::memcpy(memory, data, std::min(size, length));
        ++memory_checked_operations; std::atomic_signal_fence(std::memory_order_seq_cst);
        std::free(data);
        std::atomic_signal_fence(std::memory_order_seq_cst); --memory_checked_operations;
        memory_used -= unsigned(previous);
        data = static_cast<uint8_t *>(memory); size = length;
        return true;
    }
    void clear() {
        const unsigned charge = unsigned(memory_allocation_charge(data, size));
        ++memory_checked_operations; std::atomic_signal_fence(std::memory_order_seq_cst);
        std::free(data);
        std::atomic_signal_fence(std::memory_order_seq_cst); --memory_checked_operations; data = nullptr;
        memory_used -= charge; size = 0;
    }
    void swap(MemoryBuffer &other) {
        std::swap(data, other.data); std::swap(size, other.size);
    }
};

// Owning SDK interfaces use ordinary unique_ptr with virtual destruction. This
// class keeps their allocation in the same ledger without changing deleters.
class CheckedHeapAllocation {
    struct alignas(std::max_align_t) Header { MemoryBuffer allocation; };
public:
    static void *operator new(std::size_t size, const std::nothrow_t &) noexcept {
        if (size > UINT32_MAX - sizeof(Header)) { ++memory_failures; return nullptr; }
        MemoryBuffer allocation;
        if (!allocation.resize(unsigned(size + sizeof(Header)))) return nullptr;
        auto *header = ::new (allocation.data) Header;
        header->allocation.swap(allocation);
        return header + 1;
    }
    static void operator delete(void *memory) noexcept {
        if (!memory) return;
        auto *header = static_cast<Header *>(memory) - 1;
        MemoryBuffer allocation(std::move(header->allocation));
        header->~Header();
    }
    static void operator delete(void *memory, const std::nothrow_t &) noexcept { operator delete(memory); }
};
}
