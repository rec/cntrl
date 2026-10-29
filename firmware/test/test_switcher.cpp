#include "patch_hardware.h"
#include <cntrl/documents.h>
#include <cassert>
#include <fstream>
#include <string>
#include <vector>
#include <iostream>
#include "storage.h"
#include <cntrl/setup_ui.h>

struct Fake : TestPatchHardware, cntrl::Replies {
    std::vector<std::vector<uint8_t>> sent;
    std::vector<std::string> routes, replies;
    std::vector<bool> transitions;
    std::vector<std::optional<unsigned>> durations;
    cntrl::Pane panes[3];
    bool connected = true;
    bool available(const char *output) const override { return connected && supports(output); }
    unsigned errors = 0;
    uint8_t brightness = 0;
    bool splash = false;
    bool usb_midi_interface = false;
    bool send(const char *route, const uint8_t *data, unsigned n) override {
        if (!connected) return false;
        sent.emplace_back(data, data + n); routes.emplace_back(route); return true;
    }
    bool supports(const char *route) const override {
        return !std::strcmp(route, "din") || !std::strcmp(route, "trs");
    }
    bool supports_font(unsigned font, unsigned size) const override { return font == 0 && size >= 8 && size <= 96 && size % 8 == 0; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned p, const cntrl::Pane &view) override { panes[p] = view; }
    void transition(bool undo, std::optional<unsigned> duration_ms) override {
        transitions.push_back(undo); durations.push_back(duration_ms);
    }
    void set_brightness(uint8_t value, bool) override { brightness = value; }
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

void sync_program(std::vector<uint8_t> &bytes, const cntrl::Document &document, unsigned index) {
    const uint8_t *cache = bytes.data() + (document.patch(index) - document.data());
    uint8_t *node = bytes.data() + (document.node(document.browser_root(index)) - document.data());
    assert(node[0] == 1);
    std::memcpy(node + 4, cache, 2);
    std::memcpy(node + 6, cache + 4, 5);
    std::memcpy(node + 11, bytes.data() + (document.bank(index) - document.data()), 4);
}

void test_program_change_brightness(const cntrl::Document &doc) {
    Fake hw; TestController app(hw); app.activate(doc);
    const auto normal = doc.brightness();
    app.button(2, true, 0); assert(hw.brightness == 255);
    auto count = hw.sent.size();
    app.button(2, true, 1); assert(hw.sent.size() == count);
    app.tick(1000); assert(hw.brightness == 255);
    app.button(2, false, 1001); assert(hw.brightness == normal);
    app.button(2, true, 1002); assert(hw.brightness == normal);
    app.button(2, false, 1003);
    app.action(1); // Another program for the same instrument.
    hw.connected = false;
    app.button(2, true, 1004); assert(hw.brightness == normal);
    app.button(2, false, 1005); hw.connected = true;
    app.button(2, true, 1006); assert(hw.brightness == 255);
    app.cancel_gesture(); assert(hw.brightness == normal);
    app.button(2, false, 1007); assert(hw.brightness == normal);
    app.action(5); // Undo changes the actual last-sent program.
    app.button(2, true, 1008); assert(hw.brightness == 255);
    app.activate(doc); assert(hw.brightness == normal);
    app.button(2, true, 1009); assert(hw.brightness == 255);
    app.deactivate(); assert(hw.brightness == normal);
    app.button(2, true, 1010); assert(hw.brightness == normal);
    app.button(2, false, 1011);
    // A different patch name/volume using the same program and bank does not flash.
    std::vector<uint8_t> aliases(doc.data(), doc.data() + doc.length());
    auto first = unsigned(doc.patch(0) - doc.data()), second = unsigned(doc.patch(1) - doc.data());
    std::memcpy(aliases.data() + second + 4, aliases.data() + first + 4, 3);
    aliases[second + 7] = 64;
    std::memcpy(aliases.data() + (doc.bank(1) - doc.data()), doc.bank(0), 4);
    sync_program(aliases, doc, 1);
    cntrl::put32(aliases.data() + 20, cntrl::checksum(aliases.data() + 28, unsigned(aliases.size()) - 28));
    for (uint8_t brightness : {uint8_t(0), uint8_t(255)}) {
        aliases[18] = brightness;
        cntrl::Document same_program; assert(same_program.load(aliases.data(), unsigned(aliases.size()), hw));
        app.activate(same_program);
        app.button(2, true, 1100); assert(hw.brightness == 255);
        app.button(2, false, 1101); assert(hw.brightness == brightness);
        app.action(1);
        app.button(2, true, 1102); assert(hw.brightness == brightness);
        app.button(2, false, 1103); assert(hw.brightness == brightness);
    }
}

void test_greyed_names(const cntrl::Document &doc) {
    Fake hw;
    TestController app(hw);
    app.activate(doc);
    assert(!hw.panes[2].lines[1].stippled && hw.panes[2].lines[1].graphics.foreground == hw.panes[2].graphics.foreground);
    app.action(2); // A:Flute is now known to be selected.
    assert(hw.panes[2].lines[1].stippled && hw.panes[2].lines[1].graphics.foreground == hw.panes[2].graphics.foreground);
    hw.connected = false;
    for (unsigned i = 0; i < 3; ++i) {
        app.button(2, true, 2 * i); app.button(2, false, 2 * i + 1);
        assert(hw.sent.size() == 1 && app.depth() == 1 && hw.transitions.size() == 1);
        assert(hw.errors == 0 && hw.brightness == doc.brightness());
    }
    hw.connected = true;
    assert(hw.panes[1].lines[1].graphics.foreground == hw.panes[1].graphics.foreground);
    app.action(0); // B:Strings has not been sent.
    assert(!hw.panes[2].lines[1].stippled && hw.panes[2].lines[1].graphics.foreground == hw.panes[2].graphics.foreground);
    app.action(2);
    app.action(1); // Back to A:Flute, although B was sent more recently.
    assert(hw.panes[2].lines[1].stippled && hw.panes[2].lines[1].graphics.foreground == hw.panes[2].graphics.foreground);
    auto sends = hw.sent.size(), transitions = hw.transitions.size();
    app.action(2);
    assert(hw.sent.size() == sends && hw.transitions.size() == transitions && app.depth() == 2);
    app.action(1); // A:Sax is different from A's last send.
    assert(!hw.panes[2].lines[1].stippled && hw.panes[2].lines[1].graphics.foreground == hw.panes[2].graphics.foreground);
    app.action(2);
    assert(hw.panes[2].lines[1].stippled && hw.panes[2].lines[1].graphics.foreground == hw.panes[2].graphics.foreground);
    hw.connected = false;
    app.action(5); // Failed undo leaves A:Sax selected.
    assert(hw.panes[2].lines[1].stippled && hw.panes[2].lines[1].graphics.foreground == hw.panes[2].graphics.foreground);
    hw.connected = true;
    app.action(5); // Restore A:Flute, leaving A:Sax as candidate.
    assert(!hw.panes[2].lines[1].stippled && hw.panes[2].lines[1].graphics.foreground == hw.panes[2].graphics.foreground);
    app.action(0);
    assert(hw.panes[2].lines[1].stippled && hw.panes[2].lines[1].graphics.foreground == hw.panes[2].graphics.foreground);
    unsigned sent = unsigned(hw.sent.size());
    app.action(5); app.action(5); // Unknown pre-show selections: no MIDI sent.
    assert(hw.sent.size() == sent && app.depth() == 0);
    assert(hw.panes[2].lines[1].stippled && hw.panes[2].lines[1].graphics.foreground == hw.panes[2].graphics.foreground);
    app.action(1); // A:Sax
    hw.connected = false; app.action(2);
    assert(!hw.panes[2].lines[1].stippled && hw.panes[2].lines[1].graphics.foreground == hw.panes[2].graphics.foreground);
    app.action(0);
    assert(hw.panes[2].lines[1].stippled && hw.panes[2].lines[1].graphics.foreground == hw.panes[2].graphics.foreground);
    app.activate(doc); // A new document starts with unknown instrument state.
    assert(!hw.panes[2].lines[1].stippled && hw.panes[2].lines[1].graphics.foreground == hw.panes[2].graphics.foreground);
}

void test_undo_restores_undone_candidate(const cntrl::Document &doc) {
    Fake hw;
    TestController app(hw); app.activate(doc);
    app.action(5); assert(hw.transitions.empty());
    hw.connected = false; app.action(2); assert(hw.transitions.empty());
    hw.connected = true; app.action(2); // Flute.
    app.action(0); app.action(2); // Strings.
    app.action(0); // Browse away to Sax before undoing Strings.
    unsigned sent = hw.sent.size();
    app.action(5);
    assert(hw.sent.size() == sent); // No previous selection for Strings' instrument.
    assert(!std::strcmp(hw.panes[2].lines[1].text, "Strings"));
    assert((hw.transitions == std::vector<bool>{false, false, true}));
    assert((hw.durations == std::vector<std::optional<unsigned>>{std::nullopt, std::nullopt, std::nullopt}));
    app.action(5);
    assert(!std::strcmp(hw.panes[2].lines[1].text, "Flute"));
    assert(!hw.panes[0].lines[1].text[0] && !hw.panes[1].lines[1].text[0]);
    std::vector<uint8_t> configured(doc.data(), doc.data() + doc.length());
    configured[doc.metadata() + 6] = 0x4d;
    configured[doc.metadata() + 7] = 1; // 333 milliseconds.
    cntrl::put32(configured.data() + 20, cntrl::checksum(configured.data() + 28, configured.size() - 28));
    cntrl::Document custom; assert(custom.load(configured.data(), configured.size(), hw));
    app.activate(custom); hw.durations.clear();
    app.action(2); app.action(5);
    assert((hw.durations == std::vector<std::optional<unsigned>>{333, 333}));
}

void test_greyed_aliases(const std::vector<uint8_t> &bytes) {
    Fake hw;
    TestController app(hw);
    auto aliased = bytes;
    cntrl::Document doc; assert(doc.load(bytes.data(), unsigned(bytes.size()), hw));
    unsigned first = 76 + 2 * 68 + 2 * 68, second = first + 82;
    aliased[second + 4] = aliased[first + 4]; // Same PC, but a different bank.
    sync_program(aliased, doc, 1);
    cntrl::put32(aliased.data() + 20, cntrl::checksum(aliased.data() + 28, unsigned(aliased.size()) - 28));
    assert(doc.load(aliased.data(), unsigned(aliased.size()), hw));
    app.activate(doc); app.action(2); app.action(1);
    assert(!hw.panes[2].lines[1].stippled && hw.panes[2].lines[1].graphics.foreground == hw.panes[2].graphics.foreground);
    std::memcpy(aliased.data() + second + 5, aliased.data() + first + 5, 2);
    cntrl::put32(aliased.data() + (doc.bank(1) - aliased.data()), cntrl::u32(doc.bank(0)));
    sync_program(aliased, doc, 1);
    cntrl::put32(aliased.data() + 20, cntrl::checksum(aliased.data() + 28, unsigned(aliased.size()) - 28));
    assert(doc.load(aliased.data(), unsigned(aliased.size()), hw));
    app.activate(doc); app.action(2); app.action(1);
    assert(!std::strcmp(hw.panes[2].lines[1].text, "Sax")); // Different name, same patch address.
    assert(hw.panes[2].lines[1].stippled && hw.panes[2].lines[1].graphics.foreground == hw.panes[2].graphics.foreground);
    unsigned sent = unsigned(hw.sent.size());
    auto transitions = hw.transitions.size();
    app.button(2, true, 100); app.button(2, false, 101);
    assert(hw.sent.size() == sent && hw.transitions.size() == transitions && app.depth() == 1);
    assert(hw.brightness == doc.brightness());
}

void test_setup_editing_and_empty_ui(const std::vector<uint8_t> &bytes) {
    TestStorage card; Fake hw; TestController controller(hw);
    cntrl::Documents docs(card, hw, controller, hw);
    docs.boot();
    assert(hw.splash); // No setup still uses the installed startup animation.
    for (const auto &pane : hw.panes) assert(pane.lines[1].centered && !std::strcmp(pane.lines[1].text, "No setups"));
    controller.button(0, true, 0);
    assert(controller.button(3, true, 50) == cntrl::Chord::ad); // No document needed.
    controller.button(0, false, 60); controller.button(3, false, 61);
    upload(docs, bytes); command(docs, "CNTRL SETUP FINISH");
    cntrl::SetupUi ui(docs, hw);
    ui.enter(); ui.poll(0);
    for (const auto &pane : hw.panes) assert(pane.lines[1].graphics.size == 16 && pane.lines[1].graphics.line_height == 16 && pane.lines[1].wrap);
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
    assert(recovered.edit_save(0)); // Failure is reported by the asynchronous job.
    while (recovered.busy()) recovered.tick(1);
    assert(recovered.dirty() && recovered.edit_value(0, 0) == 3);
    interrupted.remaining = -1;
    assert(recovered.edit_save(2));
    while (recovered.busy()) recovered.tick(3);
    assert(!recovered.dirty() && recovered.document().patch(0)[4] == 2);
    cntrl::Documents reboot(interrupted, hw, controller, hw); reboot.boot();
    assert(reboot.document().valid() && reboot.document().patch(0)[4] == 2);
    // Corrupt selection records are not treated as an unset selection.
    card.files[""][0][0] ^= 1;
    cntrl::Documents bad_selection(card, hw, controller, hw); bad_selection.boot();
    assert(!bad_selection.document().valid());
}

void test_deletion_and_selection_power_loss(const std::vector<uint8_t> &bytes) {
    TestStorage card; Fake hw; TestController controller(hw);
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
            for (const auto &pane : hw.panes) assert(!std::strcmp(pane.lines[1].text, "No setups"));
        }
    }
}

void test_revision_history(const std::vector<uint8_t> &bytes) {
    TestStorage card; Fake hw; TestController controller(hw);
    cntrl::Documents docs(card, hw, controller, hw); docs.boot();
    auto finish = [&](cntrl::Documents &d) {
        unsigned ticks = 0;
        while (d.busy()) { d.tick(1); ++ticks; assert(ticks < 3000000); }
        assert(!d.last_error()[0]);
    };
    auto latest = [&](cntrl::Documents &d, uint64_t expected) {
        assert(d.history_next("Test", UINT64_MAX)); finish(d);
        assert(d.history_revision().number == expected);
    };
    upload(docs, bytes); command(docs, "CNTRL SETUP FINISH");
    assert(docs.history_save("Test", "Rehearsal"));
    assert(docs.busy()); finish(docs);
    assert(!std::strcmp(docs.history_revision().label, "Rehearsal"));
    upload(docs, bytes); latest(docs, 1);
    assert(docs.edit_begin() && docs.edit_adjust(0, 0, 1) && docs.edit_save(0)); finish(docs);
    latest(docs, 2);
    assert(docs.history_compare("Test", 1)); finish(docs);
    assert(docs.history_comparison().patches == 1);
    auto sent = hw.sent.size();
    assert(docs.history_restore("Test", 1, 2)); finish(docs);
    assert(hw.sent.size() == sent); latest(docs, 3);
    assert(docs.history_restore("Test", 3, 4)); finish(docs);
    assert(docs.history_delete("Test", 4)); finish(docs);
    assert(docs.history_save("Test", "After deletion")); finish(docs); latest(docs, 5);
    card.remaining = 30;
    assert(docs.history_save("Test", "Interrupted"));
    while (docs.busy()) docs.tick(1);
    assert(docs.last_error()[0]); card.remaining = -1;
    assert(docs.history_save("Test", "After interruption")); finish(docs); latest(docs, 7);
    for (unsigned i = 0; i < 1000; ++i) { assert(docs.history_save("Test", "")); finish(docs); }
    latest(docs, 1007);
    cntrl::Documents reboot(card, hw, controller, hw); reboot.boot();
    assert(reboot.document().valid());
    assert(reboot.history_delete("Test", 0)); finish(reboot); latest(reboot, 0);
    assert(reboot.history_save("Test", "Still monotonic")); finish(reboot); latest(reboot, 1008);
    command(reboot, "CNTRL HISTORY SAVE 54657374 0 -");
    for (unsigned tick = 1; reboot.busy(); ++tick) {
        auto replies = hw.replies.size();
        reboot.tick(tick * 1000);
        assert(hw.replies.size() <= replies + 1);
    }
    assert(hw.replies.back() != "CNTRL WAIT");
}

void test_catalogue_jobs_are_bounded_and_preserve_order(const std::vector<uint8_t> &bytes) {
    struct Card : TestStorage {
        unsigned io = 0, entries = 0;
        decltype(files)::const_reverse_iterator cursor;
        void catalogue_begin() override { cursor = files.rbegin(); }
        bool catalogue_step(char *name) override {
            ++entries; name[0] = 0;
            if (cursor == files.rend()) return true;
            if (cntrl::setup_name(cursor->first.c_str())) std::snprintf(name, 64, "%s", cursor->first.c_str());
            ++cursor; return false;
        }
        bool read(unsigned slot, unsigned offset, uint8_t *data, unsigned n) override {
            io += n; return TestStorage::read(slot, offset, data, n);
        }
        bool write(const uint8_t *data, unsigned n) override { io += n; return TestStorage::write(data, n); }
    } card;
    Fake hw; TestController controller(hw); cntrl::Documents docs(card, hw, controller, hw);
    docs.boot();
    for (const char *name : {"Zulu", "Alpha", "Middle", "Deleted"}) upload(docs, bytes, name);
    // Newer tombstone hides the older valid payload.
    auto &deleted = card.files["Deleted"];
    deleted[1].assign(deleted[0].begin(), deleted[0].begin() + 96); deleted[1].resize(100);
    cntrl::put32(deleted[1].data() + 4, cntrl::u32(deleted[0].data() + 4) + 1);
    cntrl::put32(deleted[1].data() + 8, 0); cntrl::put32(deleted[1].data() + 12, 0);
    cntrl::put32(deleted[1].data() + 92, cntrl::checksum(deleted[1].data(), 92));
    std::memcpy(deleted[1].data() + 96, "DONE", 4);
    // A damaged newer payload must fall back to the older valid slot.
    auto &middle = card.files["Middle"]; middle[1] = middle[0]; middle[1][96] ^= 1;
    cntrl::put32(middle[1].data() + 4, cntrl::u32(middle[0].data() + 4) + 1);
    cntrl::put32(middle[1].data() + 92, cntrl::checksum(middle[1].data(), 92));
    for (unsigned i = 0; i < 1000; ++i) card.files["Invalid" + std::to_string(i)][0] = {'B'};
    command(docs, "CNTRL SETUP FINISH"); assert(!std::strcmp(docs.selected(), "Alpha"));
    auto finish = [&] {
        unsigned tick = 0;
        while (docs.busy()) {
            card.io = card.entries = 0; auto replies = hw.replies.size();
            docs.tick(++tick * 10);
            assert(card.io <= 512 && card.entries <= 1 && hw.replies.size() <= replies + 1);
            assert(tick < 100000);
        }
        assert(!docs.last_error()[0] && hw.sent.empty());
    };
    cntrl::SetupUi ui(docs, hw); ui.enter(); ui.poll(0); ui.button(1, true, 0);
    assert(docs.busy() && !std::strcmp(docs.selected(), "Alpha")); finish(); ui.poll(0);
    assert(!std::strcmp(hw.panes[1].lines[1].text, "Middle"));
    for (const char *expected : {"Middle", "Zulu", "Alpha"}) {
        command(docs, "CNTRL SETUP FORWARD"); assert(docs.busy()); finish();
        assert(!std::strcmp(docs.selected(), expected) && hw.replies.back() == "CNTRL OK");
    }
    command(docs, "CNTRL SETUP BACKWARD"); finish(); assert(!std::strcmp(docs.selected(), "Zulu"));
    assert(docs.browse("Alpha", false)); assert(!docs.browse("Alpha", true));
    finish(); assert(!std::strcmp(docs.browse_result(), "Middle"));
}

int main(int argc, char **argv) {
    assert(argc == 3);
    std::ifstream file(argv[1]);
    std::string hex; file >> hex;
    assert(!hex.empty() && hex.size() % 2 == 0);
    std::vector<uint8_t> bytes;
    for (unsigned i = 0; i < hex.size(); i += 2)
        bytes.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
    Fake hw;
    test_catalogue_jobs_are_bounded_and_preserve_order(bytes);
    cntrl::Document doc;
    assert(doc.load(bytes.data(), unsigned(bytes.size()), hw));
    {
        std::ifstream display_file(argv[2]);
        std::string display_hex; display_file >> display_hex;
        std::vector<uint8_t> display_bytes;
        for (unsigned i = 0; i < display_hex.size(); i += 2)
            display_bytes.push_back(uint8_t(std::stoul(display_hex.substr(i, 2), nullptr, 16)));
        cntrl::Document formatted;
        assert(formatted.load(display_bytes.data(), unsigned(display_bytes.size()), hw));
        Fake output;
        TestController app(output); app.activate(formatted);
        const auto &view = output.panes[2];
        assert(!std::strcmp(view.lines[0].text, "192"));
        assert(!std::strcmp(view.lines[1].text, "Flute"));
        assert(!view.lines[2].text[0]);
        assert(!std::strcmp(view.lines[3].text, "120"));
        assert(!std::strcmp(view.lines[4].text, "true"));
        assert(view.lines[0].graphics.size == 16 && view.lines[1].graphics.size == 16);
        assert(view.lines[1].graphics.background == 31 && view.lines[1].graphics.foreground == 65535);
        cntrl::Pane numeric, settings;
        formatted.page(numeric, 0, 0, false);
        assert(!std::strcmp(numeric.lines[0].text, "1"));
        assert(!numeric.lines[1].text[0] && !numeric.lines[2].text[0] && !numeric.lines[3].text[0]);
        assert(!std::strcmp(numeric.lines[4].text, "a"));
        assert(!std::strcmp(numeric.lines[5].text, "Flute"));
        formatted.page(settings, 0, 1, false);
        assert(!std::strcmp(settings.lines[0].text, "false"));
        assert(!std::strcmp(settings.lines[1].text, "0"));
        assert(!std::strcmp(settings.lines[2].text, "100"));
        assert(!settings.lines[3].text[0]);
        app.action(2);
        assert(output.panes[2].lines[1].stippled);
        assert(output.panes[2].lines[1].graphics.foreground == 65535);
        assert(!output.panes[2].lines[0].stippled);
        TestStorage storage;
        cntrl::Documents documents(storage, output, app, output);
        documents.boot(); upload(documents, display_bytes); command(documents, "CNTRL SETUP FINISH");
        assert(documents.edit_begin());
        assert(documents.edit_adjust(0, 3, 1) && documents.edit_adjust(0, 5, 1) && documents.edit_adjust(0, 0, 1));
        assert(documents.edit_save(100));
        while (documents.busy()) documents.tick(101);
        assert(!std::strcmp(output.panes[2].lines[0].text, "193"));
        assert(!std::strcmp(output.panes[2].lines[2].text, "0"));
        documents.document().page(numeric, 0, 0, false);
        assert(!std::strcmp(numeric.lines[0].text, "2"));
        assert(!std::strcmp(numeric.lines[2].text, "0"));
    }
    {
        auto scaled = bytes;
        unsigned display_offset = doc.display_metadata();
        unsigned style = display_offset + 16;
        for (unsigned i = 0; i < cntrl::u16(scaled.data() + display_offset + 4); ++i) {
            scaled[style + i * 16 + 1] = 16;
            scaled[style + i * 16 + 8] = 16;
        }
        cntrl::put32(scaled.data() + 20, cntrl::checksum(scaled.data() + 28, unsigned(scaled.size()) - 28));
        cntrl::Document autoscale;
        assert(autoscale.load(scaled.data(), unsigned(scaled.size()), hw));
        Fake display;
        TestController app(display);
        app.activate(autoscale);
        assert(display.panes[2].lines[0].graphics.size == 16);
        assert(display.panes[2].lines[1].graphics.size == 16);
        TestStorage storage;
        TestController controller(display);
        cntrl::Documents documents(storage, display, controller, display);
        documents.boot(); upload(documents, scaled); command(documents, "CNTRL SETUP FINISH");
        assert(documents.edit_begin() && documents.edit_adjust(0, 7, 1));
        assert(documents.edit_save(100));
        while (documents.busy()) documents.tick(101);
        assert(display.panes[2].lines[0].graphics.size == 16);
        scaled[style + 1] = 15; // Unsupported font size, with a valid checksum.
        cntrl::put32(scaled.data() + 20, cntrl::checksum(scaled.data() + 28, unsigned(scaled.size()) - 28));
        cntrl::Document invalid_layout;
        assert(!invalid_layout.load(scaled.data(), unsigned(scaled.size()), hw));
    }
    test_revision_history(bytes);
    test_program_change_brightness(doc);
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
    // Reserved display bytes must be zero in both windows and line styles.
    unsigned display = 0;
    for (unsigned i = 0; i + 4 <= bytes.size(); ++i)
        if (!std::memcmp(bytes.data() + i, "CDSP", 4)) { display = i; break; }
    assert(display);
    for (unsigned record : {28u, display + 16}) for (unsigned i = 12; i < 16; ++i) {
        corrupt = bytes; corrupt[record + i] = 1;
        cntrl::put32(corrupt.data() + 20, cntrl::checksum(corrupt.data() + 28, unsigned(corrupt.size()) - 28));
        assert(!invalid.load(corrupt.data(), unsigned(corrupt.size()), hw));
    }
    corrupt = bytes; corrupt[4] = 11;
    assert(!invalid.load(corrupt.data(), unsigned(corrupt.size()), hw));
    // Name metadata is length-delimited and must fit the document exactly.
    unsigned names = unsigned(doc.bank(doc.patches()) - bytes.data());
    for (unsigned offset : {names, names + 4}) {
        corrupt = bytes; corrupt[offset] = 254;
        cntrl::put32(corrupt.data() + 20, cntrl::checksum(corrupt.data() + 28, unsigned(corrupt.size()) - 28));
        assert(!invalid.load(corrupt.data(), unsigned(corrupt.size()), hw));
    }
    corrupt = bytes; corrupt.push_back(0);
    cntrl::put32(corrupt.data() + 8, unsigned(corrupt.size()));
    cntrl::put32(corrupt.data() + 20, cntrl::checksum(corrupt.data() + 28, unsigned(corrupt.size()) - 28));
    assert(!invalid.load(corrupt.data(), unsigned(corrupt.size()), hw));
    for (unsigned offset : {28u, 29u, 30u, 36u, 42u, 76u + 2 * 68 + 68}) {
        corrupt = bytes; corrupt[offset] = 255;
        cntrl::put32(corrupt.data() + 20, cntrl::checksum(corrupt.data() + 28, unsigned(corrupt.size()) - 28));
        assert(!invalid.load(corrupt.data(), unsigned(corrupt.size()), hw));
    }
    // Structurally invalid but checksum-correct documents are rejected too.
    corrupt = bytes; corrupt[76 + 64] = 16;
    cntrl::put32(corrupt.data() + 20, cntrl::checksum(corrupt.data() + 28, unsigned(corrupt.size()) - 28));
    assert(!invalid.load(corrupt.data(), unsigned(corrupt.size()), hw));
    TestController app(hw);
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
    assert(hw.sent.empty() && !hw.panes[0].lines[1].text[0] && !hw.panes[1].lines[1].text[0]);
    assert(!hw.panes[0].lines[0].text[0] && !hw.panes[1].lines[0].text[0]);
    assert(hw.panes[2].lines[0].x >= 4 && hw.panes[2].lines[1].x >= 4);
    assert(!std::strcmp(hw.panes[2].lines[1].text, "Flute"));
    app.action(0); assert(!std::strcmp(hw.panes[2].lines[1].text, "Strings"));
    app.action(1); app.action(1); // Sax
    app.action(4); assert(!std::strcmp(hw.panes[2].lines[1].text, "Pad"));
    app.action(3); assert(!std::strcmp(hw.panes[2].lines[1].text, "Sax"));
    assert(hw.sent.empty());
    app.activate(doc);
    app.action(2); // A:Flute, bank select + PC + volume + pan.
    assert((hw.sent.back() == std::vector<uint8_t>{0xb0, 0, 1, 0xb0, 32, 2, 0xc0, 0, 0xb0, 7, 100, 0xb0, 10, 0}));
    app.action(0); app.action(2); // B:Strings
    app.action(0); app.action(2); // A:Sax
    assert(!std::strcmp(hw.panes[0].lines[1].text, "Flute"));
    app.action(5);
    assert(hw.routes.back() == "din" && hw.sent.back()[7] == 0);
    assert(!std::strcmp(hw.panes[1].lines[1].text, "Flute"));
    assert(!hw.panes[0].lines[1].text[0]); // Next undo is B's first send.
    unsigned count = unsigned(hw.sent.size());
    app.action(5); assert(hw.sent.size() == count);
    assert(!std::strcmp(hw.panes[1].lines[1].text, "Flute"));
    app.action(5); assert(hw.sent.size() == count && app.depth() == 0);
    assert(!hw.panes[0].lines[1].text[0] && !hw.panes[1].lines[1].text[0]);
    assert(!std::strcmp(hw.panes[2].lines[1].text, "Flute"));
    app.action(5); assert(hw.sent.size() == count);
    app.activate(doc);
    hw.connected = false; app.action(2); assert(app.depth() == 0 && hw.errors);
    hw.connected = true; app.action(2); app.action(1); app.action(2);
    hw.connected = false; app.action(5); assert(app.depth() == 2);
    hw.connected = true;
    app.activate(doc);
    for (unsigned i = 0; i < cntrl::history_capacity + 20; ++i) {
        app.action(2); app.action(i % 2 ? 0 : 1); // Alternate Flute and Sax.
    }
    assert(app.depth() == cntrl::history_capacity);
    for (unsigned i = 0; i < cntrl::history_capacity; ++i) app.action(5);
    assert(app.depth() == 0);
    // Single release, inclusive pairing, ignored third button, and C immediacy.
    app.activate(doc);
    app.button(1, true, 0); app.button(1, false, 40);
    assert(!std::strcmp(hw.panes[2].lines[1].text, "Sax"));
    app.button(0, true, 100); app.button(1, true, 200);
    app.button(4, true, 201); app.tick(500);
    assert(!std::strcmp(hw.panes[2].lines[1].text, "Sax"));
    count = unsigned(hw.sent.size()); app.button(2, true, 501);
    assert(hw.sent.size() == count + 1);
    app.button(0, false, 502); app.button(1, false, 503);
    app.button(4, false, 504);
    app.button(0, true, 600); app.button(4, true, 650); // Unrecognized A+E.
    app.button(0, false, 651); app.button(4, false, 652);
    assert(!std::strcmp(hw.panes[2].lines[1].text, "Sax"));
    app.button(1, true, 0xfffffff0u); app.tick(0x54u);
    assert(!std::strcmp(hw.panes[2].lines[1].text, "Sax")); // Exactly 100 ms.
    app.tick(0x55u); assert(!std::strcmp(hw.panes[2].lines[1].text, "Strings"));
    app.button(1, false, 0x56u); app.tick(0x100u);
    assert(!std::strcmp(hw.panes[2].lines[1].text, "Strings"));
    const unsigned pairs[][2] = {{0, 1}, {0, 3}, {1, 4}, {3, 4}};
    const cntrl::Chord recognized[] = {cntrl::Chord::ab, cntrl::Chord::ad,
                                      cntrl::Chord::be, cntrl::Chord::de};
    for (unsigned i = 0; i < 4; ++i) for (unsigned reverse = 0; reverse < 2; ++reverse) {
        app.activate(doc);
        app.button(pairs[i][reverse], true, 0);
        assert(app.button(pairs[i][1 - reverse], true, 100) == recognized[i]);
        app.tick(500);
        assert(!std::strcmp(hw.panes[2].lines[1].text, "Flute"));
    }
    // Persistent upload, status, reboot, and interrupted inactive-slot writes.
    TestStorage memory;
    Fake io;
    TestController runner(io);
    cntrl::Documents docs(memory, io, runner, io);
    docs.boot(); assert(io.sent.empty());
    upload(docs, bytes); assert(io.replies.back() == "CNTRL OK");
    command(docs, "CNTRL SETUP FINISH");
    assert(io.sent.empty() && !std::strcmp(io.panes[2].lines[1].text, "Flute"));
    command(docs, "CNTRL STATUS"); assert(io.replies.back().find("CNTRL READY 18 ") == 0);
    cntrl::Documents reboot(memory, io, runner, io);
    reboot.boot(); assert(!std::strcmp(io.panes[2].lines[1].text, "Flute"));
    auto new_show = bytes;
    new_show[18] = 128;
    unsigned first_patch = 76 + 2 * 68 + 2 * 68;
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
        recovery.boot(); assert(!std::strcmp(io.panes[2].lines[1].text, "Flute"));
    }
    upload(reboot, new_show);
    assert(io.replies.back() == "CNTRL OK");
    assert(!std::strcmp(io.panes[2].lines[1].text, "Oboe"));
    assert(io.brightness == 128);
    io.brightness = 0;
    cntrl::Documents latest(memory, io, runner, io);
    latest.boot(); assert(!std::strcmp(io.panes[2].lines[1].text, "Oboe"));
    assert(io.brightness == 128);
    upload(reboot, corrupt); assert(io.replies.back() == "CNTRL ERROR invalid-document");
    command(reboot, "CNTRL SETUP PUT 54657374 500 00000000 1");
    reboot.receive(0, 0); reboot.tick(10001);
    assert(io.replies.back() == "CNTRL ERROR transfer-timeout");
    command(reboot, std::string(300, 'x'));
    assert(io.replies.back() == "CNTRL ERROR command-too-long");
    std::cout << "portable controller, decoder, gestures and persistence passed\n";
}
