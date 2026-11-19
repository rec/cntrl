#include "patch_hardware.h"
#include "midi_endpoints.h"
#include <cntrl/keyboard.h>
#include <cntrl/keyboard_input.h>
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
struct Hardware : TestPatchHardware {
    bool keyboard_supported = true;
    cntrl::KeyboardDelivery delivery = cntrl::KeyboardDelivery::pending;
    std::vector<bool> actions;
    cntrl::KeyboardDelivery keyboard_action(const cntrl::KeyboardKeys &keys, bool pressed) override {
        assert(keys.modifiers == 9 && keys.keys[0] == 44);
        if (delivery == cntrl::KeyboardDelivery::complete) actions.push_back(pressed);
        return delivery;
    }
    bool supports_keyboard() const override { return keyboard_supported; }
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
void test_keyboard_input_preserves_text_and_edit_keys() {
    cntrl::KeyboardInput input;
    uint8_t key;
    assert(!input.read(key));
    assert(input.press('a', 0));
    assert(input.press('A', 2));
    assert(input.press('!', 32));
    assert(input.press(127, 0));
    assert(input.press('\n', 0));
    assert(input.press(27, 0));
    for (unsigned modifier : {1u, 4u, 8u, 16u, 64u, 128u}) assert(!input.press('x', modifier));
    for (unsigned unsupported : {0u, 9u, 0xd8u, 0x100u}) assert(!input.press(unsupported, 0));
    for (uint8_t expected : {'a', 'A', '!', '\b', '\n', '\x1b'}) {
        assert(input.read(key) && key == expected);
    }
    assert(!input.read(key));
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        for (unsigned i = 0; i < cntrl::KeyboardInput::capacity - 1; ++i) assert(input.press(' ' + i, 0));
        assert(!input.press('\n', 0));
        for (unsigned i = 0; i < cntrl::KeyboardInput::capacity - 1; ++i) assert(input.read(key) && key == ' ' + i);
        assert(!input.read(key));
    }
}
int main(int argc, char **argv) {
    test_keyboard_input_preserves_text_and_edit_keys();
    cntrl::memory_budget = 1024 * 1024;
    if (argc == 2) {
        std::ifstream f(argv[1], std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), {});
        Hardware hw; cntrl::Document document;
        assert(document.load(data.data(), unsigned(data.size()), hw));
        cntrl::PatchExecutor executor(hw, hw, hw, 0);
        assert(executor.start(document, unsigned(document.named_patch("shortcut"))));
        executor.tick(); assert(executor.busy() && hw.actions.empty());
        hw.delivery = cntrl::KeyboardDelivery::complete;
        for (unsigned i = 0; i < 4 && executor.busy(); ++i) executor.tick();
        assert(executor.status() == cntrl::PatchExecutor::Status::completed);
        assert(hw.actions == std::vector<bool>({true, false}));
        hw.delivery = cntrl::KeyboardDelivery::failed;
        assert(executor.start(document, unsigned(document.named_patch("shortcut"))));
        executor.tick(); assert(executor.status() == cntrl::PatchExecutor::Status::failed);
        cntrl::Document rejected;
        hw.keyboard_supported = false;
        assert(!rejected.load(data.data(), unsigned(data.size()), hw));
        hw.keyboard_supported = true;
        for (uint8_t kind : {2, 7, 8, 9}) {
            data[document.page_layout().page(0) - data.data()] = kind;
            cntrl::put32(data.data() + 20, cntrl::checksum(data.data() + 28, unsigned(data.size()) - 28));
            assert(!rejected.load(data.data(), unsigned(data.size()), hw));
        }
    }
    Sink sink; cntrl::Keyboard keyboard;
    cntrl::KeyboardKeys first{}, second{};
    first.modifiers = 1; first.keys[0] = 44;
    second.modifiers = 2; second.keys[0] = 44; second.keys[1] = 21;
    using Delivery = cntrl::KeyboardDelivery;
    assert(keyboard.execute(first, true, sink) == Delivery::failed);
    keyboard.poll(true, sink);
    sink.blocked = true;
    assert(keyboard.execute(first, true, sink) == Delivery::pending);
    sink.blocked = false;
    keyboard.poll(true, sink);
    assert(sink.reports.back().keys[0] == 0); // An abandoned pending press never reaches USB.
    assert(keyboard.execute(first, true, sink) == Delivery::complete);
    assert(keyboard.execute(second, true, sink) == Delivery::complete);
    assert(keyboard.execute(first, true, sink) == Delivery::complete);
    keyboard.poll(true, sink);
    assert(sink.reports.back().modifiers == 3 && sink.reports.back().keys[0] == 44 && sink.reports.back().keys[1] == 21);
    sink.blocked = true;
    assert(keyboard.execute(first, false, sink) == Delivery::pending);
    sink.blocked = false;
    assert(keyboard.execute(first, false, sink) == Delivery::complete);
    keyboard.poll(true, sink);
    assert(sink.reports.back().modifiers == 2 && sink.reports.back().keys[0] == 44);
    sink.blocked = true; keyboard.clear(); keyboard.poll(true, sink);
    assert(sink.reports.back().keys[0] == 44);
    sink.blocked = false; keyboard.poll(true, sink);
    assert(sink.reports.back().keys[0] == 0);
    assert(keyboard.execute(first, true, sink) == Delivery::complete); keyboard.poll(true, sink);
    keyboard.poll(false, sink); keyboard.poll(true, sink);
    assert(sink.reports.back().keys[0] == 0 && sink.reports.back().modifiers == 0);
    cntrl::KeyboardKeys full{};
    for (unsigned i = 0; i < 6; ++i) full.keys[i] = uint8_t(4 + i);
    assert(keyboard.execute(full, true, sink) == Delivery::complete);
    assert(keyboard.execute(first, true, sink) == Delivery::failed);
    keyboard.poll(true, sink);
    assert(sink.reports.back().keys[5] == 9);
}
