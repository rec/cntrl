#include <cntrl/documents.h>
#include <cassert>
#include <fstream>
#include <string>
#include <vector>
#include <iostream>
#include "storage.h"
#include <cntrl/setup_ui.h>

struct Fake : cntrl::Hardware, cntrl::Replies {
    std::vector<std::vector<uint8_t>> sent;
    std::vector<std::string> routes, replies;
    std::vector<bool> transitions;
    std::vector<unsigned> durations;
    cntrl::Pane panes[3];
    bool available = true;
    unsigned errors = 0;
    uint8_t brightness = 0;
    bool splash = false;
    bool usb_midi_interface = false;
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
    void transition(bool undo, unsigned duration_ms) override {
        transitions.push_back(undo); durations.push_back(duration_ms);
    }
    void set_brightness(uint8_t value) override { brightness = value; }
    void set_usb_midi_interface(bool value) override { usb_midi_interface = value; }
    void set_splash(bool enabled, uint32_t) override { splash = enabled; }
    void error(const char *) override { ++errors; }
    void reply(const char *s) override { replies.emplace_back(s); }
};

void command(cntrl::Documents &docs, const std::string &line, uint32_t now = 0) {
    for (unsigned char c : line + "\n") docs.receive(c, now);
}
void upload(cntrl::Documents &docs, const std::vector<uint8_t> &data, const char *name = "Test") {
    char line[220], hex[127]; cntrl::hex_text(name, hex);
    std::snprintf(line, sizeof(line), "CNTRL SETUP PUT %s %u %08lx 1", hex, unsigned(data.size()),
                  (unsigned long)cntrl::checksum(data.data(), unsigned(data.size())));
    command(docs, line);
    for (uint8_t b : data) docs.receive(b, 0);
    while (docs.busy()) docs.tick(1);
}

void test_greyed_names(const cntrl::Document &doc) {
    Fake hw;
    cntrl::Controller app(hw);
    app.activate(doc);
    assert(hw.panes[2].patch_color == hw.panes[2].graphics.foreground);
    app.action(2); // A:Flute is now known to be selected.
    assert(hw.panes[2].patch_color == hw.panes[2].graphics.half);
    assert(hw.panes[1].patch_color == hw.panes[1].graphics.foreground);
    app.action(0); // B:Strings has not been sent.
    assert(hw.panes[2].patch_color == hw.panes[2].graphics.foreground);
    app.action(2);
    app.action(1); // Back to A:Flute, although B was sent more recently.
    assert(hw.panes[2].patch_color == hw.panes[2].graphics.half);
    app.action(1); // A:Sax is different from A's last send.
    assert(hw.panes[2].patch_color == hw.panes[2].graphics.foreground);
    app.action(2);
    assert(hw.panes[2].patch_color == hw.panes[2].graphics.half);
    hw.available = false;
    app.action(5); // Failed undo leaves A:Sax selected.
    assert(hw.panes[2].patch_color == hw.panes[2].graphics.half);
    hw.available = true;
    app.action(5); // Restore A:Flute, leaving A:Sax as candidate.
    assert(hw.panes[2].patch_color == hw.panes[2].graphics.foreground);
    app.action(0);
    assert(hw.panes[2].patch_color == hw.panes[2].graphics.half);
    unsigned sent = unsigned(hw.sent.size());
    app.action(5); app.action(5); // Unknown pre-show selections: no MIDI sent.
    assert(hw.sent.size() == sent && app.depth() == 0);
    assert(hw.panes[2].patch_color == hw.panes[2].graphics.half);
    app.action(1); // A:Sax
    hw.available = false; app.action(2);
    assert(hw.panes[2].patch_color == hw.panes[2].graphics.foreground);
    app.action(0);
    assert(hw.panes[2].patch_color == hw.panes[2].graphics.half);
    app.activate(doc); // A new document starts with unknown instrument state.
    assert(hw.panes[2].patch_color == hw.panes[2].graphics.foreground);
}

void test_undo_restores_undone_candidate(const cntrl::Document &doc) {
    Fake hw;
    cntrl::Controller app(hw); app.activate(doc);
    app.action(5); assert(hw.transitions.empty());
    hw.available = false; app.action(2); assert(hw.transitions.empty());
    hw.available = true; app.action(2); // Flute.
    app.action(0); app.action(2); // Strings.
    app.action(0); // Browse away to Sax before undoing Strings.
    unsigned sent = hw.sent.size();
    app.action(5);
    assert(hw.sent.size() == sent); // No previous selection for Strings' instrument.
    assert(!std::strcmp(hw.panes[2].patch, "Strings"));
    assert((hw.transitions == std::vector<bool>{false, false, true}));
    assert((hw.durations == std::vector<unsigned>{1000, 1000, 1000}));
    app.action(5);
    assert(!std::strcmp(hw.panes[2].patch, "Flute"));
    assert(!hw.panes[0].patch[0] && !hw.panes[1].patch[0]);
    std::vector<uint8_t> configured(doc.data(), doc.data() + doc.length());
    configured[doc.metadata() + 6] = 0x4d;
    configured[doc.metadata() + 7] = 1; // 333 milliseconds.
    cntrl::put32(configured.data() + 20, cntrl::checksum(configured.data() + 28, configured.size() - 28));
    cntrl::Document custom; assert(custom.load(configured.data(), configured.size(), hw));
    app.activate(custom); hw.durations.clear();
    app.action(2); app.action(5);
    assert((hw.durations == std::vector<unsigned>{333, 333}));
}

void test_greyed_aliases(const std::vector<uint8_t> &bytes) {
    Fake hw;
    cntrl::Controller app(hw);
    auto aliased = bytes;
    unsigned first = 76 + 2 * 68 + 2 * 74, second = first + 82;
    aliased[second + 4] = aliased[first + 4]; // Same PC, but a different bank.
    cntrl::put32(aliased.data() + 20, cntrl::checksum(aliased.data() + 28, unsigned(aliased.size()) - 28));
    cntrl::Document doc;
    assert(doc.load(aliased.data(), unsigned(aliased.size()), hw));
    app.activate(doc); app.action(2); app.action(1);
    assert(hw.panes[2].patch_color == hw.panes[2].graphics.foreground);
    std::memcpy(aliased.data() + second + 5, aliased.data() + first + 5, 2);
    cntrl::put32(aliased.data() + (doc.bank(1) - aliased.data()), cntrl::u32(doc.bank(0)));
    cntrl::put32(aliased.data() + 20, cntrl::checksum(aliased.data() + 28, unsigned(aliased.size()) - 28));
    assert(doc.load(aliased.data(), unsigned(aliased.size()), hw));
    app.activate(doc); app.action(2); app.action(1);
    assert(!std::strcmp(hw.panes[2].patch, "Sax")); // Different name, same patch address.
    assert(hw.panes[2].patch_color == hw.panes[2].graphics.half);
    unsigned sent = unsigned(hw.sent.size());
    app.action(2); // Grey is informational; it must not suppress repeated sends.
    assert(hw.sent.size() == sent + 1);
}

void test_setup_editing_and_empty_ui(const std::vector<uint8_t> &bytes) {
    TestStorage card; Fake hw; cntrl::Controller controller(hw);
    cntrl::Documents docs(card, hw, controller, hw);
    docs.boot();
    assert(hw.splash); // No setup still uses the installed startup animation.
    for (const auto &pane : hw.panes) assert(pane.centered && !std::strcmp(pane.patch, "No setups"));
    controller.button(0, true, 0);
    assert(controller.button(3, true, 50) == cntrl::Chord::ad); // No document needed.
    controller.button(0, false, 60); controller.button(3, false, 61);
    upload(docs, bytes); command(docs, "CNTRL SETUP FINISH");
    cntrl::SetupUi ui(docs, hw);
    ui.enter(); ui.poll(0);
    for (const auto &pane : hw.panes) assert(pane.graphics.size == 16 && pane.graphics.line_height == 16 && pane.wrap_patch);
    ui.button(2, true, 1); // Select/Edit menu.
    ui.button(1, true, 2); ui.button(2, true, 3); // Edit selected setup.
    assert(docs.dirty() && hw.sent.empty());
    ui.button(2, true, 4); ui.button(2, true, 5); // PC value of first patch.
    ui.button(1, true, 6);
    assert(docs.edit_value(0, 0) == 2 && hw.sent.empty());
    command(docs, "CNTRL SETUP SET 54657374");
    assert(hw.replies.back() == "CNTRL ERROR unsaved-edits");
    assert(docs.edit_value(0, 0) == 2);
    ui.button(2, true, 7); ui.button(5, true, 8); ui.button(5, true, 9);
    ui.button(0, true, 10); ui.button(0, true, 11); ui.button(2, true, 12); // Save.
    while (docs.busy()) docs.tick(13);
    ui.poll(0);
    assert(!ui.open() && !docs.dirty() && docs.document().patch(0)[4] == 1);
    assert(hw.sent.empty());
    // Each numeric field is edited in its existing ownership scope.
    assert(docs.edit_begin());
    assert(docs.edit_adjust(0, 1, -1) && docs.edit_value(0, 1) == -1);
    assert(docs.edit_adjust(0, 2, 1) && docs.edit_value(0, 2) == 2);
    assert(docs.edit_adjust(0, 3, -1) && docs.edit_value(0, 3) == 99);
    assert(docs.edit_adjust(0, 4, 1) && docs.edit_value(0, 4) == -63);
    assert(docs.edit_adjust(0, 5, -1) && docs.edit_value(0, 5) == 191);
    assert(docs.edit_adjust(0, 6, 1) && docs.edit_value(0, 6) == 1);
    assert(docs.edit_value(0, 7) == 0);
    assert(docs.edit_adjust(0, 7, 1) && docs.edit_value(0, 7) == 1);
    assert(!docs.edit_adjust(0, 7, 1));
    docs.edit_cancel();
    assert(docs.document().brightness() == 192 && hw.sent.empty());
    // An interrupted inactive slot with a valid newer HEADER must not cause
    // an editor save to overwrite the only fully committed generation.
    TestStorage interrupted = card;
    interrupted.remaining = 96;
    cntrl::Documents broken(interrupted, hw, controller, hw); broken.boot(); upload(broken, bytes);
    interrupted.remaining = -1;
    cntrl::Documents recovered(interrupted, hw, controller, hw); recovered.boot();
    assert(recovered.edit_begin() && recovered.edit_adjust(0, 0, 1));
    interrupted.remaining = 120;
    assert(recovered.edit_save(0));
    while (recovered.busy()) recovered.tick(1);
    interrupted.remaining = -1;
    cntrl::Documents reboot(interrupted, hw, controller, hw); reboot.boot();
    assert(reboot.document().valid() && reboot.document().patch(0)[4] == 1);
    // Corrupt selection records are not treated as an unset selection.
    card.files[""][0][0] ^= 1;
    cntrl::Documents bad_selection(card, hw, controller, hw); bad_selection.boot();
    assert(!bad_selection.document().valid());
}

void test_deletion_and_selection_power_loss(const std::vector<uint8_t> &bytes) {
    TestStorage card; Fake hw; cntrl::Controller controller(hw);
    cntrl::Documents docs(card, hw, controller, hw); docs.boot();
    upload(docs, bytes, "Alpha"); upload(docs, bytes, "Zulu");
    command(docs, "CNTRL SETUP FINISH"); assert(!std::strcmp(docs.selected(), "Alpha"));
    // Every byte boundary of a selection update preserves the old selection.
    for (unsigned limit = 0; limit < 84; ++limit) {
        TestStorage interrupted = card; interrupted.remaining = int(limit);
        cntrl::Documents candidate(interrupted, hw, controller, hw); candidate.boot();
        assert(!candidate.select("Zulu")); interrupted.remaining = -1;
        cntrl::Documents reboot(interrupted, hw, controller, hw); reboot.boot();
        assert(!std::strcmp(reboot.selected(), "Alpha"));
    }
    const uint8_t names[] = {'A','l','p','h','a',0,'Z','u','l','u',0};
    char line[80]; std::snprintf(line, sizeof(line), "CNTRL SETUP DELETE %u %08lx", unsigned(sizeof(names)), (unsigned long)cntrl::checksum(names, sizeof(names)));
    // Selection clear + two 100-byte tombstones. Successful tombstones must
    // never expose their older payload when the next deletion is interrupted.
    for (unsigned limit = 0; limit <= 284; ++limit) {
        TestStorage interrupted = card; interrupted.remaining = int(limit);
        cntrl::Documents candidate(interrupted, hw, controller, hw); candidate.boot();
        command(candidate, line);
        for (uint8_t c : names) candidate.receive(c, 0);
        while (candidate.busy()) candidate.tick(1);
        interrupted.remaining = -1;
        cntrl::Documents reboot(interrupted, hw, controller, hw); reboot.boot();
        char name[64];
        if (limit >= 184) assert(!reboot.next_valid("", name) || !std::strcmp(name, "Zulu"));
        if (limit == 284) {
            assert(!reboot.document().valid() && !reboot.next_valid("", name));
            for (const auto &pane : hw.panes) assert(!std::strcmp(pane.patch, "No setups"));
        }
    }
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
    assert(doc.graphics(2).half == 0x7bef);
    test_greyed_names(doc);
    test_undo_restores_undone_candidate(doc);
    test_greyed_aliases(bytes);
    test_setup_editing_and_empty_ui(bytes);
    test_deletion_and_selection_power_loss(bytes);
    for (unsigned n = 0; n < bytes.size(); ++n) {
        cntrl::Document truncated;
        assert(!truncated.load(bytes.data(), n, hw));
    }
    auto corrupt = bytes;
    corrupt.back() ^= 1;
    cntrl::Document invalid;
    assert(!invalid.load(corrupt.data(), unsigned(corrupt.size()), hw));
    for (unsigned offset : {28u, 29u, 30u, 36u, 42u, 76u + 2 * 68 + 68}) {
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
    corrupt = bytes; corrupt[19] = 4;
    assert(!invalid.load(corrupt.data(), unsigned(corrupt.size()), hw));
    corrupt = bytes; cntrl::put32(corrupt.data() + 24, 0x80000000u);
    assert(!invalid.load(corrupt.data(), unsigned(corrupt.size()), hw));
    corrupt = bytes; corrupt[4] = 1;
    assert(!invalid.load(corrupt.data(), unsigned(corrupt.size()), hw));
    app.activate(doc);
    assert(hw.brightness == 192);
    assert(hw.splash);
    auto no_animation = bytes;
    no_animation[19] = 0;
    cntrl::Document disabled;
    assert(disabled.load(no_animation.data(), unsigned(no_animation.size()), hw));
    app.activate(disabled);
    assert(!hw.splash);
    app.activate(doc);
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
    assert(!std::strcmp(hw.panes[2].patch, "Flute"));
    app.action(5); assert(hw.sent.size() == count);
    hw.available = false; app.action(2); assert(app.depth() == 0 && hw.errors);
    hw.available = true; app.action(2); app.action(1); app.action(2);
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
    TestStorage memory;
    Fake io;
    cntrl::Controller runner(io);
    cntrl::Documents docs(memory, io, runner, io);
    docs.boot(); assert(io.sent.empty());
    upload(docs, bytes); assert(io.replies.back() == "CNTRL OK");
    command(docs, "CNTRL SETUP FINISH");
    assert(io.sent.empty() && !std::strcmp(io.panes[2].patch, "Flute"));
    command(docs, "CNTRL STATUS"); assert(io.replies.back().find("CNTRL READY 8 ") == 0);
    cntrl::Documents reboot(memory, io, runner, io);
    reboot.boot(); assert(!std::strcmp(io.panes[2].patch, "Flute"));
    auto new_show = bytes;
    new_show[18] = 128;
    unsigned first_patch = 76 + 2 * 68 + 2 * 74;
    std::memcpy(new_show.data() + first_patch + 12, "Oboe\0\0", 6);
    cntrl::put32(new_show.data() + 20,
                 cntrl::checksum(new_show.data() + 28, unsigned(new_show.size()) - 28));
    // Every byte boundary in the new slot must leave the previous slot bootable.
    for (unsigned limit = 0; limit < bytes.size() + 100; ++limit) {
        TestStorage interrupted = memory;
        interrupted.remaining = int(limit);
        cntrl::Documents replacement(interrupted, io, runner, io);
        replacement.boot(); upload(replacement, new_show);
        assert(io.replies.back().find("CNTRL ERROR ") == 0);
        cntrl::Documents recovery(interrupted, io, runner, io);
        recovery.boot(); assert(!std::strcmp(io.panes[2].patch, "Flute"));
    }
    upload(reboot, new_show);
    assert(io.replies.back() == "CNTRL OK");
    assert(!std::strcmp(io.panes[2].patch, "Oboe"));
    assert(io.brightness == 128);
    io.brightness = 0;
    cntrl::Documents latest(memory, io, runner, io);
    latest.boot(); assert(!std::strcmp(io.panes[2].patch, "Oboe"));
    assert(io.brightness == 128);
    upload(reboot, corrupt); assert(io.replies.back() == "CNTRL ERROR invalid-document");
    command(reboot, "CNTRL SETUP PUT 54657374 500 00000000 1");
    reboot.receive(0, 0); reboot.tick(10001);
    assert(io.replies.back() == "CNTRL ERROR transfer-timeout");
    command(reboot, std::string(300, 'x'));
    assert(io.replies.back() == "CNTRL ERROR command-too-long");
    std::cout << "portable controller, decoder, gestures and persistence passed\n";
}
