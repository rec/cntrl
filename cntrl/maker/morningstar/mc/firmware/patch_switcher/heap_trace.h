#pragma once
#include <cstdint>

namespace cntrl::device {
struct HeapTrace {
    unsigned allocations = 0, reallocations = 0, frees = 0, failures = 0;
    unsigned live = 0, checked = 0, peak = 0, largest_request = 0;
    unsigned max_allocation_cycles = 0, max_free_cycles = 0;
    // Most recent new SDK high-water allocation since baseline capture. The
    // wrapped entry's return PC may name a forwarding calloc/libc helper;
    // resolve it against the matching exported ELF, together with its phase.
    unsigned sdk_peak = 0, sdk_request = 0, sdk_phase = 0;
    uintptr_t sdk_caller = 0;
    // Counts outer allocation calls per observed interrupt context. When a
    // handler interrupts another allocator call, valid becomes false and this
    // is only a conservative minimum, not an exact nested-allocation count.
    unsigned interrupt_allocations = 0;
    bool valid = true;
};
struct HeapSnapshot {
    HeapTrace trace;
    unsigned actual_used = 0, checked_actual = 0, sdk_used = 0, sdk_growth = 0, boot_checked = 0;
    unsigned largest_free = 0, allocatable = 0, free_blocks = 0, unallocated = 0;
    unsigned ram1_unallocated = 0, ram2_unallocated = 0;
    bool valid = false;
};
// Usage and capacity combine RAM2 with the reusable RAM1 display reservation.
// SDK allocations are confined to RAM2; only checked storage can use RAM1.
// Default snapshots read bounded counters/bitmaps. Inspecting the physical
// block chain supplies exact largest_free and validates accounting; request
// this only from the explicit idle diagnostic, never from performance service.
HeapSnapshot heap_snapshot(bool inspect_blocks = false);
void initialize_heap_accounting(unsigned linked_capacity);
enum class HeapPhase : unsigned { startup, setups, documents, songs, runtime };
void heap_phase(HeapPhase phase);
// Enables linked-allocator faults only when explicitly requested by a
// qualification harness. Negative disables faults; zero fails the next call.
void heap_fault_after(int successful_calls);
}
