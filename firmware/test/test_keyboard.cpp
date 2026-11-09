#include "input_hardware.h"
#include "midi_endpoints.h"
#include <cntrl/keyboard.h>
#include <cntrl/document.h>
#include <fstream>
#include <iterator>
#include <cassert>
#include <vector>
struct Sink : cntrl::KeyboardSink {
    bool blocked = false;
    std::vector<cntrl::KeyboardReport> reports;
    bool keyboard_report(const cntrl::KeyboardReport &r) override {
        if (blocked) return false;
        reports.push_back(r); return true;
    }
};
struct Hardware : TestInputHardware {
    bool supports_keyboard() const override { return true; }
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    cntrl::midi::Outputs midi_outputs() const override { return test_midi_outputs; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    unsigned display_count() const override { return 3; }
    const char *control_label(cntrl::ControlRole role) const override {
        const char *labels[] = {"A", "B", "C", "D", "E", "F"}; return labels[unsigned(role)];
    }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
};
int main(int argc, char **argv) {
    if (argc == 2) {
        std::ifstream f(argv[1], std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), {});
        Hardware hw; cntrl::Document document;
        assert(document.load(data.data(), unsigned(data.size()), hw));
        auto settings = document.page_settings();
        assert(settings.keyboard_count == 12 && settings.keyboard_shortcut(0).keys[0] == 44 && settings.keyboard_shortcut(0).modifiers == 9);
        assert(!std::strcmp(settings.keyboard_shortcut(11).name, "Play 11"));
    }
    Sink sink; cntrl::Keyboard keyboard;
    cntrl::KeyboardShortcut first{}, second{};
    first.modifiers = 1; first.keys[0] = 44;
    second.modifiers = 2; second.keys[0] = 44; second.keys[1] = 21;
    assert(!keyboard.press(0, first));
    keyboard.poll(true, sink);
    assert(keyboard.press(0, first) && keyboard.press(99, second));
    keyboard.poll(true, sink);
    assert(sink.reports.back().modifiers == 3 && sink.reports.back().keys[0] == 44 && sink.reports.back().keys[1] == 21);
    keyboard.release(0); keyboard.poll(true, sink);
    assert(sink.reports.back().modifiers == 2 && sink.reports.back().keys[0] == 44);
    sink.blocked = true; keyboard.clear(); keyboard.poll(true, sink);
    assert(sink.reports.back().keys[0] == 44);
    sink.blocked = false; keyboard.poll(true, sink);
    assert(sink.reports.back().keys[0] == 0);
    assert(keyboard.press(0, first)); keyboard.poll(true, sink);
    keyboard.poll(false, sink); keyboard.poll(true, sink);
    assert(sink.reports.back().keys[0] == 0 && sink.reports.back().modifiers == 0);
    cntrl::KeyboardShortcut full{};
    for (unsigned i = 0; i < 6; ++i) full.keys[i] = uint8_t(4 + i);
    assert(keyboard.press(0, full));
    assert(!keyboard.press(99, first));
    keyboard.poll(true, sink);
    assert(sink.reports.back().keys[5] == 9);
}
