#pragma once
#include "controller.h"
#include "memory.h"
#include <new>

namespace cntrl {
// Snapshots belong to live invocations. Adding one never moves another.
class NavigationSnapshots {
public:
    NavigationSnapshots() = default;
    NavigationSnapshots(const NavigationSnapshots &) = delete;
    NavigationSnapshots &operator=(const NavigationSnapshots &) = delete;
    ~NavigationSnapshots() {
        while (head_) {
            auto *entry = head_; head_ = entry->next; destroy(entry);
        }
    }
    const Controller::NavigationState *find(const PatchExecutor &invocation) const {
        for (auto *entry = head_; entry; entry = entry->next)
            if (entry->invocation == &invocation) return &entry->state;
        return nullptr;
    }
    bool put(const PatchExecutor &invocation, const Controller::NavigationState &state) {
        for (auto *entry = head_; entry; entry = entry->next)
            if (entry->invocation == &invocation) { entry->state = state; return true; }
        MemoryBuffer allocation;
        if (!allocation.resize(sizeof(Entry))) return false;
        auto *entry = new (allocation.data) Entry;
        entry->allocation.swap(allocation);
        entry->invocation = &invocation; entry->state = state; entry->next = head_; head_ = entry;
        return true;
    }
    void erase(const PatchExecutor &invocation) {
        for (auto **link = &head_; *link; link = &(*link)->next) {
            auto *entry = *link;
            if (entry->invocation != &invocation) continue;
            *link = entry->next;
            destroy(entry);
            return;
        }
    }
private:
    struct Entry {
        MemoryBuffer allocation;
        Entry *next = nullptr;
        const PatchExecutor *invocation = nullptr;
        Controller::NavigationState state;
    };
    Entry *head_ = nullptr;
    static void destroy(Entry *entry) {
        MemoryBuffer allocation;
        allocation.swap(entry->allocation);
        entry->~Entry();
    }
};
}
