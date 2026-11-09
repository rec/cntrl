#pragma once
#include <cntrl/usb_session.h>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <poll.h>
#endif

// Run the production transport over binary pipes, including fragmented output.
template<class Receive, class Poll, class Ready, class Reset>
int usb_peer(cntrl::UsbSession &usb, Receive receive, Poll poll, Ready ready, Reset reset) {
#ifndef _WIN32
    pollfd input{0, POLLIN, 0};
#endif
    while (true) {
#ifdef _WIN32
        unsigned long available = 0;
        if (!PeekNamedPipe(GetStdHandle(STD_INPUT_HANDLE), nullptr, 0, nullptr, &available, nullptr)) return 0;
        if (!available && !(usb.reply_ready() && !ready())) Sleep(1);
        if (available) {
            uint8_t byte;
            unsigned long count = 0;
            if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), &byte, 1, &count, nullptr) || count != 1) return 0;
            usb.receive(byte);
        }
#else
        if (::poll(&input, 1, usb.reply_ready() && !ready() ? 0 : 1) < 0) return 1;
        if (input.revents & POLLIN) {
            uint8_t byte;
            if (::read(0, &byte, 1) != 1) return 0;
            usb.receive(byte);
        }
        if (input.revents & POLLHUP) return 0;
#endif
        if (usb.resetting() && ready()) { reset(); usb.reset_done(); }
        while (usb.input_size() && usb.reply_ready() && ready()) {
            receive(usb.input()); usb.consumed();
        }
        if (usb.reply_ready()) poll();
        while (usb.output_pending()) {
#ifdef _WIN32
            unsigned long count = 0;
            if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), usb.output(), 1, &count, nullptr) || count != 1) return 1;
#else
            if (::write(1, usb.output(), 1) != 1) return 1;
#endif
            usb.sent(1);
        }
    }
}
