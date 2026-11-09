#include "patch_hardware.h"
#include <cntrl/midi_processing.h>
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

struct Board : TestPatchHardware, cntrl::midi::Sink {
    struct Sent { unsigned output; cntrl::midi::Packet packet; };
    cntrl::midi::Streams streams;
    cntrl::PatchExecutor entry{*this, *this, *this, 0};
    std::vector<Sent> sent;
    unsigned switch_capacity() const override { return 5; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    bool supports_trs_mode(unsigned, cntrl::midi::TrsMode) const override { return true; }
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    bool write_packet(unsigned output, const cntrl::midi::Packet &packet) override {
        sent.push_back({output, packet}); return true;
    }
    bool select_midi_streams(const cntrl::Document &document, unsigned root) override {
        streams.select(document.stream_scope(root)); return true;
    }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override { assert(false); }
    void select(const cntrl::Document &document, const char *name) {
        int node = document.named_patch(name); assert(node >= 0);
        assert(entry.start(document, unsigned(node), false, true));
        for (unsigned i = 0; i < 32 && entry.busy(); ++i) entry.tick(i);
        assert(entry.status() == cntrl::PatchExecutor::Status::completed);
        streams.service(*this, 100);
    }
    void note(unsigned pitch, bool down) {
        streams.receive(0, {uint8_t(down ? 9 : 8), {uint8_t(down ? 0x90 : 0x80), uint8_t(pitch), uint8_t(down ? 100 : 0)}}, 100);
        streams.service(*this, 100);
    }
    void check(unsigned index, unsigned output, unsigned kind, unsigned pitch) const {
        assert(index < sent.size());
        assert(sent[index].output == output);
        assert((sent[index].packet.bytes[0] >> 4) == kind);
        assert(sent[index].packet.bytes[1] == pitch);
    }
};

int main(int argc, char **argv) {
    assert(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
    Board board; cntrl::Document document;
    assert(document.load(bytes.data(), unsigned(bytes.size()), board));
    assert(board.streams.load(document.midi_streams(), document.midi_streams_size(), test_midi_outputs, document.nodes()));
    for (unsigned i = 0; i < test_midi_outputs.route_count(); ++i) {
        board.streams.connection(i, true, 1);
        board.streams.output_connection(i, true, 1);
    }
    assert(document.stream_scope(unsigned(document.named_patch("alias"))) == document.stream_scope(unsigned(document.named_patch("base"))));
    board.select(document, "base");
    board.note(60, true);
    assert(board.sent.size() == 2);
    board.check(0, 1, 9, 60); board.check(1, 8, 9, 72);

    // A one-shot boost action leaves the selected route context running.
    const unsigned selected = board.streams.selected();
    assert(board.entry.start(document, unsigned(document.named_patch("boost"))));
    for (unsigned i = 0; i < 32 && board.entry.busy(); ++i) board.entry.tick(i);
    assert(board.entry.status() == cntrl::PatchExecutor::Status::completed);
    assert(board.streams.selected() == selected && board.sent.size() == 2);

    // Reselecting the same patch keeps sounding voices alive.
    board.select(document, "base"); assert(board.sent.size() == 2);
    board.select(document, "variation");
    assert(board.sent.size() == 3); board.check(2, 8, 8, 72);
    board.note(60, false);
    assert(board.sent.size() == 4); board.check(3, 1, 8, 60);

    board.note(62, true);
    assert(board.sent.size() == 6);
    board.check(4, 1, 9, 62); board.check(5, 8, 9, 69);
    board.select(document, "alias");
    assert(board.sent.size() == 7); board.check(6, 8, 8, 69);
    board.note(62, false);
    assert(board.sent.size() == 8); board.check(7, 1, 8, 62);
    board.note(64, true);
    assert(board.sent.size() == 10);
    board.check(8, 1, 9, 64); board.check(9, 8, 9, 76);
    board.select(document, "clear");
    assert(board.sent.size() == 11); board.check(10, 8, 8, 76);
    board.note(64, false);
    assert(board.sent.size() == 12); board.check(11, 1, 8, 64);
    assert(board.streams.voices() == 0 && board.streams.pending() == 0 && board.streams.recovery() == 0);

    // Selecting an ordinary patch explicitly retires the old patch routes.
    board.select(document, "base"); board.note(65, true);
    assert(board.sent.size() == 14);
    board.check(12, 1, 9, 65); board.check(13, 8, 9, 77);
    board.select(document, "boost");
    assert(board.sent.size() == 15); board.check(14, 8, 8, 77);
    board.note(65, false);
    assert(board.sent.size() == 16); board.check(15, 1, 8, 65);
}
