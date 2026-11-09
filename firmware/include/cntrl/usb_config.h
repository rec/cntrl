#pragma once
#include "usb_frame.h"

#ifndef CNTRL_USB_BLOCK_SIZE
#define CNTRL_USB_BLOCK_SIZE 4096
#endif

namespace cntrl {
inline constexpr unsigned usb_block_size = CNTRL_USB_BLOCK_SIZE;
inline constexpr unsigned usb_text_size = 1024;
inline constexpr unsigned usb_payload_size = usb_block_size > usb_text_size ? usb_block_size : usb_text_size;
inline constexpr unsigned usb_frame_size = usb_header_size + usb_payload_size;
} // namespace cntrl
