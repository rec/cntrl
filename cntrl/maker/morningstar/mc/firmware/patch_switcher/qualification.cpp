#include "qualification.h"
#if defined(CNTRL_MEMORY_QUALIFICATION) && CNTRL_MEMORY_QUALIFICATION
#include <Arduino.h>
#include <CrashReport.h>
#include <cstdio>

namespace cntrl::device {
namespace {
arm_fault_info_struct saved_fault{};
crashreport_breadcrumbs_struct saved_breadcrumbs{};
uint32_t saved_reset = 0, progress = 0, phase = 0;
unsigned timeout_ms = 8000;
bool fault_valid = false, armed = false, watchdog = false, started = false;

const crashreport_breadcrumbs_struct *breadcrumbs() {
    return reinterpret_cast<const crashreport_breadcrumbs_struct *>(0x2027ffc0);
}
bool valid_breadcrumbs(const crashreport_breadcrumbs_struct &value) {
    return value.checksum == CrashReportClass::checksum(&value, 28);
}
uint32_t disable_interrupts() {
    uint32_t previous;
    __asm__ volatile("mrs %0, primask\n\tcpsid i" : "=r"(previous) :: "memory");
    return previous;
}
void restore_interrupts(uint32_t previous) {
    __asm__ volatile("msr primask, %0" :: "r"(previous) : "memory");
}
void refresh_watchdog() {
    const uint32_t interrupts = disable_interrupts();
    WDOG1_WSR = 0x5555;
    WDOG1_WSR = 0xaaaa;
    restore_interrupts(interrupts);
}
void breadcrumb(unsigned slot, uint32_t value) {
    if (armed) CrashReport.breadcrumb(slot, value);
}
}

FLASHMEM void initialize_qualification() {
    saved_reset = SRC_SRSR;
    fault_valid = bool(CrashReport);
    if (fault_valid) saved_fault = *reinterpret_cast<const arm_fault_info_struct *>(0x2027ff80);
    const auto *previous = breadcrumbs();
    if (valid_breadcrumbs(*previous)) saved_breadcrumbs = *previous;
}

FLASHMEM bool qualification_start() {
    if (!watchdog) {
        // WDOG01: internal reset (WDT=0), no interrupt or external assertion.
        // WT=15 gives eight seconds. WDE cannot be cleared after enabling it.
        // See NXP mcux-sdk drivers/wdog01/fsl_wdog.c, WDOG_Init/WDOG_Refresh.
        CCM_CCGR3 |= CCM_CCGR3_WDOG1(CCM_CCGR_ON);
        if (WDOG1_WCR & WDOG_WCR_WDE) return false;
        const uint32_t interrupts = disable_interrupts();
        WDOG1_WICR = 0;
        WDOG1_WMCR = 0;
        WDOG1_WCR = WDOG_WCR_WT(15) | WDOG_WCR_SRS | WDOG_WCR_WDA | WDOG_WCR_WDE;
        restore_interrupts(interrupts);
        const uint16_t configured = WDOG1_WCR;
        watchdog = bool(configured & WDOG_WCR_WDE);
        timeout_ms = (unsigned(configured >> 8) + 1) * 500;
        if (!watchdog || timeout_ms != 8000) return false;
    }
    if (timeout_ms != 8000) return false;
    // The boot snapshot above remains readable. Remove stale fault data from
    // the retained SDK area so a later watchdog reset cannot inherit its PC.
    CrashReport.clear();
    started = armed = true; progress = 0;
    for (unsigned slot = 1; slot <= 6; ++slot) breadcrumb(slot, 0);
    qualification_phase(QualificationPhase::loop);
    refresh_watchdog();
    return true;
}

FLASHMEM void qualification_stop() {
    armed = false;
    // Keep feeding at normal loop completion: enabling WDE is irreversible.
    if (watchdog) refresh_watchdog();
}

FLASHMEM void qualification_phase(unsigned value) {
    phase = value; breadcrumb(1, value);
}
FLASHMEM void qualification_packet(unsigned route, uint32_t packet) {
    breadcrumb(2, (packet & ~uint32_t(0xf0)) | ((route & 15) << 4));
}
FLASHMEM unsigned qualification_heap(bool freeing, const void *pointer, unsigned size, uintptr_t caller) {
    const unsigned previous = phase;
    if (!armed || (freeing && !pointer)) return previous;
    breadcrumb(3, uint32_t(reinterpret_cast<uintptr_t>(pointer)));
    breadcrumb(4, size);
    breadcrumb(5, uint32_t(caller));
    qualification_phase(freeing ? QualificationPhase::heap_free : QualificationPhase::heap_allocate);
    return previous;
}
FLASHMEM void qualification_progress() {
    if (armed) { breadcrumb(6, ++progress); qualification_phase(QualificationPhase::loop); }
    if (watchdog) refresh_watchdog();
}
FLASHMEM void qualification_status(char *output, unsigned size) {
    const auto &record = started ? *breadcrumbs() : saved_breadcrumbs;
    const bool valid = valid_breadcrumbs(record);
    std::snprintf(output, size,
        "CNTRL QUAL STATUS enabled=1 armed=%u watchdog=%u timeout_ms=%u fault=%u pc=%lx cfsr=%lx hfsr=%lx mmfar=%lx bfar=%lx ipsr=%lx xpsr=%lx reset=%lx breadcrumbs=%lx b1=%lx b2=%lx b3=%lx b4=%lx b5=%lx b6=%lx",
        unsigned(armed), unsigned(watchdog), timeout_ms, unsigned(fault_valid),
        (unsigned long)saved_fault.ret, (unsigned long)saved_fault.cfsr, (unsigned long)saved_fault.hfsr,
        (unsigned long)saved_fault.mmfar, (unsigned long)saved_fault.bfar, (unsigned long)saved_fault.ipsr,
        (unsigned long)saved_fault.xpsr,
        (unsigned long)saved_reset, (unsigned long)(valid ? record.bitmask : 0),
        (unsigned long)(valid ? record.value[0] : 0), (unsigned long)(valid ? record.value[1] : 0),
        (unsigned long)(valid ? record.value[2] : 0), (unsigned long)(valid ? record.value[3] : 0),
        (unsigned long)(valid ? record.value[4] : 0), (unsigned long)(valid ? record.value[5] : 0));
}
}
#endif
