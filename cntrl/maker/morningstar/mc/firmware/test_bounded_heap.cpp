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

    // Two exact-sized frames fit without rounding the pool up to another
    // 16 KiB size class. Released frame storage is available to MIDI buffers.
    alignas(8) std::array<uint8_t,245760 + 16> ram1{};
    alignas(8) std::array<uint8_t,4096> ram2{};
    cntrl::device::SharedHeap shared;
    assert(shared.initialize(ram2.data(), ram2.size(), ram1.data(), ram1.size()));
    assert(shared.ram1_unallocated() == ram1.size() && shared.ram2_unallocated() == ram2.size());
    auto *sdk = shared.allocate(ram2.size() - 8, 8, false, false);
    assert(shared.ram1_unallocated() == ram1.size() && shared.ram2_unallocated() == 0);
    auto *frame0 = static_cast<uint8_t *>(shared.allocate(122880, 8, true, true));
    auto *frame1 = shared.allocate(122880, 8, true, true);
    assert(sdk && frame0 && frame1);
    assert(shared.capacity() == ram1.size() + ram2.size());
    assert(shared.used() == shared.capacity());
    assert(shared.ram1_unallocated() == 0 && shared.ram2_unallocated() == 0);
    assert(shared.charge(frame0) == 122888 && shared.checked(frame0));
    assert(!shared.checked(sdk));
    const unsigned checked_budget = shared.capacity() - shared.charge(sdk);
    assert(checked_budget == ram1.size());
    assert(shared.charge(frame0) + shared.charge(frame1) == checked_budget);
    std::memset(frame0, 0x5a, 122880);
    assert(!shared.allocate(1, 8, true, false));
    shared.release(frame1);
    assert(shared.ram1_unallocated() == 122888 && shared.ram2_unallocated() == 0);
    assert(!shared.allocate(1, 8, false, false));
    assert(!shared.allocate(1, 8, false, true));
    auto *recording = shared.allocate(122880, 8, true, false);
    assert(recording && shared.checked(recording));
    // Re-enabling the second frame fails without evicting or modifying either
    // the recording or the currently displayed frame.
    std::memset(recording, 0x63, 122880);
    assert(!shared.allocate(122880, 8, true, true));
    assert(shared.used() == shared.capacity());
    for (unsigned i = 0; i < 122880; ++i) {
        assert(frame0[i] == 0x5a);
        assert(static_cast<uint8_t *>(recording)[i] == 0x63);
    }
    shared.release(frame0); shared.release(recording);
    // With zero frames, one large MIDI allocation can use the coalesced pool.
    recording = shared.allocate(245760, 8, true, false);
    assert(recording && shared.usable(recording) >= 245760);
    // An unsplittable eight-byte tail belongs to the allocated chunk. The
    // actual-charge callback must account for it rather than the nominal size.
    assert(shared.charge(recording) == checked_budget);
    assert(shared.charge(recording) == cntrl::memory_charge(245760) + 8);
    assert(!shared.allocate(1, 8, false, false));
    shared.release(recording); shared.release(sdk);
    assert(shared.used() == 0 && shared.free_blocks() == 2);
    assert(shared.ram1_unallocated() == ram1.size() && shared.ram2_unallocated() == ram2.size());
    assert(shared.inspect(largest) && largest == ram1.size() - 8);
}
