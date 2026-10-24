// A host-only protocol peer. No device access, used by the Python PTY test.
#include <cntrl/documents.h>
#include <unistd.h>
#include <vector>
#include "storage.h"

struct Peer : cntrl::Hardware, cntrl::Replies, TestStorage {
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    bool supports(const char *name) const override {
        return !std::strcmp(name, "din") || !std::strcmp(name, "trs");
    }
    void show(unsigned, const cntrl::Pane &) override {}
    bool supports_font(unsigned font, unsigned size) const override { return font == 0 && size >= 8 && size <= 96 && size % 8 == 0; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void set_brightness(uint8_t) override {}
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
    cntrl::Controller app(peer);
    cntrl::Documents docs(peer, peer, app, peer);
    docs.boot();
    uint8_t buffer[37];
    ssize_t n;
    while ((n = ::read(0, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < n; ++i) docs.receive(buffer[i], 0);
        // A maximum-sized document needs at most 128 commit chunks.
        for (unsigned i = 0; i < 129; ++i) docs.tick(0);
    }
}
