#include <Arduino.h>
#include <usb_dev.h>
#include <usb_midi.h>
#include <cntrl/usb_midi_forward.h>
#include "usb_forward_device.h"

extern "C" {
extern const uint8_t usb_config_descriptor_480[], usb_config_descriptor_12[];
extern volatile uint8_t usb_high_speed;
}

namespace {
uint8_t high_speed[512], full_speed[512], product[128];
unsigned high_length = 0, full_length = 0;
bool visible = false;
}

namespace cntrl::device {
void flush_usb_midi() {
    // SOF calls the same SDK flush routine. Keep it out of this transfer update.
    // Mask this IRQ: SDK helpers can re-enable the global interrupt flag.
    const bool enabled = NVIC_IS_ENABLED(IRQ_USB1) != 0;
    NVIC_DISABLE_IRQ(IRQ_USB1);
    if (usb_configuration) usb_midi_flush_output();
    if (enabled) NVIC_ENABLE_IRQ(IRQ_USB1);
}
void detach_usb_device() {
    NVIC_DISABLE_IRQ(IRQ_USB1);
    USB1_USBCMD &= ~USB_USBCMD_RS;
    usb_configuration = 0;
    USB1_ENDPTFLUSH = 0xffffffff;
    NVIC_ENABLE_IRQ(IRQ_USB1);
}
void attach_usb_device() {
    USB1_USBCMD |= USB_USBCMD_RS;
    NVIC_ENABLE_IRQ(IRQ_USB1);
}
bool publish_hosted_midi(const char *name) {
    visible = false;
    if (!name) return true;
    const unsigned high_size = usb_config_descriptor_480[2] | unsigned(usb_config_descriptor_480[3]) << 8;
    const unsigned full_size = usb_config_descriptor_12[2] | unsigned(usb_config_descriptor_12[3]) << 8;
    high_length = midi::append_usb_midi_port(usb_config_descriptor_480, high_size, high_speed, sizeof(high_speed), 11);
    full_length = midi::append_usb_midi_port(usb_config_descriptor_12, full_size, full_speed, sizeof(full_speed), 11);
    if (!high_length || !full_length) return false;
    // USBHost_t36 retains the low byte of each UTF-16 code unit in product().
    unsigned count = 0;
    while (name[count] && count < 63) {
        product[2 + count * 2] = uint8_t(name[count]); product[3 + count * 2] = 0; ++count;
    }
    product[0] = uint8_t(2 + count * 2); product[1] = 3;
    visible = true;
    return true;
}
bool hosted_midi_visible() { return visible; }
}

extern "C" const uint8_t *cntrl_usb_descriptor(uint16_t value, uint16_t index, uint16_t *length) {
    if (!visible) return nullptr;
    if (value == 0x030b && index == 0x0409) { *length = product[0]; return product; }
    if (index || (value != 0x0200 && value != 0x0700)) return nullptr;
    const bool high = (usb_high_speed != 0) == (value == 0x0200);
    uint8_t *bytes = high ? high_speed : full_speed;
    bytes[1] = value >> 8;
    *length = uint16_t(high ? high_length : full_length);
    return bytes;
}
