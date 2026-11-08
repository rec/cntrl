#pragma once
#include "memory.h"

#ifndef CNTRL_USB_BLOCK_SIZE
#define CNTRL_USB_BLOCK_SIZE 4096
#endif

namespace cntrl {
inline constexpr unsigned usb_block_size = CNTRL_USB_BLOCK_SIZE;
inline constexpr unsigned usb_text_size = usb_block_size * 2 + 128;
inline constexpr unsigned usb_frame_size = usb_text_size * 2 + 64;

// Hex command replies need both the binary block and its encoded text. Keep
// these temporary bytes in the shared heap rather than the firmware stack.
struct UsbBlock {
    MemoryBuffer memory;
    uint8_t *bytes = nullptr;
    char *text = nullptr;
    UsbBlock() {
        if (memory.resize(usb_block_size + usb_text_size)) {
            bytes = memory.data;
            text = reinterpret_cast<char *>(bytes + usb_block_size);
        }
    }
};
} // namespace cntrl
