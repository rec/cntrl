#pragma once
#include <cstdint>

namespace cntrl::device {
// Locator values are kept with the exact diagnostic ELF for addr2line.
enum class QualificationPhase : unsigned {
    loop = 1, display, keyboard, scan, pages, midi, serial, expression, navigation,
    midi_inputs, midi_ingress, midi_derived, midi_bridge, midi_streams,
    midi_reclaim, midi_flush, midi_files, midi_write, heap_allocate, heap_free, watchdog_test
};
#if defined(CNTRL_MEMORY_QUALIFICATION) && CNTRL_MEMORY_QUALIFICATION
void initialize_qualification();
bool qualification_start();
void qualification_stop();
void qualification_status(char *output, unsigned size);
void qualification_phase(unsigned phase);
inline void qualification_phase(QualificationPhase phase) { qualification_phase(unsigned(phase)); }
void qualification_packet(unsigned route, uint32_t packet);
unsigned qualification_heap(bool freeing, const void *pointer, unsigned size, uintptr_t caller);
void qualification_progress();
bool qualification_request_hang();
void qualification_poll(bool output_pending);
#endif
}
