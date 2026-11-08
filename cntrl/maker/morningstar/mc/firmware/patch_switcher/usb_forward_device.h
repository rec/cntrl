#pragma once
namespace cntrl::device {
void detach_usb_device();
void attach_usb_device();
bool publish_hosted_midi(const char *name);
bool hosted_midi_visible();
}
