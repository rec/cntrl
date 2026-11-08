// Host-only receiver for the Python PTY tests. No device access.
#include <cntrl/animation/receiver.h>
#include "storage.h"
#include "../usb_peer.h"

struct Wire : cntrl::animation::Replies {
    cntrl::UsbSession usb;
    void reply(const char *line) override { usb.reply(line); }
};

int main() {
    cntrl::memory_budget = 192 * 1024;
    Memory memory;
    Wire wire;
    cntrl::animation::Receiver receiver(memory, wire);
    receiver.boot();
    while (receiver.busy()) receiver.poll(0);
    return usb_peer(wire.usb, [&](uint8_t b) { receiver.receive(b, 0); },
        [&]() { receiver.poll(0); }, [&]() { return receiver.accepts_input(); },
        [&]() { receiver.cancel_transfer(); });
}
