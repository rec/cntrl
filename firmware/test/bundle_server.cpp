#include "patch_hardware.h"
#include "storage.h"
#include "usb_peer.h"
#include <cntrl/bundle_transfer.h>

struct Peer : TestPatchHardware, cntrl::Replies {
    cntrl::UsbSession usb;
    bool reply_ready() const override { return usb.reply_ready(); }
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
    void reply(const char *text) override { usb.reply(text); }
};

int main() {
    Peer peer; TestStorage disk; cntrl::BundledStorage storage(disk, peer);
    assert(storage.boot()); TestController controller(peer);
    cntrl::Documents documents(storage, peer, controller, peer); documents.boot();
    cntrl::BundleTransfer transfer(storage, documents, peer, peer);
    char line[320] = {}; unsigned used = 0;
    return usb_peer(peer.usb, [&](uint8_t byte) {
        if (documents.busy()) { documents.receive(byte, 0); return; }
        if (byte == '\n') {
            line[used] = 0;
            if (!transfer.command(line, 0)) {
                for (unsigned i = 0; i < used; ++i) documents.receive(uint8_t(line[i]), 0);
                documents.receive('\n', 0);
            }
            used = 0;
        } else if (used + 1 < sizeof(line)) line[used++] = char(byte);
    }, [&]() { documents.tick(0); transfer.poll(0); },
       [&]() { return documents.accepts_input(); },
       [&]() { documents.cancel_transfer(); transfer.cancel(); });
}
