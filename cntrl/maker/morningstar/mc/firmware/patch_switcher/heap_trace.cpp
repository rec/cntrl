#include <Arduino.h>
#include <malloc.h>
#include <errno.h>
#include <cntrl/memory.h>
#include "heap_trace.h"
#include "qualification.h"
#include "bounded_heap.h"

extern "C" {
extern char _heap_start, _heap_end;
}

namespace {
cntrl::device::BoundedHeap heap;
cntrl::device::HeapTrace trace;
unsigned depth = 0, boot_sdk = 0, boot_checked = 0;
unsigned active_context = 0;
unsigned phase = 0;
bool timing = false;
int fault_after = -1;
// Only bounded allocator metadata is protected. Large copies and calloc's
// clearing happen after this guard restores the previous interrupt mask.
struct InterruptLock {
    uint32_t previous;
    InterruptLock() { __asm__ volatile("mrs %0, primask" : "=r"(previous)); __disable_irq(); }
    ~InterruptLock() { if (!previous) __enable_irq(); }
};
struct HeapCall {
    unsigned previous_context;
    bool nested;
    explicit HeapCall(bool allocation) : previous_context(active_context), nested(depth++ != 0) {
        unsigned context; __asm__ volatile("mrs %0, ipsr" : "=r"(context));
        if (nested && context != previous_context) {
            // The allocator was interrupted by a different handler. Its nested
            // calls bypass outer chunk accounting, so invalidate the snapshot
            // even if an allocation/free pair leaves no net change in usage.
            trace.valid = false;
        }
        if (allocation && context && (!nested || context != previous_context)) ++trace.interrupt_allocations;
        active_context = context;
    }
    ~HeapCall() { active_context = previous_context; --depth; }
};
#if defined(CNTRL_MEMORY_QUALIFICATION) && CNTRL_MEMORY_QUALIFICATION
struct QualificationHeap {
    unsigned previous;
    QualificationHeap(bool freeing, const void *pointer, unsigned size, uintptr_t caller)
        : previous(cntrl::device::qualification_heap(freeing, pointer, size, caller)) {}
    ~QualificationHeap() { cntrl::device::qualification_phase(previous); }
};
#define CNTRL_HEAP_QUALIFY(freeing, pointer, size) QualificationHeap qualification(freeing, pointer, unsigned(std::min<size_t>(size, UINT32_MAX)), reinterpret_cast<uintptr_t>(__builtin_return_address(0)))
#else
#define CNTRL_HEAP_QUALIFY(freeing, pointer, size) ((void)0)
#endif
FLASHMEM unsigned chunk_bytes(const void *memory) {
    const unsigned size = heap.charge(memory);
    if (memory && !size) trace.valid = false;
    return size;
}
FLASHMEM bool begin(_reent *context, size_t request) {
    if (!heap.capacity() && !heap.initialize(&_heap_start,
        uintptr_t(&_heap_end) - uintptr_t(&_heap_start))) {
        ++trace.failures; context->_errno = ENOMEM; return false;
    }
    ++trace.allocations;
    trace.largest_request = std::max(trace.largest_request, unsigned(std::min<size_t>(request, UINT32_MAX)));
    if (fault_after == 0 && request) { ++trace.failures; context->_errno = ENOMEM; return false; }
    if (fault_after > 0 && request) --fault_after;
    return true;
}
FLASHMEM void complete(void *result, uint32_t started, size_t request, uintptr_t caller) {
    if (timing) trace.max_allocation_cycles = std::max<unsigned>(trace.max_allocation_cycles, uint32_t(ARM_DWT_CYCCNT - started));
    if (!result) { ++trace.failures; return; }
    const unsigned current = chunk_bytes(result);
    // Realloc growth retains the complete original until copying finishes.
    trace.peak = std::max(trace.peak, trace.live + current);
    trace.live += current;
    if (heap.checked(result)) trace.checked += current;
    else {
        const unsigned sdk = trace.live - trace.checked;
        if (sdk > trace.sdk_peak) {
            trace.sdk_peak = sdk; trace.sdk_request = unsigned(request);
            trace.sdk_caller = caller; trace.sdk_phase = phase;
        }
    }
}
}

extern "C" FLASHMEM void *__wrap__malloc_r(_reent *context, size_t size) {
    InterruptLock lock;
    HeapCall call(true);
    CNTRL_HEAP_QUALIFY(false, nullptr, size);
    const uint32_t started = timing ? ARM_DWT_CYCCNT : 0;
    const bool admitted = begin(context, size);
    void *result = admitted ? heap.allocate(size, 8, !active_context && cntrl::memory_checked_operations) : nullptr;
    if (!result) context->_errno = ENOMEM;
    if (admitted) complete(result, started, size, reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
    return result;
}
extern "C" FLASHMEM void *__wrap__calloc_r(_reent *context, size_t count, size_t size) {
    if (count && size > SIZE_MAX / count) {
        InterruptLock lock;
        ++trace.allocations; ++trace.failures; trace.largest_request = UINT32_MAX;
        context->_errno = ENOMEM; return nullptr;
    }
    const size_t request = count * size;
    void *result = __wrap__malloc_r(context, request);
    if (result) std::memset(result, 0, request);
    return result;
}
extern "C" void __wrap__free_r(_reent *context, void *memory);
extern "C" FLASHMEM void *__wrap__realloc_r(_reent *context, void *memory, size_t size) {
    { InterruptLock lock; ++trace.reallocations; }
    if (!size && memory) { __wrap__free_r(context, memory); return nullptr; }
    if (memory && size <= heap.usable(memory)) return memory;
    void *result;
    {
        InterruptLock lock;
        HeapCall call(true);
        CNTRL_HEAP_QUALIFY(false, memory, size);
        const uint32_t started = timing ? ARM_DWT_CYCCNT : 0;
        const bool admitted = begin(context, size);
        result = admitted ? heap.allocate(size, 8, memory ? heap.checked(memory)
            : !active_context && cntrl::memory_checked_operations) : nullptr;
        if (!result) context->_errno = ENOMEM;
        if (admitted) complete(result, started, size, reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
    }
    if (result && memory) {
        std::memcpy(result, memory, heap.usable(memory));
        __wrap__free_r(context, memory);
    }
    return result;
}
extern "C" FLASHMEM void *__wrap__memalign_r(_reent *context, size_t alignment, size_t size) {
    InterruptLock lock;
    HeapCall call(true);
    CNTRL_HEAP_QUALIFY(false, nullptr, size);
    const uint32_t started = timing ? ARM_DWT_CYCCNT : 0;
    const bool admitted = begin(context, size);
    void *result = admitted ? heap.allocate(size, alignment, !active_context && cntrl::memory_checked_operations) : nullptr;
    if (!result) context->_errno = alignment && !(alignment & (alignment - 1)) ? ENOMEM : EINVAL;
    if (admitted) complete(result, started, size, reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
    return result;
}
extern "C" FLASHMEM void __wrap__free_r(_reent *context, void *memory) {
    (void)context;
    InterruptLock lock;
    HeapCall call(false);
    CNTRL_HEAP_QUALIFY(true, memory, 0);
    const uint32_t started = timing ? ARM_DWT_CYCCNT : 0;
    const unsigned previous = chunk_bytes(memory);
    const bool checked = heap.checked(memory);
    heap.release(memory);
    trace.live -= previous;
    if (checked) trace.checked -= previous;
    trace.frees += bool(memory);
    if (timing) trace.max_free_cycles = std::max<unsigned>(trace.max_free_cycles, uint32_t(ARM_DWT_CYCCNT - started));
}
extern "C" FLASHMEM size_t __wrap__malloc_usable_size_r(_reent *, void *memory) {
    InterruptLock lock;
    return heap.usable(memory);
}

namespace cntrl::device {
FLASHMEM HeapSnapshot heap_snapshot(bool inspect_blocks) {
    InterruptLock lock;
    HeapSnapshot result; result.trace = trace; result.valid = trace.valid;
    result.actual_used = heap.used();
    result.checked_actual = trace.checked;
    result.sdk_used = trace.live >= trace.checked ? trace.live - trace.checked : 0;
    result.sdk_growth = result.sdk_used > boot_sdk ? result.sdk_used - boot_sdk : 0;
    result.boot_checked = boot_checked;
    if (trace.live != heap.used()
        || trace.checked > trace.live || trace.checked != memory_used) result.valid = false;
    result.unallocated = heap.capacity() - heap.used();
    result.allocatable = heap.largest_free(); result.free_blocks = heap.free_blocks();
    if (inspect_blocks && !heap.inspect(result.largest_free)) result.valid = false;
    return result;
}
FLASHMEM void initialize_heap_accounting(unsigned linked_capacity) {
    InterruptLock lock;
    memory_actual_charge = chunk_bytes;
    timing = true;
    boot_sdk = trace.live >= trace.checked ? trace.live - trace.checked : 0;
    boot_checked = trace.checked;
    trace.sdk_peak = boot_sdk; trace.sdk_caller = 0; trace.sdk_request = 0; trace.sdk_phase = 0;
    const auto snapshot = heap_snapshot();
    memory_budget = snapshot.valid && snapshot.sdk_used < linked_capacity ? linked_capacity - snapshot.sdk_used : 0;
}
FLASHMEM void heap_fault_after(int successful_calls) { fault_after = successful_calls; }
FLASHMEM void heap_phase(HeapPhase value) { InterruptLock lock; phase = unsigned(value); }
}
