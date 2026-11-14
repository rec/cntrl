#include <usb_dev.h>
#include <usb_midi.h>
#include "patch_switcher/usb_forward_device.h"
#include <cassert>
#include <cstdio>

volatile uint8_t usb_configuration = 1;
volatile uint32_t USB1_USBCMD = 0, USB1_ENDPTFLUSH = 0;
extern "C" {
extern const uint8_t usb_config_descriptor_480[] = {0, 0, 4, 0};
extern const uint8_t usb_config_descriptor_12[] = {0, 0, 4, 0};
volatile uint8_t usb_high_speed = 1;
}

namespace {
bool irq_enabled = true, pending = false, flushing = false;
unsigned flushes = 0, interrupts = 0, overlaps = 0;

void dispatch_interrupt() {
    if (!irq_enabled || !pending) return;
    pending = false;
    ++interrupts;
    overlaps += flushing;
}

void reset(bool enabled, bool connected) {
    irq_enabled = enabled; usb_configuration = connected;
    pending = flushing = false;
    flushes = interrupts = overlaps = 0;
}
}

uint32_t mock_nvic_is_enabled(unsigned irq) {
    assert(irq == IRQ_USB1);
    return irq_enabled ? (1u << IRQ_USB1) : 0;
}
void mock_nvic_disable(unsigned irq) {
    assert(irq == IRQ_USB1); irq_enabled = false;
}
void mock_nvic_enable(unsigned irq) {
    assert(irq == IRQ_USB1); irq_enabled = true;
    dispatch_interrupt();
}
extern "C" void usb_midi_flush_output(void) {
    ++flushes; flushing = true;
    // The SDK's SOF handler calls this same flush routine. Model an interrupt
    // arriving while the main-loop call is preparing its transfer descriptor.
    pending = true; dispatch_interrupt();
    flushing = false;
}

int main() {
    for (unsigned enabled = 0; enabled < 2; ++enabled) {
        reset(enabled != 0, true);
        cntrl::device::flush_usb_midi();
        if (overlaps) {
            std::fputs("USB SOF interrupted the main-loop MIDI flush\n", stderr);
            return 1;
        }
        assert(flushes == 1 && irq_enabled == bool(enabled));
        assert(interrupts == enabled);
        if (!enabled) {
            assert(pending);
            mock_nvic_enable(IRQ_USB1);
            assert(interrupts == 1 && !pending && !overlaps);
        }
        reset(enabled != 0, false);
        cntrl::device::flush_usb_midi();
        assert(!flushes && !interrupts && irq_enabled == bool(enabled));
    }
}
