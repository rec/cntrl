#include "bounded_heap.h"
#include <algorithm>
#include <cstring>
#include <new>

namespace cntrl::device {
unsigned BoundedHeap::highest(uint32_t value) {
    unsigned bit = 0;
    if (value >> 16) { value >>= 16; bit += 16; }
    if (value >> 8) { value >>= 8; bit += 8; }
    if (value >> 4) { value >>= 4; bit += 4; }
    if (value >> 2) { value >>= 2; bit += 2; }
    return bit + unsigned(value >> 1);
}
unsigned BoundedHeap::lowest(uint32_t value) { return highest(value & (0u - value)); }
void BoundedHeap::mapping(unsigned bytes, unsigned &first, unsigned &second) {
    first = highest(bytes);
    second = (bytes >> (first - 3)) - subdivisions;
}
BoundedHeap::Header &BoundedHeap::header(unsigned offset) const { return *reinterpret_cast<Header *>(base_ + offset); }
BoundedHeap::Links &BoundedHeap::links(unsigned offset) const { return *reinterpret_cast<Links *>(base_ + offset + sizeof(Header)); }
unsigned BoundedHeap::size(unsigned offset) const { return header(offset).size_flags & ~7u; }
void BoundedHeap::insert(unsigned offset) {
    ++steps_; ++free_blocks_;
    unsigned first, second; mapping(size(offset), first, second);
    const auto head = heads_[first][second];
    ::new (base_ + offset + sizeof(Header)) Links{head, none};
    if (head != none) links(head).previous = offset;
    heads_[first][second] = offset;
    first_ |= 1u << first; second_[first] |= 1u << second;
}
void BoundedHeap::remove(unsigned offset) {
    ++steps_; --free_blocks_;
    unsigned first, second; mapping(size(offset), first, second);
    const auto entry = links(offset);
    if (entry.previous != none) links(entry.previous).next = entry.next;
    else heads_[first][second] = entry.next;
    if (entry.next != none) links(entry.next).previous = entry.previous;
    if (heads_[first][second] == none) {
        second_[first] &= ~(1u << second);
        if (!second_[first]) first_ &= ~(1u << first);
    }
}
unsigned BoundedHeap::find(unsigned bytes) {
    ++steps_;
    // A freed frame-sized block can fit exactly even when its size lies below
    // the next class boundary. Inspect just this class's head, never a list.
    unsigned exact_first, exact_second; mapping(bytes, exact_first, exact_second);
    const unsigned exact = heads_[exact_first][exact_second];
    if (exact != none && size(exact) >= bytes) return exact;
    // Round the search to the next class boundary. Every member of the chosen
    // class then fits; no traversal of a size-class list is necessary.
    const unsigned shift = highest(bytes) - 3;
    const uint64_t rounded = uint64_t(bytes) + ((uint64_t(1) << shift) - 1);
    if (rounded > UINT32_MAX) return none;
    unsigned first, second; mapping(unsigned(rounded), first, second);
    uint32_t candidates = second_[first] & (~0u << second);
    if (!candidates) {
        const uint32_t higher = first == 31 ? 0 : first_ & (~0u << (first + 1));
        if (!higher) return none;
        first = lowest(higher); candidates = second_[first];
    }
    return heads_[first][lowest(candidates)];
}
void BoundedHeap::split(unsigned offset, unsigned bytes) {
    ++steps_;
    const unsigned old = size(offset), remaining = old - bytes;
    if (remaining < minimum) return;
    header(offset).size_flags = bytes;
    ::new (base_ + offset + bytes) Header{remaining, bytes};
    if (offset + old < capacity_) header(offset + old).previous = remaining;
    insert(offset + bytes);
}
void BoundedHeap::finish() { maximum_steps_ = std::max(maximum_steps_, steps_); }
bool BoundedHeap::initialize(void *memory, std::size_t bytes) {
    if (base_ || !memory || (reinterpret_cast<uintptr_t>(memory) & 7) || bytes > UINT32_MAX) return false;
    const unsigned length = unsigned(bytes) & ~7u;
    if (length < minimum) return false;
    base_ = static_cast<uint8_t *>(memory); capacity_ = length;
    for (auto &row : heads_) for (auto &head : row) head = none;
    ::new (base_) Header{length, 0}; insert(0); finish(); return true;
}
void *BoundedHeap::allocate(std::size_t bytes, std::size_t alignment, bool checked_allocation) {
    steps_ = 0;
    if (!base_ || !alignment || (alignment & (alignment - 1))) return nullptr;
    alignment = std::max<std::size_t>(alignment, 8);
    const uint64_t needed = std::max<uint64_t>(minimum, (uint64_t(bytes) + 15) & ~uint64_t(7));
    if (bytes > UINT32_MAX - 15 || alignment > UINT32_MAX) return nullptr;
    const uint64_t search = needed + (alignment > 8 ? alignment + minimum : 0);
    if (search > UINT32_MAX) return nullptr;
    unsigned offset = find(unsigned(search));
    if (offset == none) { finish(); return nullptr; }
    remove(offset);
    if (alignment > 8) {
        const uintptr_t address = reinterpret_cast<uintptr_t>(base_ + offset + sizeof(Header));
        uintptr_t aligned = (address + alignment - 1) & ~(uintptr_t(alignment) - 1);
        unsigned prefix = unsigned(aligned - address);
        if (prefix && prefix < minimum) { aligned += alignment; prefix = unsigned(aligned - address); }
        if (prefix) {
            const unsigned old = size(offset), previous = header(offset).previous;
            header(offset) = {prefix, previous};
            ::new (base_ + offset + prefix) Header{old - prefix, prefix};
            if (offset + old < capacity_) header(offset + old).previous = old - prefix;
            insert(offset); offset += prefix;
        }
    }
    split(offset, unsigned(needed));
    header(offset).size_flags |= 1u | (checked_allocation ? 2u : 0u);
    used_ += size(offset); peak_ = std::max(peak_, used_); finish();
    return base_ + offset + sizeof(Header);
}
void BoundedHeap::release(void *memory) {
    steps_ = 0;
    if (!memory) return;
    unsigned offset = unsigned(static_cast<uint8_t *>(memory) - base_ - sizeof(Header));
    unsigned length = size(offset); used_ -= length;
    const unsigned next = offset + length;
    if (next < capacity_ && !(header(next).size_flags & 1)) {
        remove(next); length += size(next);
    }
    const unsigned previous = header(offset).previous;
    if (previous && !(header(offset - previous).size_flags & 1)) {
        offset -= previous; remove(offset); length += previous;
    }
    header(offset).size_flags = length;
    if (offset + length < capacity_) header(offset + length).previous = length;
    insert(offset); finish();
}
void *BoundedHeap::resize(void *memory, std::size_t bytes) {
    if (!memory) return allocate(bytes);
    if (!bytes) { release(memory); return nullptr; }
    if (bytes <= usable(memory)) return memory;
    void *replacement = allocate(bytes, 8, checked(memory));
    if (!replacement) return nullptr;
    std::memcpy(replacement, memory, usable(memory));
    release(memory); return replacement;
}
unsigned BoundedHeap::charge(const void *memory) const {
    if (!memory || !base_) return 0;
    const auto address = reinterpret_cast<uintptr_t>(memory), base = reinterpret_cast<uintptr_t>(base_);
    if (address < base + sizeof(Header) || address >= base + capacity_ || (address & 7)) return 0;
    const unsigned offset = unsigned(address - base - sizeof(Header));
    const unsigned length = size(offset);
    return (header(offset).size_flags & 1) && length >= minimum && length <= capacity_ - offset ? length : 0;
}
unsigned BoundedHeap::usable(const void *memory) const { const unsigned length = charge(memory); return length ? length - sizeof(Header) : 0; }
bool BoundedHeap::checked(const void *memory) const {
    if (!charge(memory)) return false;
    const unsigned offset = unsigned(static_cast<const uint8_t *>(memory) - base_ - sizeof(Header));
    return header(offset).size_flags & 2;
}
bool BoundedHeap::contains(const void *memory) const {
    const auto address = reinterpret_cast<uintptr_t>(memory), base = reinterpret_cast<uintptr_t>(base_);
    return address >= base && address - base < capacity_;
}
unsigned BoundedHeap::largest_free() const {
    if (!first_) return 0;
    const unsigned first = highest(first_), second = highest(second_[first]);
    // A conservative request guaranteed by the largest nonempty class.
    return ((subdivisions + second) << (first - 3)) - sizeof(Header);
}
bool BoundedHeap::inspect(unsigned &largest) const {
    largest = 0;
    unsigned offset = 0, previous = 0, used = 0, free_blocks = 0;
    bool previous_free = false;
    while (offset < capacity_) {
        const unsigned length = size(offset), flags = header(offset).size_flags & 7;
        if (length < minimum || length > capacity_ - offset || header(offset).previous != previous
            || (flags != 0 && flags != 1 && flags != 3)) return false;
        if (flags & 1) used += length;
        else {
            if (previous_free) return false;
            largest = std::max(largest, length - unsigned(sizeof(Header)));
            ++free_blocks;
        }
        previous_free = !(flags & 1); previous = length; offset += length;
    }
    return used == used_ && free_blocks == free_blocks_;
}

bool SharedHeap::initialize(void *ram2, std::size_t ram2_bytes, void *ram1, std::size_t ram1_bytes) {
    return ram2_.initialize(ram2, ram2_bytes) && ram1_.initialize(ram1, ram1_bytes);
}
void *SharedHeap::allocate(std::size_t bytes, std::size_t alignment, bool checked_allocation, bool frame_allocation) {
    if (frame_allocation) return checked_allocation ? ram1_.allocate(bytes, alignment, true) : nullptr;
    if (void *memory = ram2_.allocate(bytes, alignment, checked_allocation)) return memory;
    return checked_allocation ? ram1_.allocate(bytes, alignment, true) : nullptr;
}
void SharedHeap::release(void *memory) {
    if (ram1_.contains(memory)) ram1_.release(memory);
    else ram2_.release(memory);
}
const BoundedHeap &SharedHeap::owner(const void *memory) const { return ram1_.contains(memory) ? ram1_ : ram2_; }
unsigned SharedHeap::charge(const void *memory) const { return owner(memory).charge(memory); }
unsigned SharedHeap::usable(const void *memory) const { return owner(memory).usable(memory); }
bool SharedHeap::checked(const void *memory) const { return owner(memory).checked(memory); }
unsigned SharedHeap::capacity() const { return ram2_.capacity() + ram1_.capacity(); }
unsigned SharedHeap::used() const { return ram2_.used() + ram1_.used(); }
unsigned SharedHeap::ram1_unallocated() const { return ram1_.capacity() - ram1_.used(); }
unsigned SharedHeap::ram2_unallocated() const { return ram2_.capacity() - ram2_.used(); }
unsigned SharedHeap::free_blocks() const { return ram2_.free_blocks() + ram1_.free_blocks(); }
unsigned SharedHeap::largest_free() const { return std::max(ram2_.largest_free(), ram1_.largest_free()); }
bool SharedHeap::inspect(unsigned &largest) const {
    unsigned first = 0, second = 0;
    const bool valid = ram2_.inspect(first) && ram1_.inspect(second);
    largest = std::max(first, second); return valid;
}
}
