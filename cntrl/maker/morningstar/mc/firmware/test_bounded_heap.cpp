#include "patch_switcher/bounded_heap.h"
#include <cntrl/memory.h>
#include <array>
#include <cassert>
#include <cstring>
#include <cstdint>

using cntrl::device::BoundedHeap;
int main() {
    alignas(256) std::array<uint8_t,65536> memory{};
    BoundedHeap heap;
    assert(heap.initialize(memory.data(), memory.size()));
    assert(!heap.initialize(memory.data(), memory.size()));
    for (unsigned bytes = 1; bytes <= 128; ++bytes) {
        void *block = heap.allocate(bytes);
        assert(block && heap.charge(block) == cntrl::memory_charge(bytes));
        heap.release(block);
    }
    for (unsigned alignment = 8; alignment <= 4096; alignment *= 2) {
        auto *block = static_cast<uint8_t *>(heap.allocate(53, alignment));
        assert(block && !(reinterpret_cast<uintptr_t>(block) & (alignment - 1)));
        assert(heap.usable(block) >= 53);
        std::memset(block, 0x63, 53);
        heap.release(block);
        assert(!heap.used() && heap.free_blocks() == 1);
    }
    assert(!heap.allocate(SIZE_MAX));
    assert(!heap.allocate(17, 3));
    auto *original = static_cast<uint8_t *>(heap.allocate(1024, 8, true));
    assert(heap.checked(original));
    std::memset(original, 0x5a, 1024);
    assert(!heap.resize(original, 65536));
    for (unsigned i = 0; i < 1024; ++i) assert(original[i] == 0x5a);
    auto *grown = static_cast<uint8_t *>(heap.resize(original, 2048));
    assert(grown && grown != original);
    assert(heap.checked(grown));
    for (unsigned i = 0; i < 1024; ++i) assert(grown[i] == 0x5a);
    assert(heap.resize(grown, 23) == grown);
    assert(!heap.resize(grown, 0) && !heap.used());
    std::array<void *,256> blocks{};
    std::array<unsigned,256> lengths{};
    uint32_t random = 27;
    for (unsigned i = 0; i < 20000; ++i) {
        random = random * 1664525 + 1013904223;
        const unsigned slot = (random >> 16) % blocks.size();
        if (blocks[slot]) {
            const auto *data = static_cast<const uint8_t *>(blocks[slot]);
            for (unsigned j = 0; j < lengths[slot]; ++j) assert(data[j] == uint8_t(slot));
            heap.release(blocks[slot]); blocks[slot] = nullptr;
        }
        else {
            lengths[slot] = 1 + (random & 1023);
            blocks[slot] = heap.allocate(lengths[slot]);
            if (blocks[slot]) std::memset(blocks[slot], uint8_t(slot), lengths[slot]);
        }
        assert(heap.used() <= heap.capacity());
        assert(heap.maximum_metadata_steps() <= 5);
        unsigned largest;
        assert(heap.inspect(largest) && largest >= heap.largest_free());
    }
    for (void *block : blocks) heap.release(block);
    assert(heap.used() == 0 && heap.free_blocks() == 1);
    unsigned largest;
    assert(heap.inspect(largest) && largest == memory.size() - 8);
    auto *all = heap.allocate(memory.size() - 8);
    assert(all && heap.charge(all) == memory.size() && !heap.allocate(1));
    heap.release(all);
    // Splits at both neighbours coalesce, including an aligned prefix.
    auto *a = heap.allocate(9), *b = heap.allocate(19, 64), *c = heap.allocate(29);
    heap.release(a); heap.release(c); heap.release(b);
    assert(heap.used() == 0 && heap.free_blocks() == 1);
}
