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
    assert(std::string(usb.output()) == "CNTRL USB abc 1 1 4096 -\n");
    frame("CNTRL USB abc 2 0 410a");
    std::string command;
    while (usb.input_size()) { command += char(usb.input()); usb.consumed(); }
    assert(command == "A\n");
    usb.reply("CNTRL OK");
    frame("CNTRL USB abc 2 0 410a");
    assert(!usb.input_size() && !usb.reply_ready());
    assert(std::string(usb.output()) == "CNTRL USB abc 2 1 4096 434e54524c204f4b0a\n");
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
    // A full block is accepted once, and a full encoded reply survives retries.
    frame("CNTRL USB 123 1 0 -"); usb.reset_done();
    std::string block(cntrl::usb_block_size * 2, 'a');
    std::string full = "CNTRL USB 123 2 0 " + block;
    frame(full.c_str());
    unsigned received = 0;
    while (usb.input_size()) { assert(usb.input() == 0xaa); usb.consumed(); ++received; }
    assert(received == cntrl::usb_block_size);
    std::string reply(cntrl::usb_block_size * 2, 'b'); usb.reply(reply.c_str());
    frame(full.c_str());
    assert(!usb.input_size());
    assert(std::strlen(usb.output()) > reply.size() * 2);
    usb.reset(); assert(!usb.active() && !usb.output_pending());
    // A rebooted receiver must never execute an ambiguous old command.
    cntrl::UsbSession rebooted;
    for (char c : std::string("\x1e" "CNTRL USB abc 2 0 410a\n")) rebooted.receive(uint8_t(c));
    assert(std::string(rebooted.output()) == "CNTRL USB LOST\n" && !rebooted.input_size());
}
