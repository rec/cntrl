#pragma once
#include "memory.h"
#include <iterator>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace cntrl {
// Reserved nodes retain their addresses through growth and release. Admission
// can acquire nodes before output; erasure returns nodes to the same owner.
template<class T> class CheckedList {
    struct Node {
        MemoryBuffer allocation;
        Node *allocated = nullptr, *next = nullptr, *previous = nullptr;
        CheckedList *origin = nullptr;
        std::optional<T> value;
    };
public:
    template<bool Constant> class Iterator {
        friend class CheckedList;
        Node *node_ = nullptr;
        explicit Iterator(Node *node) : node_(node) {}
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using reference = std::conditional_t<Constant, const T &, T &>;
        using pointer = std::conditional_t<Constant, const T *, T *>;
        Iterator() = default;
        reference operator*() const { return *node_->value; }
        pointer operator->() const { return &*node_->value; }
        Iterator &operator++() { node_ = node_->next; return *this; }
        Iterator operator++(int) { auto old = *this; ++*this; return old; }
        bool operator==(Iterator other) const { return node_ == other.node_; }
        bool operator!=(Iterator other) const { return !(*this == other); }
    };
    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;
    static constexpr unsigned node_size() { return sizeof(Node); }
    CheckedList() = default;
    CheckedList(const CheckedList &) = delete;
    CheckedList &operator=(const CheckedList &) = delete;
    CheckedList(CheckedList &&other) noexcept { swap(other); }
    CheckedList &operator=(CheckedList &&other) noexcept { release(); swap(other); return *this; }
    ~CheckedList() { release(); }
#ifdef CNTRL_NOINLINE
    CNTRL_NOINLINE
#endif
    bool reserve(unsigned count) {
        const unsigned available = memory_used <= memory_budget ? memory_budget - memory_used : 0;
        if (count > capacity_ && uint64_t(count - capacity_) * memory_charge(sizeof(Node)) > available) {
            ++memory_failures; return false;
        }
        const unsigned previous = capacity_;
        while (capacity_ < count) {
            MemoryBuffer allocation;
            if (!allocation.resize(sizeof(Node))) {
                trim(capacity_ - previous); return false;
            }
            auto *node = new (allocation.data) Node;
            node->allocation.swap(allocation);
            node->allocated = allocated_; allocated_ = node;
            node->next = free_; free_ = node; ++capacity_;
        }
        return true;
    }
    [[nodiscard]] T *append(T value) {
        return insert(end(), std::move(value));
    }
    [[nodiscard]] T *insert(iterator position, T value) {
        if (!free_ && (size_ == UINT32_MAX || !reserve(size_ + 1))) return nullptr;
        auto *node = free_; free_ = node->next;
        node->next = position.node_; node->previous = position.node_ ? position.node_->previous : tail_;
        node->value.emplace(std::move(value));
        if (node->previous) node->previous->next = node; else head_ = node;
        if (node->next) node->next->previous = node; else tail_ = node;
        ++size_; return &*node->value;
    }
    iterator erase(iterator position) {
        auto *node = position.node_; auto *next = node->next;
        if (node->previous) node->previous->next = next; else head_ = next;
        if (next) next->previous = node->previous; else tail_ = node->previous;
        node->value.reset(); --size_;
        if (node->origin) return_node(node);
        else { node->next = free_; free_ = node; }
        return iterator(next);
    }
    template<class Predicate> void erase_if(Predicate predicate) {
        for (auto i = begin(); i != end();) if (predicate(*i)) i = erase(i); else ++i;
    }
    void clear() { while (!empty()) erase(begin()); }
    void pop_back() { erase(iterator(tail_)); }
    // A donor with outstanding loans must remain at its original address.
    bool take_reserved(CheckedList &source, unsigned count) {
        if (source.capacity_ - source.size_ < count) return false;
        while (count--) {
            auto *node = source.free_; source.free_ = node->next;
            auto **link = &source.allocated_;
            while (*link != node) link = &(*link)->allocated;
            *link = node->allocated;
            node->allocated = allocated_; allocated_ = node;
            node->origin = &source; ++source.loans_;
            node->next = free_; free_ = node;
            --source.capacity_; ++capacity_;
        }
        return true;
    }
    void return_reserved() {
        for (auto **link = &free_; *link;) {
            auto *node = *link;
            if (!node->origin) { link = &node->next; continue; }
            *link = node->next; return_node(node);
        }
    }
#ifdef CNTRL_NOINLINE
    CNTRL_NOINLINE
#endif
    unsigned trim(unsigned budget) {
        unsigned retired = 0;
        while (free_ && retired < budget) {
            auto *node = free_; free_ = node->next;
            if (node->origin) return_node(node);
            else {
                auto **link = &allocated_;
                while (*link != node) link = &(*link)->allocated;
                *link = node->allocated; --capacity_;
                MemoryBuffer allocation; allocation.swap(node->allocation); node->~Node();
            }
            ++retired;
        }
        return retired;
    }
#ifdef CNTRL_NOINLINE
    CNTRL_NOINLINE
#endif
    void release() {
        while (allocated_) {
            if (allocated_->origin) {
                auto *node = allocated_; node->value.reset(); return_node(node); continue;
            }
            auto *node = allocated_; allocated_ = node->allocated;
            MemoryBuffer allocation; allocation.swap(node->allocation); node->~Node();
        }
        head_ = tail_ = free_ = nullptr; size_ = capacity_ = 0;
    }
    void swap(CheckedList &other) noexcept {
        std::swap(allocated_, other.allocated_); std::swap(head_, other.head_);
        std::swap(tail_, other.tail_); std::swap(free_, other.free_);
        std::swap(size_, other.size_); std::swap(capacity_, other.capacity_);
        std::swap(loans_, other.loans_);
    }
    iterator begin() { return iterator(head_); }
    iterator end() { return iterator(); }
    const_iterator begin() const { return const_iterator(head_); }
    const_iterator end() const { return const_iterator(); }
    unsigned size() const { return size_; }
    unsigned capacity() const { return capacity_; }
    unsigned memory_bytes() const {
        unsigned bytes = 0;
        for (auto *node = allocated_; node; node = node->allocated)
            bytes += unsigned(memory_allocation_charge(node->allocation.data, node->allocation.size));
        return bytes;
    }
    unsigned loans() const { return loans_; }
    bool empty() const { return !size_; }
    T &front() { return *head_->value; }
    const T &front() const { return *head_->value; }
    T &back() { return *tail_->value; }
    const T &back() const { return *tail_->value; }
    T &operator[](unsigned index) { auto i = begin(); while (index--) ++i; return *i; }
    const T &operator[](unsigned index) const { auto i = begin(); while (index--) ++i; return *i; }
private:
    void return_node(Node *node) {
        auto **link = &allocated_;
        while (*link != node) link = &(*link)->allocated;
        *link = node->allocated; --capacity_;
        auto &origin = *node->origin; node->origin = nullptr; --origin.loans_;
        node->allocated = origin.allocated_; origin.allocated_ = node;
        node->next = origin.free_; origin.free_ = node; ++origin.capacity_;
    }
    Node *allocated_ = nullptr, *head_ = nullptr, *tail_ = nullptr, *free_ = nullptr;
    unsigned size_ = 0, capacity_ = 0, loans_ = 0;
};
}
