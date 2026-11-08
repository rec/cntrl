#include "input_hardware.h"
#include <cntrl/ui/presentation.h>
#include <cntrl/module_menu.h>
#include <cassert>
#include <string>
#include <vector>

struct Hardware : TestInputHardware {
    unsigned windows;
    std::vector<cntrl::Pane> shown;
    explicit Hardware(unsigned count) : windows(count), shown(count) {}
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 200; }
    unsigned display_height() const override { return 160; }
    unsigned display_count() const override { return windows; }
    const char *control_label(cntrl::ControlRole role) const override {
        const char *labels[] = {"Left", "Right", "Select", "", "", ""}; return labels[unsigned(role)];
    }
    void show(unsigned index, const cntrl::Pane &pane) override { assert(index < windows); shown[index] = pane; }
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
};
int main() {
    cntrl::Pane panes[3];
    std::strcpy(panes[0].lines[0].text, "Instructions");
    std::strcpy(panes[1].lines[0].text, "Selection");
    std::strcpy(panes[1].lines[1].text, "A setup");
    std::strcpy(panes[2].lines[0].text, "Details");
    std::strcpy(panes[2].lines[15].text, "Back");
    for (unsigned count : {1u, 2u, 3u, 5u}) {
        Hardware hardware(count); cntrl::ui::present(hardware, panes, 1);
        std::vector<std::string> lines;
        for (const auto &pane : hardware.shown) for (const auto &line : pane.lines) if (line.text[0]) lines.emplace_back(line.text);
        assert(lines.size() == 5);
        for (const char *expected : {"Instructions", "Selection", "A setup", "Details", "Back"})
            assert(std::find(lines.begin(), lines.end(), expected) != lines.end());
        if (count < 3) assert(std::strcmp(hardware.shown[0].lines[0].text, "Selection") == 0);
        if (count == 3) for (unsigned i = 0; i < 3; ++i) assert(std::memcmp(&hardware.shown[i], &panes[i], sizeof(cntrl::Pane)) == 0);
        if (count == 5) assert(std::strcmp(hardware.shown[2].lines[0].text, "Selection") == 0);
    }
    Hardware single(1); const char *names[] = {"Setups"}; cntrl::ModuleMenu menu(single, names, 1);
    menu.enter(); std::vector<std::string> text;
    for (const auto &line : single.shown[0].lines) if (line.text[0]) text.emplace_back(line.text);
    assert(std::find(text.begin(), text.end(), "Left/Right browse") != text.end());
    assert(std::find(text.begin(), text.end(), "Select OK") != text.end());
    assert(std::find(text.begin(), text.end(), "Pages and tools") != text.end());

}
