#include <cntrl/songs_ui.h>
#include <cntrl/songs_transfer.h>
#include "storage.h"
#include <cassert>
#include <vector>

struct Board : cntrl::Hardware, cntrl::SongResolver, cntrl::SongCapture {
    unsigned sends = 0, switches = 0; cntrl::SongContinuation saved; bool missing = false, writable = true;
    std::string feedback;
    bool send(const char *, const uint8_t *, unsigned) override { if (!writable) return false; ++sends; return true; }
    bool stock_firmware(uint8_t pair, const cntrl::SongContinuation *continuation = nullptr) override {
        assert(pair == 33); ++switches; if (continuation) saved = *continuation; return writable;
    }
    bool supports(const char *s) const override { return !std::strcmp(s, "din"); }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned pane, const cntrl::Pane &view) override { if (!pane) feedback = view.lines[2].text; }
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
    bool revision(const cntrl::SongSelection &, uint32_t &crc) override { crc = 1; return true; }
    cntrl::SongExecution state = cntrl::SongExecution::completed;
    bool activate(const cntrl::SongSelection &selection, const cntrl::SongContinuation &continuation, bool resume) override {
        if (missing || !writable) return false;
        state = cntrl::SongExecution::completed;
        if (!resume && !std::strcmp(selection.key, "stock")) {
            saved = continuation; ++switches; state = cntrl::SongExecution::suspended;
        } else if (!resume) ++sends;
        return true;
    }
    cntrl::SongExecution execution_status() const override { return state; }
    bool capture_source(const cntrl::Document *&) override { return false; }
    bool capture(cntrl::Song &s, bool) override { static_cast<cntrl::SongSelection &>(s) = patch(); std::strcpy(s.name, "Sound"); return true; }
    void recalled() override {}
    static cntrl::SongSelection patch() { cntrl::SongSelection p; std::strcpy(p.document, "source"); std::strcpy(p.key, "sound"); return p; }

};
struct SongStorage : TestStorage {
    bool fail_reads = false;
    unsigned io_bytes = 0;
    unsigned finishes = 0;
    bool finish() override { ++finishes; return TestStorage::finish(); }
    bool read(unsigned slot, unsigned offset, uint8_t *data, unsigned n) override {
        assert(n <= 256); io_bytes += n; if (fail_reads) return false; return TestStorage::read(slot, offset, data, n);
    }
    bool write(const uint8_t *data, unsigned n) override {
        assert(n <= 256); io_bytes += n; return TestStorage::write(data, n);
    }
};
template<class T> T completed(SongStorage &storage, cntrl::Songs &songs, T accepted) {
    if (!accepted || !songs.busy()) return accepted;
    auto generation = songs.generation(); auto crc = songs.crc();
    while (songs.busy()) {
        storage.io_bytes = 0; songs.tick(); assert(storage.io_bytes <= 512);
        if (songs.busy()) assert(songs.generation() == generation && songs.crc() == crc);
    }
    return songs.success() ? accepted : T{};
}
void press(SongStorage &storage, cntrl::SongsUi &ui, cntrl::Songs &songs, unsigned button, bool down) {
    ui.button(button, down);
    completed(storage, songs, true);
    ui.poll(0);
}
std::vector<uint8_t> snapshot(const cntrl::Songs &songs) {
    std::vector<uint8_t> bytes(songs.length());
    for (unsigned offset = 0; offset < bytes.size(); offset += 256) {
        unsigned n = std::min(256u, unsigned(bytes.size()) - offset);
        assert(songs.read_payload(offset, bytes.data() + offset, n));
    }
    return bytes;
}
void test_rename_read_failure_can_be_retried() {
    SongStorage storage; Board board; cntrl::Songs songs(storage, board);
    assert(songs.boot());
    assert(completed(storage, songs, songs.save_song("A", Board::patch())));
    assert(completed(storage, songs, songs.save_song("B", Board::patch())));
    cntrl::Song a, b; assert(songs.song_at(0, a)); assert(songs.song_at(1, b));
    uint32_t set = completed(storage, songs, songs.new_setlist()); assert(completed(storage, songs, songs.insert(set, 0, b.id))); songs.choose(set);
    cntrl::SongsUi ui(songs, board, board); ui.enter(); ui.poll(0);
    press(storage, ui, songs, 1, true); press(storage, ui, songs, 1, true); press(storage, ui, songs, 2, true); // Library.
    press(storage, ui, songs, 2, true); // A's actions; rendering Next caches B.
    storage.fail_reads = true; press(storage, ui, songs, 2, true);
    assert(board.feedback == "Song read failed");
    storage.fail_reads = false; press(storage, ui, songs, 2, true); // Retry Rename.
    press(storage, ui, songs, 1, true); press(storage, ui, songs, 2, true); // A becomes B, then save.
    assert(songs.song_at(0, a) && !std::strcmp(a.name, "B"));
    assert(board.sends == 0);
}
void test_abandoned_imports_close_without_changing_library() {
    struct Replies : cntrl::Replies { void reply(const char *) override {} } replies;
    SongStorage storage; Board board; cntrl::Songs songs(storage, board);
    assert(songs.boot()); auto before = snapshot(songs);
    cntrl::SongsTransfer transfer(songs, replies);
    auto begin = [&]() { assert(transfer.command("CNTRL SONGS BEGIN 16 0 1", 1)); assert(transfer.busy()); };
    begin(); unsigned finishes = storage.finishes;
    transfer.poll(10002); assert(!transfer.busy() && storage.finishes == ++finishes);
    begin(); transfer.command("CNTRL SONGS DATA xx", 2);
    assert(!transfer.busy() && storage.finishes == ++finishes);
    begin(); transfer.command("CNTRL SONGS END", 2);
    assert(!transfer.busy() && storage.finishes == ++finishes);
    begin(); begin(); assert(storage.finishes == ++finishes);
    storage.remaining = 0; transfer.command("CNTRL SONGS DATA 00", 2);
    assert(!transfer.busy() && storage.finishes == ++finishes);
    assert(snapshot(songs) == before);
}
void test_import_progress_and_failure_keep_the_active_generation() {
    struct Replies : cntrl::Replies {
        unsigned count = 0, waits = 0; std::string last;
        void reply(const char *text) override { ++count; waits += !std::strcmp(text, "CNTRL WAIT"); last = text; }
    } replies;
    SongStorage storage; Board board; cntrl::Songs songs(storage, board);
    assert(songs.boot());
    assert(completed(storage, songs, songs.save_song("A", Board::patch())));
    assert(completed(storage, songs, songs.save_song("B", Board::patch())));
    auto data = snapshot(songs);
    cntrl::SongsTransfer transfer(songs, replies);
    for (unsigned failure = 0; failure < 3; ++failure) {
        auto bytes = data;
        if (failure == 2) {
            cntrl::Song first, second; std::memcpy(&first, bytes.data() + 12, sizeof(first));
            std::memcpy(&second, bytes.data() + 12 + sizeof(first), sizeof(second));
            second.id = first.id; std::memcpy(bytes.data() + 12 + sizeof(first), &second, sizeof(second));
        }
        uint32_t crc = cntrl::checksum(bytes.data(), unsigned(bytes.size()));
        if (failure == 1) crc ^= 1;
        char line[280]; std::snprintf(line, sizeof(line), "CNTRL SONGS BEGIN %u %08lx 1", unsigned(bytes.size()), (unsigned long)crc);
        assert(transfer.command(line));
        for (unsigned offset = 0; offset < bytes.size(); offset += 128) {
            std::strcpy(line, "CNTRL SONGS DATA ");
            unsigned n = std::min(128u, unsigned(bytes.size()) - offset);
            for (unsigned i = 0; i < n; ++i) std::snprintf(line + 17 + i * 2, 3, "%02x", bytes[offset + i]);
            assert(transfer.command(line));
        }
        auto replies_before = replies.count, generation = songs.generation(), active_crc = songs.crc();
        assert(transfer.command("CNTRL SONGS END"));
        assert(transfer.busy() && songs.busy() && replies.count == replies_before);
        unsigned tick = 0;
        while (songs.busy()) {
            storage.io_bytes = 0; songs.tick(); assert(storage.io_bytes <= 512);
            if (songs.busy()) assert(songs.generation() == generation && songs.crc() == active_crc);
            transfer.poll(++tick * 1000);
        }
        assert(!transfer.busy() && replies.waits);
        assert(replies.last == (failure ? "CNTRL ERROR songs" : "CNTRL OK"));
        assert(snapshot(songs) == data && board.sends == 0);
    }
}
int main() {
    test_import_progress_and_failure_keep_the_active_generation();
    test_abandoned_imports_close_without_changing_library();
    test_rename_read_failure_can_be_retried();
    SongStorage storage; Board board; cntrl::Songs songs(storage, board);
    assert(songs.boot()); assert(completed(storage, songs, songs.save_song("A", Board::patch())));
    assert(completed(storage, songs, songs.save_song("B", Board::patch())));
    cntrl::Song first, second; assert(songs.song_at(0, first) && songs.song_at(1, second));
    uint32_t a = first.id, b = second.id;
    uint32_t set = completed(storage, songs, songs.new_setlist()); assert(set);
    assert(completed(storage, songs, songs.insert(set, 0, a))); assert(completed(storage, songs, songs.insert(set, 1, b))); assert(completed(storage, songs, songs.insert(set, 2, a)));
    assert(board.sends == 0); songs.choose(set); assert(songs.next()); assert(board.sends == 1);
    assert(completed(storage, songs, songs.insert(set, 1, a))); assert(completed(storage, songs, songs.insert(set, 2, b)));
    storage.fail_reads = true; assert(!songs.next()); assert(board.sends == 1);
    assert(!std::strcmp(songs.last_error(), "Setlist read failed")); storage.fail_reads = false;
    assert(songs.next_position() == 1); assert(completed(storage, songs, songs.move(set, 0, 2))); assert(songs.next_position() == 3);
    assert(completed(storage, songs, songs.remove(set, 2))); assert(songs.removed()); assert(songs.next_position() == 2);
    assert(completed(storage, songs, songs.undo())); assert(!songs.removed()); assert(songs.next_position() == 3);
    board.missing = true; assert(!songs.next()); assert(board.sends == 1);
    board.missing = false; assert(songs.next()); assert(board.sends == 2);
    assert(songs.next()); assert(!songs.next()); assert(board.sends == 3);
    assert(completed(storage, songs, songs.insert(set, songs.setlist(set)->count, b))); assert(songs.next());
    assert(completed(storage, songs, songs.rename_song(b, "Z"))); assert(completed(storage, songs, songs.rename_setlist(set, "S")));
    uint32_t next_id = songs.counts().next_id;
    assert(completed(storage, songs, songs.insert(set, 0, a))); assert(completed(storage, songs, songs.undo()));
    assert(songs.counts().next_id == next_id + 1);
    unsigned sent = board.sends;
    auto before = snapshot(songs); storage.remaining = 30;
    assert(!completed(storage, songs, songs.remove(set, 0))); assert(before == snapshot(songs));
    storage.remaining = -1;
    cntrl::Songs reboot(storage, board); assert(reboot.boot());
    assert(before == snapshot(reboot)); assert(board.sends == sent);
    cntrl::SongsUi ui(reboot, board, board); ui.enter(); ui.poll(0);
    press(storage, ui, reboot, 1, true); press(storage, ui, reboot, 2, true); // Setlists.
    press(storage, ui, reboot, 1, true); press(storage, ui, reboot, 2, true); // New setlist, picker remains open.
    for (unsigned i = 0; i < 5; ++i) press(storage, ui, reboot, 2, true);
    assert(reboot.setlist(reboot.active_set())->count == 5); assert(board.sends == sent);
    press(storage, ui, reboot, 5, true); // Editing.
    assert(ui.open());
    // The former collection ceilings are not RAM dimensions any more.
    static_assert(sizeof(cntrl::Songs) < 4096);
    static_assert(sizeof(cntrl::SongsTransfer) < 128);
    for (unsigned i = 0; i < 128; ++i) assert(completed(storage, reboot, reboot.save_song("More", Board::patch())));
    for (unsigned i = 0; i < 20; ++i) assert(completed(storage, reboot, reboot.new_setlist()));
    uint32_t large_set = reboot.active_set();
    for (unsigned i = 0; i < 100; ++i) assert(completed(storage, reboot, reboot.insert(large_set, reboot.setlist(large_set)->count, a)));
    assert(reboot.counts().songs == 130 && reboot.counts().sets == 22);
    assert(reboot.setlist(large_set)->count == 105);
    assert(completed(storage, reboot, reboot.move(large_set, 104, 0)));
    assert(completed(storage, reboot, reboot.remove(large_set, 104))); assert(completed(storage, reboot, reboot.undo()));
    cntrl::Songs large_boot(storage, board); assert(large_boot.boot());
    assert(large_boot.counts().songs == 130 && large_boot.counts().sets == 22);
    assert(large_boot.setlist(large_set)->count == 105);
    assert(board.sends == sent);
    // A stock visit advances within the same song, never to the next song.
    SongStorage visits; Board target; cntrl::Songs sequence(visits, target);
    assert(sequence.boot() && completed(visits, sequence, sequence.save_song("Sequence", Board::patch())));
    cntrl::Song item; assert(sequence.song_at(0, item));
    cntrl::SongSelection stock = Board::patch(); std::strcpy(stock.key, "stock");
    assert(completed(visits, sequence, sequence.insert_patch(item.id, 0, stock)));
    cntrl::SongSelection midi = Board::patch();
    assert(completed(visits, sequence, sequence.insert_patch(item.id, 1, midi)));
    assert(sequence.song(item.id)->count == 3);
    uint32_t list = completed(visits, sequence, sequence.new_setlist()); assert(list && completed(visits, sequence, sequence.insert(list, 0, item.id))); sequence.choose(list);
    assert(sequence.next() && target.sends == 1);
    assert(sequence.next() && target.switches == 1 && target.sends == 1);
    cntrl::Songs returned(visits, target); assert(returned.boot());
    assert(target.sends == 1); assert(returned.resume(target.saved)); assert(target.sends == 2);
    assert(!returned.next() && !std::strcmp(returned.last_error(), "End of set"));
    assert(completed(visits, returned, returned.move_patch(item.id, 1, true)));
    cntrl::SongSelection check; assert(returned.patch_at(item.id, 0, check) && !std::strcmp(check.key, "stock"));
    assert(completed(visits, returned, returned.move_patch(item.id, 0, false)));
    assert(completed(visits, returned, returned.remove_patch(item.id, 2)));
    assert(!returned.resume(target.saved)); // A changed library cannot auto-recall.
    returned.choose(list); assert(returned.next()); assert(returned.next());
    unsigned sends = target.sends;
    cntrl::Songs final_return(visits, target); assert(final_return.boot());
    assert(final_return.resume(target.saved) && target.sends == sends);
    assert(!std::strcmp(final_return.last_error(), "End of song"));
    assert(completed(visits, final_return, final_return.remove_patch(item.id, 0)));
    assert(final_return.patch_at(item.id, 0, check) && !std::strcmp(check.key, "stock"));
    assert(!completed(visits, final_return, final_return.remove_patch(item.id, 0)));
    // A failed asynchronous entry restores the previous performance cursor.
    final_return.choose(list);
    assert(final_return.next());
    final_return.cancel_function();
    assert(!final_return.current_name()[0]);
    // Incompatible/corrupt generations can only be replaced by an explicit import.
    target.state = cntrl::SongExecution::completed;
    SongStorage invalid; invalid.files[""][0] = {'B', 'A', 'D'};
    cntrl::Songs repair(invalid, target);
    assert(!repair.boot() && repair.invalid());
    assert(!completed(invalid, repair, repair.save_song("Do not overwrite", Board::patch())));
    auto replacement = snapshot(final_return);
    assert(repair.import_begin(unsigned(replacement.size())));
    for (unsigned offset = 0; offset < replacement.size(); offset += 128)
        assert(repair.import_write(replacement.data() + offset, std::min(128u, unsigned(replacement.size()) - offset)));
    assert(completed(invalid, repair, repair.import_finish(unsigned(replacement.size()), cntrl::checksum(replacement.data(), unsigned(replacement.size())))));
    assert(!repair.invalid() && snapshot(repair) == replacement);
    std::printf("songs: persistence, occurrences, undo, references and pedal picker passed; %zu bytes\n", sizeof(cntrl::Songs));
}
