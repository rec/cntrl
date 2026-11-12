#include <cntrl/text_messages.h>
#include <cntrl/usb_session.h>
#include <cntrl/ui/renderer.h>
#include <cassert>
#include <string>
#include <vector>

std::vector<uint8_t> message(const std::string &text, uint8_t kind = 1) {
    std::vector<uint8_t> bytes{0xf0, 0x7d, 0x43, 0x4e, 0x54, 0x52, 0x4c, 1, kind};
    bytes.insert(bytes.end(), text.begin(), text.end()); bytes.push_back(0xf7); return bytes;
}
void incoming(cntrl::TextMessages &messages, const std::vector<uint8_t> &bytes, uint32_t now = 10, unsigned input = 0, unsigned cable = 0) {
    for (auto byte : bytes) messages.byte(input, cable, byte, now);
}
void sysex_messages_are_complete_bounded_and_independent() {
    cntrl::TextMessages messages;
    auto first = message("First");
    for (unsigned i = 0; i + 1 < first.size(); ++i) {
        messages.byte(0, 0, first[i], 10); messages.byte(0, 0, 0xf8, 10);
    }
    assert(!messages.active(10));
    incoming(messages, message("Other cable", 2), 20, 0, 1);
    assert(messages.kind() == cntrl::TextMessages::Kind::warning);
    assert(!std::strcmp(messages.text(), "Other cable"));
    messages.byte(0, 0, 0xf7, 30);
    assert(!std::strcmp(messages.text(), "First"));
    assert(messages.active(5029) && !messages.active(5030));
    for (const auto &bad : {message(""), message(std::string(81, 'a')), message("bad", 3), message("bad\n")}) {
        incoming(messages, bad, 40); assert(!std::strcmp(messages.text(), "First"));
    }
    incoming(messages, {0xf0, 0x7d, 0x43, 0x90}, 40);
    messages.byte(0, 0, 0xf7, 40); assert(!std::strcmp(messages.text(), "First"));
    incoming(messages, message(std::string(80, 'x')), 50);
    assert(std::strlen(messages.text()) == 80);
    incoming(messages, {0xf0, 0x7d, 0x43}, 60, 1);
    messages.forget(1);
    incoming(messages, {0x4e, 0x54, 0x52, 0x4c, 1, 1, 'a', 0xf7}, 60, 1);
    assert(std::strlen(messages.text()) == 80);
    incoming(messages, message("Wrapped"), 0xfffffff0u);
    assert(messages.active(20) && !messages.active(5000));
    messages.poll(5000);
    assert(!messages.active(0xfffffff0u));
}
void serial(cntrl::UsbSession &usb, const std::string &text, uint32_t now) {
    for (unsigned char c : text) usb.receive(c, now);
}
void direct_serial_shares_the_display_and_respects_session_ownership() {
    cntrl::UsbSession usb; cntrl::TextMessages messages; usb.text_messages(messages);
    serial(usb, "CNTRL TEXT CUE Chorus", 100);
    assert(!messages.active(100));
    serial(usb, " next\r\n", 101);
    assert(!std::strcmp(messages.text(), "Chorus next"));
    assert(std::string(reinterpret_cast<const char *>(usb.output()), usb.output_size()) == "CNTRL OK\n");
    usb.sent(usb.output_size());
    incoming(messages, message("MIDI", 2), 200);
    assert(messages.kind() == cntrl::TextMessages::Kind::warning);
    serial(usb, "CNTRL TEXT WARNING Serial\n", 300);
    assert(!std::strcmp(messages.text(), "Serial"));
    assert(messages.active(5299) && !messages.active(5300));
    for (const auto &bad : {std::string("CNTRL TEXT CUE \n"), std::string("CNTRL TEXT OTHER bad\n"), "CNTRL TEXT CUE " + std::string(200, 'x') + "\n"}) {
        serial(usb, bad, 400); assert(!std::strcmp(messages.text(), "Serial"));
    }
    serial(usb, "CNTRL TEXT CUE stale", 400);
    usb.text_connection(false, 401); serial(usb, " tail\n", 402);
    assert(!std::strcmp(messages.text(), "Serial"));
    serial(usb, "CNTRL TEXT CUE timeout", 500);
    usb.text_connection(true, 10501); serial(usb, " tail\n", 10501);
    assert(!std::strcmp(messages.text(), "Serial"));
    serial(usb, "CNTRL TEXT CUE Recovered\n", 10502);
    assert(!std::strcmp(messages.text(), "Recovered"));
    uint8_t open[cntrl::usb_header_size] = {};
    cntrl::UsbFrame{cntrl::UsbKind::open, cntrl::UsbStatus::ok, 123, 1, 0, 0, 0, 0}.encode(open, nullptr);
    for (auto byte : open) usb.receive(byte, 10600);
    assert(usb.resetting()); usb.reset_done(); assert(usb.active());
    serial(usb, "CNTRL TEXT CUE Suppressed\n", 10601);
    assert(!std::strcmp(messages.text(), "Recovered"));
}
struct TextFonts : cntrl::ui::Fonts {
    mutable std::vector<std::string> lines;
    bool supports(unsigned, unsigned, uint32_t = 0) const override { return true; }
    cntrl::ui::Bounds bounds(unsigned, unsigned, uint32_t, const char *text) const override { return {0, 0, int(std::strlen(text) * 6), 8}; }
    bool glyph(unsigned, unsigned, uint32_t, const char *text, unsigned, cntrl::ui::Glyph &) const override {
        if (*text) lines.emplace_back(text);
        return false;
    }
};
struct TextDisplay : cntrl::ui::Display {
    unsigned count() const override { return 2; }
    cntrl::ui::Viewport viewport(unsigned) const override { return {64, 40, 0, 0}; }
    void poll() override {}
    bool ready() const override { return true; }
    bool failed() override { return false; }
    bool priority_due() const override { return false; }
    bool submit(unsigned, unsigned, unsigned, unsigned, unsigned, const uint8_t *) override { return true; }
};
void long_text_wraps_with_an_ellipsis_on_only_the_first_screen() {
    TextDisplay display; TextFonts fonts;
    uint16_t work[64 * 40] = {}, cache[2 * 64 * 40] = {}, previous[2 * 64 * 40] = {};
    cntrl::ui::Renderer renderer(display, fonts, work, 64 * 40, cache, previous, 2 * 64 * 40);
    cntrl::Pane page; page.graphics.pad = 0; page.graphics.background = 0x1234;
    renderer.show(0, page); renderer.show(1, page);
    cntrl::TextMessages messages;
    incoming(messages, message(std::string(80, 'x'), 2)); renderer.message(messages, 10);
    for (unsigned i = 0; i < 1000; ++i) renderer.draw();
    assert((fonts.lines == std::vector<std::string>{"Warning", "xxxxxxxxx", "xxxxxx..."}));
    assert(cache[0] == 0xffff && cache[64 * 40] == 0x1234);
    fonts.lines.clear();
    renderer.message(messages, 11);
    for (unsigned i = 0; i < 1000; ++i) renderer.draw();
    assert(fonts.lines.empty());
    renderer.message(messages, 5010);
    for (unsigned i = 0; i < 1000; ++i) renderer.draw();
    assert(cache[0] == 0x1234 && cache[64 * 40] == 0x1234);
}
int main() {
    cntrl::memory_budget = 1024 * 1024;
    sysex_messages_are_complete_bounded_and_independent();
    direct_serial_shares_the_display_and_respects_session_ownership();
    long_text_wraps_with_an_ellipsis_on_only_the_first_screen();
}
