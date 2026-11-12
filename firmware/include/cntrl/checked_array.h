#pragma once
#include "memory.h"
#include <limits>
#include <new>
#include <utility>

namespace cntrl {
// Contiguous configuration storage. Reserve the encoded count before filling.
template<class T> class CheckedArray {
public:
    CheckedArray() = default;
    CheckedArray(const CheckedArray &) = delete;
    CheckedArray &operator=(const CheckedArray &) = delete;
    CheckedArray(CheckedArray &&other) noexcept { swap(other); }
    CheckedArray &operator=(CheckedArray &&other) noexcept { release(); swap(other); return *this; }
    ~CheckedArray() { clear(); }
#ifdef CNTRL_NOINLINE
    CNTRL_NOINLINE
#endif
    bool reserve(unsigned count) {
        if (count <= capacity()) return true;
        if (count > std::numeric_limits<unsigned>::max() / sizeof(T)) return false;
        MemoryBuffer replacement;
        if (!replacement.resize(count * sizeof(T))) return false;
        auto *destination = reinterpret_cast<T *>(replacement.data);
        for (unsigned i = 0; i < size_; ++i) {
            new (destination + i) T(std::move(data()[i]));
            data()[i].~T();
        }
        storage_.swap(replacement); return true;
    }
    bool resize(unsigned count) {
        if (!reserve(count)) return false;
        while (size_ > count) data()[--size_].~T();
        while (size_ < count) { new (data() + size_) T{}; ++size_; }
        return true;
    }
    [[nodiscard]] T *append(T value) {
        if (size_ == capacity()) return nullptr;
        auto *item = new (data() + size_) T(std::move(value)); ++size_; return item;
    }
    void clear() { while (size_) data()[--size_].~T(); }
    void release() { clear(); storage_.clear(); }
    void swap(CheckedArray &other) noexcept { storage_.swap(other.storage_); std::swap(size_, other.size_); }
    T *data() { return reinterpret_cast<T *>(storage_.data); }
    const T *data() const { return reinterpret_cast<const T *>(storage_.data); }
    T *begin() { return data(); }
    const T *begin() const { return data(); }
    T *end() { return size_ ? data() + size_ : data(); }
    const T *end() const { return size_ ? data() + size_ : data(); }
    T &operator[](unsigned index) { return data()[index]; }
    const T &operator[](unsigned index) const { return data()[index]; }
    T &front() { return data()[0]; }
    const T &front() const { return data()[0]; }
    T &back() { return data()[size_ - 1]; }
    const T &back() const { return data()[size_ - 1]; }
    unsigned size() const { return size_; }
    unsigned capacity() const { return storage_.size / sizeof(T); }
    unsigned memory_bytes() const { return unsigned(memory_allocation_charge(storage_.data, storage_.size)); }
    bool empty() const { return !size_; }
private:
    MemoryBuffer storage_;
    unsigned size_ = 0;
};
template<class T> class CheckedObject {
public:
    bool create() { return storage_.resize(1); }
    void reset() { storage_.release(); }
    explicit operator bool() const { return !storage_.empty(); }
    T *get() { return storage_.data(); }
    const T *get() const { return storage_.data(); }
    T *operator->() { return get(); }
    const T *operator->() const { return get(); }
    T &operator*() { return *get(); }
    const T &operator*() const { return *get(); }
private:
    CheckedArray<T> storage_;
};
}
