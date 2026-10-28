#include "original_firmware.h"
#include <cassert>
#include <cstring>

unsigned starts = 0;
bool available = false;
bool start_stock() { ++starts; return available; }
struct Hardware : cntrl::Hardware {
    cntrl::Pane panes[3]; unsigned errors = 0;
    bool send(const char *, const uint8_t *, unsigned) override { assert(false); return false; }
    bool supports(const char *) const override { return false; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned i, const cntrl::Pane &pane) override { panes[i] = pane; }
    void set_brightness(uint8_t, bool) override { assert(false); }
    void set_splash(bool, uint32_t) override { assert(false); }
    void error(const char *) override { ++errors; }
};
int main() {
    Hardware hardware; OriginalFirmware module(hardware, start_stock);
    module.start(); assert(starts == 0);
    module.enter(); assert(module.open() && starts == 0);
    assert(!std::strcmp(hardware.panes[1].lines[0].text, "C start"));
    module.start(); assert(starts == 1 && hardware.errors == 1 && module.open());
    available = true; module.start(); assert(starts == 2 && hardware.errors == 1);
    module.leave(); module.start(); assert(!module.open() && starts == 2);
}
