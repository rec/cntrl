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
        Node *next = nullptr, *previous = nullptr;
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
            node->next = free_; free_ = node; ++capacity_;
            bytes_ += node_bytes(node);
        }
        return true;
    }
    [[nodiscard]] T *append(T value) {
        return insert(end(), std::move(value));
    }
    [[nodiscard]] T *insert(iterator position, T value) {
        if (!free_ && !borrowed_free_ && (size_ == UINT32_MAX || !reserve(size_ + 1))) return nullptr;
        auto *node = pop_free();
        node->value.emplace(std::move(value));
        link(position.node_, node); return &*node->value;
    }
    iterator erase(iterator position) {
        auto *node = position.node_; auto *next = node->next;
        unlink(node); node->value.reset();
        if (node->origin) return_node(node);
        else push_free(node);
        return iterator(next);
    }
    template<class Predicate> void erase_if(Predicate predicate) {
        for (auto i = begin(); i != end();) if (predicate(*i)) i = erase(i); else ++i;
    }
    void clear() { while (!empty()) erase(begin()); }
    void pop_back() { erase(iterator(tail_)); }
    // Move the actual live node, preserving its value, address and original
    // loan donor. No destination reservation or value construction is needed.
    iterator splice(iterator position, CheckedList &source, iterator item) {
        auto *node = item.node_;
        if (&source == this && (position.node_ == node || position.node_ == node->next)) return item;
        source.unlink(node);
        if (&source != this) adopt(source, node);
        link(position.node_, node); return iterator(node);
    }
    // Transfer unused node ownership without creating loans. Work is constant
    // per transferred node; callers choose the amount of admission/reclaim work.
    unsigned transfer_free(CheckedList &source, unsigned count) {
        if (&source == this) return 0;
        unsigned transferred = 0;
        while (transferred < count && (source.free_ || source.borrowed_free_)) {
            auto *node = source.pop_free(); adopt(source, node); push_free(node); ++transferred;
        }
        return transferred;
    }
    // A donor with outstanding loans must remain at its original address.
    bool take_reserved(CheckedList &source, unsigned count) {
        if (source.capacity_ - source.size_ < count) return false;
        if (&source == this) return true;
        while (count--) {
            auto *node = source.pop_free();
            if (!node->origin) { node->origin = &source; ++source.loans_; }
            adopt(source, node); push_free(node);
        }
        return true;
    }
    void return_reserved() {
        while (borrowed_free_) { auto *node = borrowed_free_; borrowed_free_ = node->next; return_node(node); }
    }
#ifdef CNTRL_NOINLINE
    CNTRL_NOINLINE
#endif
    unsigned trim(unsigned budget) {
        unsigned retired = 0;
        while ((free_ || borrowed_free_) && retired < budget) {
            auto *&head = free_ ? free_ : borrowed_free_;
            auto *node = head; head = node->next;
            if (node->origin) return_node(node);
            else {
                bytes_ -= node_bytes(node); --capacity_;
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
        clear(); return_reserved();
        while (free_) {
            auto *node = free_; free_ = node->next;
            MemoryBuffer allocation; allocation.swap(node->allocation); node->~Node();
        }
        size_ = capacity_ = bytes_ = 0;
    }
    void swap(CheckedList &other) noexcept {
        std::swap(borrowed_free_, other.borrowed_free_); std::swap(head_, other.head_);
        std::swap(tail_, other.tail_); std::swap(free_, other.free_);
        std::swap(size_, other.size_); std::swap(capacity_, other.capacity_);
        std::swap(loans_, other.loans_);
        std::swap(bytes_, other.bytes_);
    }
    iterator begin() { return iterator(head_); }
    iterator end() { return iterator(); }
    const_iterator begin() const { return const_iterator(head_); }
    const_iterator end() const { return const_iterator(); }
    unsigned size() const { return size_; }
    unsigned capacity() const { return capacity_; }
    unsigned memory_bytes() const { return bytes_; }
    unsigned loans() const { return loans_; }
    bool empty() const { return !size_; }
    T &front() { return *head_->value; }
    const T &front() const { return *head_->value; }
    T &back() { return *tail_->value; }
    const T &back() const { return *tail_->value; }
    T &operator[](unsigned index) { auto i = begin(); while (index--) ++i; return *i; }
    const T &operator[](unsigned index) const { auto i = begin(); while (index--) ++i; return *i; }
private:
    static unsigned node_bytes(const Node *node) {
        return unsigned(memory_allocation_charge(node->allocation.data, node->allocation.size));
    }
    Node *pop_free() {
        auto *&head = borrowed_free_ ? borrowed_free_ : free_;
        auto *node = head; head = node->next; return node;
    }
    void push_free(Node *node) {
        auto *&head = node->origin ? borrowed_free_ : free_;
        node->next = head; head = node;
    }
    void adopt(CheckedList &source, Node *node) {
        const unsigned bytes = node_bytes(node);
        --source.capacity_; source.bytes_ -= bytes; ++capacity_; bytes_ += bytes;
        if (node->origin == this) { node->origin = nullptr; --loans_; }
    }
    void unlink(Node *node) {
        if (node->previous) node->previous->next = node->next; else head_ = node->next;
        if (node->next) node->next->previous = node->previous; else tail_ = node->previous;
        --size_;
    }
    void link(Node *position, Node *node) {
        node->next = position; node->previous = position ? position->previous : tail_;
        if (node->previous) node->previous->next = node; else head_ = node;
        if (node->next) node->next->previous = node; else tail_ = node;
        ++size_;
    }
    void return_node(Node *node) {
        auto &origin = *node->origin; node->origin = nullptr; --origin.loans_;
        origin.adopt(*this, node); origin.push_free(node);
    }
    Node *borrowed_free_ = nullptr, *head_ = nullptr, *tail_ = nullptr, *free_ = nullptr;
    unsigned size_ = 0, capacity_ = 0, loans_ = 0, bytes_ = 0;
};
}
