#include <Arduino.h>
#include <malloc.h>
#include <errno.h>
#include <cntrl/memory.h>
#include "heap_trace.h"

extern "C" {
extern char _heap_start, _heap_end;
extern char *__brkval;
void *__real__malloc_r(_reent *, size_t);
void *__real__calloc_r(_reent *, size_t, size_t);
void *__real__realloc_r(_reent *, void *, size_t);
void *__real__memalign_r(_reent *, size_t, size_t);
void __real__free_r(_reent *, void *);
}

// Teensy 1.62.0 with teensy-compile 15.2.1, opt=osstd links newlib nano.
// Its chunk stores size at 0 and next at 4; pointers have a size or negative
// alignment displacement at ptr-4. This matches _malloc_usable_size_r.
struct Chunk { uint32_t size; Chunk *next; };
static_assert(sizeof(void *) == 4 && sizeof(Chunk) == 8);
extern "C" Chunk *__malloc_free_list;
namespace {
cntrl::device::HeapTrace trace;
unsigned depth = 0, boot_sdk = 0;
unsigned active_context = 0;
bool timing = false;
int fault_after = -1;
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
FLASHMEM unsigned chunk_bytes(const void *memory) {
    if (!memory) return 0;
    const auto address = reinterpret_cast<uintptr_t>(memory);
    const auto first = reinterpret_cast<uintptr_t>(&_heap_start), end = reinterpret_cast<uintptr_t>(__brkval);
    if (address < first + 4 || address >= end || (address & 7)) { trace.valid = false; return 0; }
    const int32_t marker = reinterpret_cast<const int32_t *>(memory)[-1];
    const auto header = marker < 0 ? address - uint32_t(-int64_t(marker)) - 4 : address - 4;
    if (header < first || header + 4 > end || (header & 3)) { trace.valid = false; return 0; }
    const unsigned size = *reinterpret_cast<const uint32_t *>(header);
    if (size < 12 || (size & 3) || size > end - header) { trace.valid = false; return 0; }
    return size;
}
FLASHMEM bool begin(_reent *context, size_t request, bool reallocating) {
    ++trace.allocations; trace.reallocations += reallocating;
    trace.largest_request = std::max(trace.largest_request, unsigned(std::min<size_t>(request, UINT32_MAX)));
    if (fault_after == 0 && request) { ++trace.failures; context->_errno = ENOMEM; return false; }
    if (fault_after > 0 && request) --fault_after;
    return true;
}
FLASHMEM void complete(void *result, unsigned previous, bool released, uint32_t started) {
    if (timing) trace.max_allocation_cycles = std::max<unsigned>(trace.max_allocation_cycles, uint32_t(ARM_DWT_CYCCNT - started));
    if (!result && !released) { ++trace.failures; return; }
    const unsigned current = chunk_bytes(result);
    // Conservative peak for realloc: it may allocate the complete replacement
    // before freeing the original even when a particular call grows in place.
    trace.peak = std::max(trace.peak, trace.live + current);
    trace.live = trace.live - previous + current;
    if (cntrl::memory_checked_operations) trace.checked = trace.checked - previous + current;
}
}

extern "C" FLASHMEM void *__wrap__malloc_r(_reent *context, size_t size) {
    HeapCall call(true);
    if (call.nested) return __real__malloc_r(context, size);
    const uint32_t started = timing ? ARM_DWT_CYCCNT : 0;
    const bool admitted = begin(context, size, false);
    void *result = admitted ? __real__malloc_r(context, size) : nullptr;
    if (admitted) complete(result, 0, false, started);
    return result;
}
extern "C" FLASHMEM void *__wrap__calloc_r(_reent *context, size_t count, size_t size) {
    HeapCall call(true);
    if (call.nested) return __real__calloc_r(context, count, size);
    const uint32_t started = timing ? ARM_DWT_CYCCNT : 0;
    const size_t request = count && size > SIZE_MAX / count ? SIZE_MAX : count * size;
    const bool admitted = begin(context, request, false);
    void *result = admitted ? __real__calloc_r(context, count, size) : nullptr;
    if (admitted) complete(result, 0, false, started);
    return result;
}
extern "C" FLASHMEM void *__wrap__realloc_r(_reent *context, void *memory, size_t size) {
    HeapCall call(true);
    if (call.nested) return __real__realloc_r(context, memory, size);
    const uint32_t started = timing ? ARM_DWT_CYCCNT : 0;
    const unsigned previous = chunk_bytes(memory);
    const bool admitted = begin(context, size, true);
    void *result = admitted ? __real__realloc_r(context, memory, size) : nullptr;
    if (admitted) complete(result, previous, !size, started);
    return result;
}
extern "C" FLASHMEM void *__wrap__memalign_r(_reent *context, size_t alignment, size_t size) {
    HeapCall call(true);
    if (call.nested) return __real__memalign_r(context, alignment, size);
    const uint32_t started = timing ? ARM_DWT_CYCCNT : 0;
    const bool admitted = begin(context, size, false);
    void *result = admitted ? __real__memalign_r(context, alignment, size) : nullptr;
    if (admitted) complete(result, 0, false, started);
    return result;
}
extern "C" FLASHMEM void __wrap__free_r(_reent *context, void *memory) {
    HeapCall call(false);
    if (call.nested) { __real__free_r(context, memory); return; }
    const uint32_t started = timing ? ARM_DWT_CYCCNT : 0;
    const unsigned previous = chunk_bytes(memory);
    __real__free_r(context, memory);
    trace.live -= previous;
    if (cntrl::memory_checked_operations) trace.checked -= previous;
    trace.frees += bool(memory);
    if (timing) trace.max_free_cycles = std::max<unsigned>(trace.max_free_cycles, uint32_t(ARM_DWT_CYCCNT - started));
}

namespace cntrl::device {
FLASHMEM HeapSnapshot heap_snapshot() {
    HeapSnapshot result; result.trace = trace; result.valid = trace.valid;
    const auto info = mallinfo(); result.actual_used = info.uordblks;
    result.checked_actual = trace.checked;
    result.sdk_used = trace.live >= trace.checked ? trace.live - trace.checked : 0;
    result.sdk_growth = result.sdk_used > boot_sdk ? result.sdk_used - boot_sdk : 0;
    const auto first = reinterpret_cast<uintptr_t>(&_heap_start), end = reinterpret_cast<uintptr_t>(__brkval);
    const auto limit = reinterpret_cast<uintptr_t>(&_heap_end);
    if (end < first || end > limit || trace.live != info.uordblks) result.valid = false;
    result.unallocated = end <= limit ? unsigned(limit - end) : 0;
    unsigned traversed = 0;
    for (auto *chunk = __malloc_free_list; chunk; chunk = chunk->next) {
        const auto address = reinterpret_cast<uintptr_t>(chunk);
        if (address < first || address + sizeof(Chunk) > end || (address & 3)
            || chunk->size < 12 || (chunk->size & 3) || chunk->size > end - address
            || ++traversed > (end - first) / 12) { result.valid = false; break; }
        result.largest_free = std::max<unsigned>(result.largest_free, (chunk->size - 8) & ~3u);
        ++result.free_blocks;
    }
    // A fresh nano allocation needs round_up(request,4)+8 bytes.
    if (result.unallocated >= 12) result.largest_free = std::max(result.largest_free, (result.unallocated - 8) & ~3u);
    return result;
}
FLASHMEM void initialize_heap_accounting(unsigned linked_capacity) {
    memory_actual_charge = chunk_bytes;
    timing = true;
    boot_sdk = trace.live >= trace.checked ? trace.live - trace.checked : 0;
    const auto snapshot = heap_snapshot();
    memory_budget = snapshot.valid && snapshot.sdk_used < linked_capacity ? linked_capacity - snapshot.sdk_used : 0;
}
FLASHMEM void heap_fault_after(int successful_calls) { fault_after = successful_calls; }
}
