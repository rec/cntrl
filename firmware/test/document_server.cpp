#include "patch_hardware.h"
#include "usb_peer.h"
// A host-only protocol peer. No device access, used by the Python PTY test.
#include <cntrl/documents.h>
#include <vector>
#include "storage.h"

struct Peer : TestPatchHardware, cntrl::Replies, TestStorage {
    cntrl::UsbSession usb;
    bool reply_ready() const override { return usb.reply_ready(); }
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    bool supports(const char *name) const override {
        return !std::strcmp(name, "din") || !std::strcmp(name, "trs");
    }
    void show(unsigned, const cntrl::Pane &) override {}
    bool supports_font(unsigned font, unsigned size) const override { return font == 0 && size >= 8 && size <= 96 && size % 8 == 0; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    unsigned display_count() const override { return 3; }
    const char *control_label(cntrl::ControlRole role) const override {
        const char *labels[] = {"A", "B", "C", "D", "E", "F"}; return labels[unsigned(role)];
    }
    void set_brightness(uint8_t, bool) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
    void reply(const char *line) override { usb.reply(line); }
};

int main() {
    Peer peer;
    TestController app(peer);
    cntrl::Documents docs(peer, peer, app, peer);
    docs.boot();
    return usb_peer(peer.usb, [&](uint8_t b) { docs.receive(b, 0); },
        [&]() { docs.tick(0); }, [&]() { return docs.accepts_input(); }, [&]() { docs.cancel_transfer(); });
}
