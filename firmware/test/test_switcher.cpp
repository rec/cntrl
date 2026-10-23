#include <cntrl/documents.h>
#include <cassert>
#include <fstream>
#include <string>
#include <vector>
#include <iostream>

struct Fake : cntrl::Hardware, cntrl::Replies {
    std::vector<std::vector<uint8_t>> sent;
    std::vector<std::string> routes, replies;
    cntrl::Pane panes[3];
    bool available = true;
    unsigned errors = 0;
    uint8_t brightness = 0;
    bool send(const char *route, const uint8_t *data, unsigned n) override {
        if (!available) return false;
        sent.emplace_back(data, data + n); routes.emplace_back(route); return true;
    }
    bool supports(const char *route) const override {
        return !std::strcmp(route, "din") || !std::strcmp(route, "trs");
    }
    bool supports_font(unsigned font, unsigned size) const override { return font == 0 && size >= 8 && size <= 96 && size % 8 == 0; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned p, const cntrl::Pane &view) override { panes[p] = view; }
    void set_brightness(uint8_t value) override { brightness = value; }
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override { ++errors; }
    void reply(const char *s) override { replies.emplace_back(s); }
};

struct Memory : cntrl::Storage {
    std::vector<uint8_t> slots[2];
    unsigned slot = 0;
    int remaining = -1;
    unsigned size(unsigned s) override { return unsigned(slots[s].size()); }
    bool read(unsigned s, unsigned offset, uint8_t *data, unsigned n) override {
        if (offset + n > slots[s].size()) return false;
        std::memcpy(data, slots[s].data() + offset, n); return true;
    }
    bool begin(unsigned s) override { slot = s; slots[s].clear(); return true; }
    bool write(const uint8_t *data, unsigned n) override {
        if (remaining >= 0) {
            unsigned amount = std::min(n, unsigned(remaining));
            slots[slot].insert(slots[slot].end(), data, data + amount);
            remaining -= int(amount);
            return amount == n;
        }
        slots[slot].insert(slots[slot].end(), data, data + n); return true;
    }
    bool finish() override { return true; }
};

void command(cntrl::Documents &docs, const std::string &line, uint32_t now = 0) {
    for (unsigned char c : line + "\n") docs.receive(c, now);
}
void upload(cntrl::Documents &docs, const std::vector<uint8_t> &data) {
    char line[96];
    std::snprintf(line, sizeof(line), "CNTRL BEGIN %u %08lx", unsigned(data.size()),
                  (unsigned long)cntrl::checksum(data.data(), unsigned(data.size())));
    command(docs, line);
    for (uint8_t b : data) docs.receive(b, 0);
    while (docs.busy()) docs.tick(1);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    std::ifstream file(argv[1]);
    std::string hex; file >> hex;
    assert(!hex.empty() && hex.size() % 2 == 0);
    std::vector<uint8_t> bytes;
    for (unsigned i = 0; i < hex.size(); i += 2)
        bytes.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
    Fake hw;
    cntrl::Document doc;
    assert(doc.load(bytes.data(), unsigned(bytes.size()), hw));
    for (unsigned n = 0; n < bytes.size(); ++n) {
        cntrl::Document truncated;
        assert(!truncated.load(bytes.data(), n, hw));
    }
    auto corrupt = bytes;
    corrupt.back() ^= 1;
    cntrl::Document invalid;
    assert(!invalid.load(corrupt.data(), unsigned(corrupt.size()), hw));
    for (unsigned offset : {28u, 29u, 30u, 36u, 76u + 2 * 68 + 68}) {
        corrupt = bytes; corrupt[offset] = 255;
        cntrl::put32(corrupt.data() + 20, cntrl::checksum(corrupt.data() + 28, unsigned(corrupt.size()) - 28));
        assert(!invalid.load(corrupt.data(), unsigned(corrupt.size()), hw));
    }
    for (unsigned offset : {45u, 46u}) { // Valid size/padding, but not shared.
        corrupt = bytes; corrupt[offset] = 8;
        cntrl::put32(corrupt.data() + 20, cntrl::checksum(corrupt.data() + 28, unsigned(corrupt.size()) - 28));
        assert(!invalid.load(corrupt.data(), unsigned(corrupt.size()), hw));
    }
    // Structurally invalid but checksum-correct documents are rejected too.
    corrupt = bytes; corrupt[76 + 64] = 16;
    cntrl::put32(corrupt.data() + 20, cntrl::checksum(corrupt.data() + 28, unsigned(corrupt.size()) - 28));
    assert(!invalid.load(corrupt.data(), unsigned(corrupt.size()), hw));
    cntrl::Controller app(hw);
    for (unsigned value : {0u, 64u, 255u}) {
        auto configured = bytes;
        configured[18] = uint8_t(value);
        cntrl::Document settings;
        assert(settings.load(configured.data(), unsigned(configured.size()), hw));
        app.activate(settings);
        assert(hw.brightness == value && hw.sent.empty());
    }
    corrupt = bytes; corrupt[19] = 2;
    assert(!invalid.load(corrupt.data(), unsigned(corrupt.size()), hw));
    corrupt = bytes; cntrl::put32(corrupt.data() + 24, 0x80000000u);
    assert(!invalid.load(corrupt.data(), unsigned(corrupt.size()), hw));
    corrupt = bytes; corrupt[4] = 1;
    assert(!invalid.load(corrupt.data(), unsigned(corrupt.size()), hw));
    app.activate(doc);
    assert(hw.brightness == 192);
    assert(hw.sent.empty() && !hw.panes[0].patch[0] && !hw.panes[1].patch[0]);
    assert(!hw.panes[0].category[0] && !hw.panes[1].category[0]);
    assert(hw.panes[2].category_x >= 4 && hw.panes[2].patch_x >= 4);
    assert(!std::strcmp(hw.panes[2].patch, "Flute"));
    app.action(0); assert(!std::strcmp(hw.panes[2].patch, "Strings"));
    app.action(1); app.action(1); // Sax
    app.action(4); assert(!std::strcmp(hw.panes[2].patch, "Pad"));
    app.action(3); assert(!std::strcmp(hw.panes[2].patch, "Sax"));
    assert(hw.sent.empty());
    app.activate(doc);
    app.action(2); // A:Flute, bank select + PC + volume + pan.
    assert((hw.sent.back() == std::vector<uint8_t>{0xb0, 0, 1, 0xb0, 32, 2, 0xc0, 0, 0xb0, 7, 100, 0xb0, 10, 0}));
    app.action(0); app.action(2); // B:Strings
    app.action(0); app.action(2); // A:Sax
    assert(!std::strcmp(hw.panes[0].patch, "Flute"));
    app.action(5);
    assert(hw.routes.back() == "din" && hw.sent.back()[7] == 0);
    assert(!std::strcmp(hw.panes[1].patch, "Flute"));
    assert(!hw.panes[0].patch[0]); // Next undo is B's first send.
    unsigned count = unsigned(hw.sent.size());
    app.action(5); assert(hw.sent.size() == count);
    assert(!std::strcmp(hw.panes[1].patch, "Flute"));
    app.action(5); assert(hw.sent.size() == count && app.depth() == 0);
    assert(!hw.panes[0].patch[0] && !hw.panes[1].patch[0]);
    assert(!std::strcmp(hw.panes[2].patch, "Sax"));
    app.action(5); assert(hw.sent.size() == count);
    hw.available = false; app.action(2); assert(app.depth() == 0 && hw.errors);
    hw.available = true; app.action(2); app.action(0); app.action(2);
    hw.available = false; app.action(5); assert(app.depth() == 2);
    hw.available = true;
    app.activate(doc);
    for (unsigned i = 0; i < cntrl::history_capacity + 20; ++i) app.action(2);
    assert(app.depth() == cntrl::history_capacity);
    for (unsigned i = 0; i < cntrl::history_capacity; ++i) app.action(5);
    assert(app.depth() == 0);
    // Single release, inclusive pairing, ignored third button, and C immediacy.
    app.activate(doc);
    app.button(1, true, 0); app.button(1, false, 40);
    assert(!std::strcmp(hw.panes[2].patch, "Sax"));
    app.button(0, true, 100); app.button(1, true, 200);
    app.button(4, true, 201); app.tick(500);
    assert(!std::strcmp(hw.panes[2].patch, "Sax"));
    count = unsigned(hw.sent.size()); app.button(2, true, 501);
    assert(hw.sent.size() == count + 1);
    app.button(0, false, 502); app.button(1, false, 503);
    app.button(4, false, 504);
    app.button(0, true, 600); app.button(4, true, 650); // Unrecognized A+E.
    app.button(0, false, 651); app.button(4, false, 652);
    assert(!std::strcmp(hw.panes[2].patch, "Sax"));
    app.button(1, true, 0xfffffff0u); app.tick(0x54u);
    assert(!std::strcmp(hw.panes[2].patch, "Sax")); // Exactly 100 ms.
    app.tick(0x55u); assert(!std::strcmp(hw.panes[2].patch, "Strings"));
    app.button(1, false, 0x56u); app.tick(0x100u);
    assert(!std::strcmp(hw.panes[2].patch, "Strings"));
    const unsigned pairs[][2] = {{0, 1}, {0, 3}, {1, 4}, {3, 4}};
    const cntrl::Chord recognized[] = {cntrl::Chord::ab, cntrl::Chord::ad,
                                      cntrl::Chord::be, cntrl::Chord::de};
    for (unsigned i = 0; i < 4; ++i) for (unsigned reverse = 0; reverse < 2; ++reverse) {
        app.activate(doc);
        app.button(pairs[i][reverse], true, 0);
        assert(app.button(pairs[i][1 - reverse], true, 100) == recognized[i]);
        app.tick(500);
        assert(!std::strcmp(hw.panes[2].patch, "Flute"));
    }
    // Persistent upload, status, reboot, and interrupted inactive-slot writes.
    Memory memory;
    Fake io;
    cntrl::Controller runner(io);
    cntrl::Documents docs(memory, io, runner, io);
    docs.boot(); assert(io.sent.empty());
    upload(docs, bytes); assert(io.replies.back().find("CNTRL DONE ") == 0);
    assert(io.sent.empty() && !std::strcmp(io.panes[2].patch, "Flute"));
    command(docs, "CNTRL STATUS"); assert(io.replies.back().find("CNTRL READY 5 ") == 0);
    cntrl::Documents reboot(memory, io, runner, io);
    reboot.boot(); assert(!std::strcmp(io.panes[2].patch, "Flute"));
    auto new_show = bytes;
    new_show[18] = 128;
    unsigned first_patch = 76 + 2 * 68 + 2 * 74;
    std::memcpy(new_show.data() + first_patch + 12, "Oboe\0\0", 6);
    cntrl::put32(new_show.data() + 20,
                 cntrl::checksum(new_show.data() + 28, unsigned(new_show.size()) - 28));
    // Every byte boundary in the new slot must leave the previous slot bootable.
    for (unsigned limit = 0; limit < bytes.size() + 20; ++limit) {
        Memory interrupted = memory;
        interrupted.remaining = int(limit);
        cntrl::Documents replacement(interrupted, io, runner, io);
        replacement.boot(); upload(replacement, new_show);
        assert(io.replies.back().find("CNTRL ERROR ") == 0);
        cntrl::Documents recovery(interrupted, io, runner, io);
        recovery.boot(); assert(!std::strcmp(io.panes[2].patch, "Flute"));
    }
    upload(reboot, new_show);
    assert(io.replies.back().find("CNTRL DONE ") == 0);
    assert(!std::strcmp(io.panes[2].patch, "Oboe"));
    assert(io.brightness == 128);
    io.brightness = 0;
    cntrl::Documents latest(memory, io, runner, io);
    latest.boot(); assert(!std::strcmp(io.panes[2].patch, "Oboe"));
    assert(io.brightness == 128);
    upload(reboot, corrupt); assert(io.replies.back() == "CNTRL ERROR invalid-document");
    command(reboot, "CNTRL BEGIN 500 00000000");
    reboot.receive(0, 0); reboot.tick(10001);
    assert(io.replies.back() == "CNTRL ERROR transfer-timeout");
    command(reboot, std::string(120, 'x'));
    assert(io.replies.back() == "CNTRL ERROR command-too-long");
    std::cout << "portable controller, decoder, gestures and persistence passed\n";
}
