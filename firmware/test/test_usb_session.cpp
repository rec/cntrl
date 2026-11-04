#include <cntrl/usb_session.h>
#include <cassert>
#include <string>

int main() {
    cntrl::UsbSession usb;
    auto frame = [&](const char *text) {
        usb.receive(30);
        for (const char *p = text; *p; ++p) usb.receive(uint8_t(*p));
        usb.receive('\n');
    };
    frame("CNTRL USB abc 1 0 -");
    assert(usb.resetting()); usb.reset_done();
    assert(std::string(usb.output()) == "CNTRL USB abc 1 1 -\n");
    frame("CNTRL USB abc 2 0 410a");
    std::string command;
    while (usb.input_size()) { command += char(usb.input()); usb.consumed(); }
    assert(command == "A\n");
    usb.reply("CNTRL OK");
    frame("CNTRL USB abc 2 0 410a");
    assert(!usb.input_size() && !usb.reply_ready());
    assert(std::string(usb.output()) == "CNTRL USB abc 2 1 434e54524c204f4b0a\n");
    frame("CNTRL USB abc 3 1 -");
    assert(usb.reply_ready());
    // A replacement session must cancel unfinished input before accepting data.
    frame("CNTRL USB def 1 0 -");
    assert(usb.resetting()); usb.reset_done();
    frame("CNTRL USB abc 4 1 410a");
    assert(std::string(usb.output()) == "CNTRL USB LOST\n" && !usb.input_size());
    frame("CNTRL USB def 0 0 -");
    assert(usb.resetting()); usb.reset_done();
    assert(!usb.active() && usb.reply_ready());
    // A rebooted receiver must never execute an ambiguous old command.
    cntrl::UsbSession rebooted;
    for (char c : std::string("\x1e" "CNTRL USB abc 2 0 410a\n")) rebooted.receive(uint8_t(c));
    assert(std::string(rebooted.output()) == "CNTRL USB LOST\n" && !rebooted.input_size());
}
