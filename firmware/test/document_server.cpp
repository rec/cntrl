// A host-only protocol peer. No device access, used by the Python PTY test.
#include <cntrl/documents.h>
#include <unistd.h>
#include <vector>

struct Peer : cntrl::Hardware, cntrl::Replies, cntrl::Storage {
    std::vector<uint8_t> slots[2];
    unsigned slot = 0;
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    bool supports(const char *name) const override {
        return !std::strcmp(name, "din") || !std::strcmp(name, "trs");
    }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t) override {}
    void error(const char *) override {}
    void reply(const char *line) override {
        // Deliberately fragment replies to exercise host stream handling.
        for (const char *p = line; *p; ++p) if (::write(1, p, 1) != 1) return;
        ::write(1, "\n", 1);
    }
    unsigned size(unsigned s) override { return unsigned(slots[s].size()); }
    bool read(unsigned s, unsigned offset, uint8_t *data, unsigned n) override {
        if (offset + n > slots[s].size()) return false;
        std::memcpy(data, slots[s].data() + offset, n); return true;
    }
    bool begin(unsigned s) override { slot = s; slots[s].clear(); return true; }
    bool write(const uint8_t *p, unsigned n) override {
        slots[slot].insert(slots[slot].end(), p, p + n); return true;
    }
    bool finish() override { return true; }
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
