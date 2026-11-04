#pragma once
#include <cntrl/usb_session.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>

// Run the production transport over a PTY, including fragmented output.
template<class Receive, class Poll, class Ready, class Reset>
int usb_peer(cntrl::UsbSession &usb, Receive receive, Poll poll, Ready ready, Reset reset) {
    pollfd input{0, POLLIN, 0};
    while (true) {
        if (::poll(&input, 1, usb.reply_ready() && !ready() ? 0 : 1) < 0) return 1;
        if (input.revents & POLLIN) {
            uint8_t byte;
            if (::read(0, &byte, 1) != 1) return 0;
            usb.receive(byte);
        }
        if (usb.resetting() && ready()) { reset(); usb.reset_done(); }
        while (usb.input_size() && usb.reply_ready() && ready()) {
            receive(usb.input()); usb.consumed();
        }
        if (usb.reply_ready()) poll();
        while (usb.output_pending()) {
            if (::write(1, usb.output(), 1) != 1) return 1;
            usb.sent(1);
        }
    }
}
