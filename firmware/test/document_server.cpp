#include "patch_hardware.h"
// A host-only protocol peer. No device access, used by the Python PTY test.
#include <cntrl/documents.h>
#include <unistd.h>
#include <poll.h>
#include <vector>
#include "storage.h"

struct Peer : TestPatchHardware, cntrl::Replies, TestStorage {
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    bool supports(const char *name) const override {
        return !std::strcmp(name, "din") || !std::strcmp(name, "trs");
    }
    void show(unsigned, const cntrl::Pane &) override {}
    bool supports_font(unsigned font, unsigned size) const override { return font == 0 && size >= 8 && size <= 96 && size % 8 == 0; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void set_brightness(uint8_t, bool) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
    void reply(const char *line) override {
        // Deliberately fragment replies to exercise host stream handling.
        for (const char *p = line; *p; ++p) if (::write(1, p, 1) != 1) return;
        ::write(1, "\n", 1);
    }
};

int main() {
    Peer peer;
    TestController app(peer);
    cntrl::Documents docs(peer, peer, app, peer);
    docs.boot();
    uint8_t buffer[37];
    pollfd input{0, POLLIN, 0};
    while (true) {
        int ready = ::poll(&input, 1, docs.busy() ? 0 : -1);
        if (ready < 0) return 1;
        if (ready) {
            ssize_t n = ::read(0, buffer, sizeof(buffer));
            if (n <= 0) break;
            for (ssize_t i = 0; i < n; ++i) docs.receive(buffer[i], 0);
        }
        docs.tick(0);
    }
}
