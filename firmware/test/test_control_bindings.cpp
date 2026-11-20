#include "patch_hardware.h"
#include "storage.h"
#include <cntrl/switches.h>
#include <cntrl/songs.h>
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

struct Board : TestPatchHardware, cntrl::midi::Sink, cntrl::Replies {
    cntrl::midi::Bridge bridge{test_midi_outputs};
    cntrl::Switches switches{*this, *this, *this};
    cntrl::PatchExecutor entry{*this, *this, *this, 8};
    std::vector<cntrl::midi::Packet> sent;
    cntrl::Pane panes[3];
    uint8_t held = 0;
    uint32_t now = 0;
    unsigned pins = 0;
    bool exiting = false;
    bool expression_enabled = true;
    const cntrl::Document *foreign = nullptr;
    uint32_t source_signature = 0;
    uint32_t names_signature() const override { return source_signature; }
    bool acquire(uint16_t index, const cntrl::Document *&document, uint32_t &revision) override {
        if (index || !foreign) return false;
        document = foreign; revision = cntrl::u32(foreign->data() + foreign->core_offset() + 20); return true;
    }
    bool expression_available(unsigned) const override { return expression_enabled; }
    std::string response;
    void reply(const char *text) override { response = text; }
    void data(const uint8_t *, unsigned) override { assert(false); }
    void begin() override { ++pins; }
    void end() override { assert(pins); --pins; }
    unsigned switch_capacity() const override { return 5; }
    unsigned input_count() const override { return 8; }
    const char *input_label(unsigned input) const override {
        static constexpr const char *labels[] = {"A", "B", "C", "D", "E", "F", "G", "H"}; return labels[input];
    }
    bool supports_trs_mode(unsigned, cntrl::midi::TrsMode) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    bool send(const char *, const uint8_t *, unsigned) override { return false; }
    bool send_patch(unsigned owner, const char *, const uint8_t *bytes, unsigned size) override { return bridge.owned_send(owner, 0, bytes, size); }
    cntrl::midi::Bridge::Delivery patch_delivery(unsigned owner) const override { return bridge.delivery(owner); }
    void cancel_patch(unsigned owner) override { bridge.cancel_owner(owner); }
    bool write_packet(unsigned, const cntrl::midi::Packet &packet) override { sent.push_back(packet); return true; }
    void show(unsigned pane, const cntrl::Pane &content) override { panes[pane] = content; }
    void error(const char *message) override { std::fprintf(stderr, "%s\n", message); }
    bool state(unsigned operation, uint32_t) override { assert(operation == 10); exiting = true; return true; }
    cntrl::midi::Bridge::Delivery prepare_leave(cntrl::PatchExecutor &invocation) override { return switches.prepare_leave(invocation); }
    cntrl::midi::Bridge::Delivery bind_controls(const cntrl::Document &document, unsigned node, cntrl::PatchExecutor &invocation) override {
        return switches.bind_controls(document, node, invocation);
    }
    void tick(unsigned count = 32) {
        for (unsigned i = 0; i < count; ++i) { entry.tick(now); switches.tick(now++, held); bridge.service(*this, now); }
    }
    void button(unsigned input, bool down) {
        held = down ? held | (1u << input) : held & ~(1u << input);
        switches.button(input, down, now); tick();
    }
};

struct ScopedSongs : cntrl::SongResolver {
    Board &board;
    cntrl::Songs *songs = nullptr;
    std::vector<uint8_t> bytes;
    cntrl::Document document;
    explicit ScopedSongs(Board &board) : board(board) {}
    bool activate(const cntrl::SongSelection &selection, const cntrl::SongContinuation &context, bool) override {
        bytes.resize(songs->source_size(0));
        if (!songs->read_source(0, 0, bytes.data(), unsigned(bytes.size()))
            || !document.load(bytes.data(), unsigned(bytes.size()), board)) return false;
        board.source_signature = document.names_signature();
        const auto *set = songs->setlist(context.set);
        unsigned set_bindings = set ? set->bindings : ~0u;
        const auto *song = songs->song(context.song);
        return song && board.switches.binding_context(&document, set_bindings, song->bindings)
            && board.entry.start(document, selection.node);
    }
    cntrl::SongExecution execution_status() const override { return board.entry.busy() ? cntrl::SongExecution::pending : cntrl::SongExecution::completed; }
    bool revision(const cntrl::SongSelection &, uint32_t &) override { return false; }
    bool capture_source(const cntrl::Document *&) override { return false; }
};

void scoped_bindings(Board &board, const std::vector<uint8_t> &bytes) {
    TestStorage storage; ScopedSongs resolver(board); cntrl::Songs songs(storage, resolver); resolver.songs = &songs;
    assert(songs.boot() && songs.import_begin(unsigned(bytes.size())));
    assert(songs.import_write(bytes.data(), unsigned(bytes.size())));
    assert(songs.import_finish(unsigned(bytes.size()), cntrl::checksum(bytes.data(), unsigned(bytes.size()))));
    while (songs.busy()) songs.tick();
    assert(songs.success());
    const cntrl::Song original_style = *songs.song("One");
    auto next = [&]() { assert(songs.next()); board.tick(); songs.tick(); };
    auto recall = [&](const char *name) { assert(board.entry.start(resolver.document, unsigned(resolver.document.named_patch(name)))); board.tick(); };
    auto press = [&](unsigned input) { board.button(input, true); board.button(input, false); };
    songs.choose("Set"); next();
    assert(board.switches.expression(0).controller == 11);
    board.expression_enabled = false;
    recall("both");
    assert(board.entry.status() == cntrl::PatchExecutor::Status::failed);
    assert(board.switches.expression(0).controller == 11);
    board.expression_enabled = true;
    auto count = board.sent.size();
    press(0); press(1); press(3);
    assert(board.sent.size() == count); // Switches do not inherit by default.
    press(2);
    assert(board.sent.size() == count + 1 && board.sent.back().bytes[1] == 30);
    assert(board.switches.performance() && board.switches.expression(0).controller == 11);
    press(2); // An ordinary action leaves the map and pedal assignment intact.
    assert(board.sent.size() == count + 2);
    assert(board.switches.binding_context());
    press(4); // A bound map change retains its enclosing song despite a browser action.
    assert(board.switches.expression(0).controller == 11);
    assert(board.switches.binding_context(&resolver.document,
        unsigned(resolver.document.binding_scope(1, "Set")), unsigned(resolver.document.binding_scope(2, "One"))));
    recall("both");
    press(0); assert(board.sent.back().bytes[1] == 22); // Song overrides global A.
    press(1); assert(board.sent.back().bytes[1] == 21); // Set B.
    press(3); assert(board.sent.back().bytes[1] == 30); // Global D.
    assert(board.switches.expression(0).controller == 11);
    recall("switch");
    assert(!board.switches.expression(0).channel);
    press(0); assert(board.sent.back().bytes[1] == 22);
    recall("none"); count = board.sent.size();
    press(0); press(1); assert(board.sent.size() == count);
    assert(!board.switches.expression(0).channel);
    recall("override"); assert(board.switches.expression(0).controller == 1);
    recall("own"); press(0); assert(board.sent.back().bytes[1] == 23);
    recall("foreign");
    assert(board.switches.expression(0).controller == 11);
    board.foreign = nullptr; // The installed map owns its external source.
    press(3); assert(board.sent.back().bytes[1] == 30); // Global reference uses the library's boost.
    press(5); assert(board.sent.back().bytes[1] == 31); // Local reference uses the foreign boost.
    press(0); assert(board.sent.back().bytes[1] == 22);
    next(); assert(songs.boundary()); next(); assert(board.switches.expression(0).controller == 10); // Song Two inherits the set.
    songs.choose("Other"); next(); assert(board.switches.expression(0).controller == 74);
    songs.choose("Plain"); next(); assert(board.switches.expression(0).controller == 7);
    assert(songs.rename_song("One", "Renamed")); while (songs.busy()) songs.tick(); assert(songs.success());
    const auto *renamed = songs.song("Renamed");
    assert(renamed && renamed->foreground == original_style.foreground && renamed->background == original_style.background
        && renamed->font == original_style.font);
    assert(songs.rename_setlist("Set", "Renamed set")); while (songs.busy()) songs.tick(); assert(songs.success());
    songs.choose("Renamed set"); next(); assert(board.switches.expression(0).controller == 11);
    next(); next(); assert(board.switches.expression(0).controller == 10);
    assert(songs.undo()); while (songs.busy()) songs.tick(); assert(songs.success());
    const auto *restored = songs.song("Renamed");
    assert(restored && restored->foreground == original_style.foreground && restored->background == original_style.background && restored->font == original_style.font);
    songs.choose("Set"); next(); assert(board.switches.expression(0).controller == 11);
    assert(board.switches.binding_context());
    recall("default"); assert(board.switches.expression(0).controller == 7); // Browser has no lingering song scope.
}

int main(int argc, char **argv) {
    cntrl::memory_budget = 192 * 1024;
    assert(argc == 3 || argc == 4);
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
    Board board; cntrl::Document document;
    if (!std::strcmp(argv[2], "--scopes")) {
        assert(argc == 4);
        std::ifstream source(argv[3], std::ios::binary);
        std::vector<uint8_t> foreign_bytes{std::istreambuf_iterator<char>(source), {}};
        cntrl::Document foreign;
        assert(foreign.load(foreign_bytes.data(), unsigned(foreign_bytes.size()), board));
        board.foreign = &foreign; scoped_bindings(board, bytes); return 0;
    }
    assert(document.load(bytes.data(), unsigned(bytes.size()), board));
    if (!std::strcmp(argv[2], "--validate")) return 0;
    if (!std::strcmp(argv[2], "--reject")) {
        assert(board.entry.start(document, unsigned(document.named_patch("bad")))); board.tick();
        assert(!board.switches.open() && !board.switches.performance() && board.pins == 0); return 0;
    }
    // A held opening switch cannot trigger its newly installed action.
    board.held = 4;
    assert(board.entry.start(document, unsigned(document.named_patch("verse")))); board.tick();
    assert(board.switches.performance() && board.switches.open());
    assert(board.sent.size() == 1 && board.sent[0].bytes[2] == 30);
    assert(!std::strcmp(board.panes[1].lines[0].text, "verse"));
    assert(board.switches.expression(0).channel == 2 && board.switches.expression(0).controller == 11);
    board.button(2, false);
    // D and F are ordinary bound switches; navigation does not intercept them.
    board.button(3, true); board.button(3, false);
    assert(board.sent.back().bytes[2] == 100);
    board.button(7, true); board.button(7, false);
    assert(board.sent.back().bytes[2] == 100);
    board.button(0, true);
    assert(board.sent.back().bytes[1] == 64 && board.sent.back().bytes[2] == 127);
    board.button(1, true);
    assert(board.sent.back().bytes[1] == 10 && board.sent.back().bytes[2] == 1);
    unsigned before = unsigned(board.sent.size());
    // Switching contexts from a bound action drains momentary and gesture
    // releases before the new entry MIDI, even while those buttons stay held.
    board.button(2, true);
    assert(board.switches.performance() && !board.switches.closing());
    bool sustain_released = false, gesture_released = false;
    for (unsigned i = before; i < board.sent.size(); ++i) {
        const auto &packet = board.sent[i];
        if (packet.bytes[1] == 64 && packet.bytes[2] == 0) sustain_released = true;
        if (packet.bytes[1] == 10 && packet.bytes[2] == 2) gesture_released = true;
        if (packet.bytes[1] == 7 && packet.bytes[2] == 90) assert(sustain_released && gesture_released);
    }
    assert(sustain_released && gesture_released && board.sent.back().bytes[2] == 90);
    before = unsigned(board.sent.size());
    board.button(0, false); board.button(1, false); board.button(2, false);
    assert(board.sent.size() == before);
    // Pending double taps are cancelled when another patch takes ownership.
    board.button(4, true); board.button(4, false);
    assert(board.entry.start(document, unsigned(document.named_patch("empty")))); board.tick(400);
    assert(board.switches.open());
    assert(board.sent.size() == before);
    // A new bound map can be entered again after an intentionally empty map.
    assert(board.entry.start(document, unsigned(document.named_patch("verse")))); board.tick();
    board.button(5, true);
    assert(board.exiting && !board.switches.open() && !board.switches.performance());
    assert(board.pins == 0);
    // The normal editor and history path can still reach a program wrapped
    // by bindings, and exported derivations retain the edited volume.
    TestStorage storage; TestController controller(board);
    cntrl::Documents documents(storage, board, controller, board); documents.boot();
    auto command = [&](const std::string &line) { for (char c : line + "\n") documents.receive(uint8_t(c), 0); };
    char put[128]; std::snprintf(put, sizeof(put), "CNTRL SETUP PUT 54657374 %u %08lx 0",
        unsigned(bytes.size()), (unsigned long)cntrl::checksum(bytes.data(), unsigned(bytes.size())));
    command(put); assert(board.response == "CNTRL SEND");
    for (uint8_t b : bytes) documents.receive(b, 0);
    while (documents.busy()) documents.tick(0);
    assert(board.response == "CNTRL OK" && documents.select("Test")); documents.tick(0);
    assert(documents.edit_begin());
    assert(documents.edit_adjust(4, 3, 1) && documents.edit_value(4, 3) == 101);
    assert(documents.edit_save(0)); while (documents.busy()) documents.tick(0);
    const auto &saved = documents.document();
    std::ofstream output(argv[2], std::ios::binary);
    output.write(reinterpret_cast<const char *>(saved.data()), saved.length());
}
