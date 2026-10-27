#include <cntrl/songs_ui.h>
#include <cntrl/songs_transfer.h>
#include "storage.h"
#include <cassert>
#include <vector>

struct Board : cntrl::Hardware, cntrl::SongResolver, cntrl::SongCapture {
    unsigned sends = 0; bool missing = false, writable = true;
    bool send(const char *, const uint8_t *, unsigned) override { if (!writable) return false; ++sends; return true; }
    bool supports(const char *s) const override { return !std::strcmp(s, "din"); }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
    bool resolve(const cntrl::Song &, cntrl::SongPatch &p) override { p = patch(); return !missing; }
    bool capture(cntrl::Song &s, bool) override { s.patch = patch(); std::strcpy(s.name, "Sound"); return true; }
    void recalled() override {}
    static cntrl::SongPatch patch() { cntrl::SongPatch p; std::strcpy(p.output, "din"); return p; }
};
struct SongStorage : TestStorage {
    bool fail_reads = false;
    bool read(unsigned slot, unsigned offset, uint8_t *data, unsigned n) override {
        assert(n <= 256); if (fail_reads) return false; return TestStorage::read(slot, offset, data, n);
    }
    bool write(const uint8_t *data, unsigned n) override {
        assert(n <= 256); return TestStorage::write(data, n);
    }
};
std::vector<uint8_t> snapshot(const cntrl::Songs &songs) {
    std::vector<uint8_t> bytes(songs.length());
    for (unsigned offset = 0; offset < bytes.size(); offset += 256) {
        unsigned n = std::min(256u, unsigned(bytes.size()) - offset);
        assert(songs.read_payload(offset, bytes.data() + offset, n));
    }
    return bytes;
}
int main() {
    SongStorage storage; Board board; cntrl::Songs songs(storage, board, board);
    assert(songs.boot()); assert(songs.save_song("A", Board::patch()));
    assert(songs.save_song("B", Board::patch(), "source", "synth"));
    cntrl::Song first, second; assert(songs.song_at(0, first) && songs.song_at(1, second));
    uint32_t a = first.id, b = second.id;
    uint32_t set = songs.new_setlist(); assert(set);
    assert(songs.insert(set, 0, a)); assert(songs.insert(set, 1, b)); assert(songs.insert(set, 2, a));
    assert(board.sends == 0); songs.choose(set); assert(songs.next()); assert(board.sends == 1);
    assert(songs.insert(set, 1, a)); assert(songs.insert(set, 2, b));
    storage.fail_reads = true; assert(!songs.next()); assert(board.sends == 1);
    assert(!std::strcmp(songs.last_error(), "Setlist read failed")); storage.fail_reads = false;
    assert(songs.next_position() == 1); assert(songs.move(set, 0, 2)); assert(songs.next_position() == 3);
    assert(songs.remove(set, 2)); assert(songs.removed()); assert(songs.next_position() == 2);
    assert(songs.undo()); assert(!songs.removed()); assert(songs.next_position() == 3);
    board.missing = true; assert(!songs.next()); assert(board.sends == 1);
    board.missing = false; assert(songs.next()); assert(board.sends == 2);
    assert(songs.next()); assert(!songs.next()); assert(board.sends == 3);
    assert(songs.insert(set, songs.setlist(set)->count, b)); assert(songs.next());
    assert(songs.rename_song(b, "Z")); assert(songs.rename_setlist(set, "S"));
    uint32_t next_id = songs.counts().next_id;
    assert(songs.insert(set, 0, a)); assert(songs.undo());
    assert(songs.counts().next_id == next_id + 1);
    unsigned sent = board.sends;
    auto before = snapshot(songs); storage.remaining = 30;
    assert(!songs.remove(set, 0)); assert(before == snapshot(songs));
    storage.remaining = -1;
    cntrl::Songs reboot(storage, board, board); assert(reboot.boot());
    assert(before == snapshot(reboot)); assert(board.sends == sent);
    cntrl::SongsUi ui(reboot, board, board); ui.enter(); ui.poll(0);
    ui.button(1, true); ui.button(2, true); // Setlists.
    ui.button(1, true); ui.button(2, true); // New setlist, picker remains open.
    for (unsigned i = 0; i < 5; ++i) ui.button(2, true);
    assert(reboot.setlist(reboot.active_set())->count == 5); assert(board.sends == sent);
    ui.button(5, true); // Editing.
    assert(ui.open());
    // The former collection ceilings are not RAM dimensions any more.
    static_assert(sizeof(cntrl::Songs) < 1024);
    static_assert(sizeof(cntrl::SongsTransfer) < 128);
    for (unsigned i = 0; i < 128; ++i) assert(reboot.save_song("More", Board::patch()));
    for (unsigned i = 0; i < 20; ++i) assert(reboot.new_setlist());
    uint32_t large_set = reboot.active_set();
    for (unsigned i = 0; i < 100; ++i) assert(reboot.insert(large_set, reboot.setlist(large_set)->count, a));
    assert(reboot.counts().songs == 130 && reboot.counts().sets == 22);
    assert(reboot.setlist(large_set)->count == 105);
    assert(reboot.move(large_set, 104, 0));
    assert(reboot.remove(large_set, 104)); assert(reboot.undo());
    cntrl::Songs large_boot(storage, board, board); assert(large_boot.boot());
    assert(large_boot.counts().songs == 130 && large_boot.counts().sets == 22);
    assert(large_boot.setlist(large_set)->count == 105);
    assert(board.sends == sent);
    std::printf("songs: persistence, occurrences, undo, references and pedal picker passed; %zu bytes\n", sizeof(cntrl::Songs));
}
