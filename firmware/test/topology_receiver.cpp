#include "input_hardware.h"
#include <cntrl/document.h>
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

constexpr cntrl::midi::OutputConnection endpoints[] = {
    {"computer", "Computer", 0, cntrl::midi::OutputTransport::usb_device, true},
    {"synth", "Synth", 1, cntrl::midi::OutputTransport::physical, true},
};
struct Hardware : TestInputHardware {
    cntrl::midi::Outputs midi_outputs() const override { return {endpoints, 2}; }
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    bool supports(const char *name) const override { return midi_outputs().find(name); }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    unsigned display_count() const override { return 3; }
    const char *display_id(unsigned i) const override {
        constexpr const char *names[] = {"left", "center", "right"}; return names[i];
    }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
};
int main(int argc, char **argv) {
    assert(argc == 2);
    std::ifstream stream(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(stream), {}};
    Hardware hardware;
    cntrl::Document document;
    assert(document.load(bytes.data(), unsigned(bytes.size()), hardware));
}
