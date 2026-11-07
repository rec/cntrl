// Structural qualification of converted backups against an exported target.
// Rendering/assets and physical transports are qualified separately.
#include <cntrl/document.h>
#include "target_capabilities.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

struct ValidationHardware : cntrl::Hardware {
    bool send(const char *, const uint8_t *, unsigned) override { return false; }
    bool supports(const char *name) const override {
        auto *endpoint = cntrl::target::midi_connections.find(name);
        return endpoint && endpoint->output;
    }
    cntrl::midi::Outputs midi_outputs() const override { return cntrl::target::midi_connections; }
    unsigned input_count() const override { return cntrl::target::button_count; }
    const char *input_label(unsigned i) const override { return cntrl::target::button_labels[i]; }
    unsigned display_count() const override { return cntrl::target::display_count; }
    const char *display_id(unsigned i) const override { return cntrl::target::display_ids[i]; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    bool supports_font_asset(uint32_t, unsigned) const override { return true; }
    bool supports_function(unsigned value) const override { return value == 1; }
    bool supports_keyboard() const override { return true; }
    bool supports_trs_mode(unsigned port, cntrl::midi::TrsMode mode) const override {
        return port < cntrl::target::port_count && mode <= cntrl::midi::TrsMode::aux;
    }
    unsigned switch_capacity() const override { return cntrl::target::max_switches; }
    unsigned display_width() const override { return cntrl::target::viewports[0].width; }
    unsigned display_height() const override { return cntrl::target::viewports[0].height; }
    unsigned memory_capacity() const override { return cntrl::target::memory_capacity; }
    unsigned memory_runtime() const override { return cntrl::target::memory_runtime; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
};
int main(int argc, char **argv) {
    if (argc < 2) return 2;
    cntrl::memory_budget = cntrl::target::memory_capacity;
    ValidationHardware hardware;
    unsigned invalid = 0;
    for (int i = 1; i < argc; ++i) {
        std::ifstream input(argv[i], std::ios::binary);
        if (!input) return 2;
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        cntrl::Document document;
        if (!document.load(bytes.data(), unsigned(bytes.size()), hardware)) {
            std::cerr << argv[i] << ": invalid converted setup\n"; ++invalid;
        }
    }
    if (invalid) return 1;
    std::cout << argc - 1 << " converted setup/history images validated\n";
}
