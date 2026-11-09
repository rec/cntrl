#include "patch_hardware.h"
#include "storage.h"
#include <cntrl/songs.h>
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

struct SongBoard : TestPatchHardware, cntrl::SongResolver {
    cntrl::PatchExecutor invocation{*this, *this, *this, 0};
    cntrl::Songs *songs = nullptr;
    cntrl::SongContinuation context, saved;
    cntrl::Document source;
    std::vector<uint8_t> bytes;
    std::vector<unsigned> events;
    unsigned visits = 0;
    bool send(const char *, const uint8_t *message, unsigned length) override {
        events.insert(events.end(), message, message + length); return true;
    }
    bool supports(const char *output) const override { return !std::strcmp(output, "din"); }
    bool supports_font(unsigned, unsigned) const override { return true; }
    bool supports_function(unsigned function) const override { return function == 1; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t value, bool = false) override { events.push_back(value); }
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
    bool stock(uint8_t pair, cntrl::PatchExecutor &execution) override {
        assert(pair == 33); saved = context; saved.node = execution.root();
        saved.completed = execution.completed(); saved.sources_crc = execution.fingerprint();
        ++visits; return true;
    }
    bool revision(const cntrl::SongSelection &, uint32_t &) override { return false; }
    bool capture_source(const cntrl::Document *&result) override { result = &source; return source.valid(); }
    bool activate(const cntrl::SongSelection &selection, const cntrl::SongContinuation &continuation, bool resume) override {
        assert(!selection.document[0]);
        bytes.resize(songs->source_size(selection.source));
        for (unsigned offset = 0; offset < bytes.size(); offset += 256) {
            unsigned n = std::min(256u, unsigned(bytes.size()) - offset);
            if (!songs->read_source(selection.source, offset, bytes.data() + offset, n)) return false;
        }
        if (!source.load(bytes.data(), unsigned(bytes.size()), *this)) return false;
        context = continuation;
        return resume ? invocation.resume(source, selection.node, continuation.completed, continuation.sources_crc)
                      : invocation.start(source, selection.node);
    }
    cntrl::SongExecution execution_status() const override {
        using Status = cntrl::PatchExecutor::Status;
        switch (invocation.status()) {
        case Status::pending: return cntrl::SongExecution::pending;
        case Status::suspended: return cntrl::SongExecution::suspended;
        case Status::failed: return cntrl::SongExecution::failed;
        case Status::unknown: return cntrl::SongExecution::unknown;
        default: return cntrl::SongExecution::completed;
        }
    }
    void drain() {
        for (unsigned i = 0; i < 100 && invocation.status() == cntrl::PatchExecutor::Status::pending; ++i) {
            invocation.tick(); songs->tick();
        }
    }
};

int main(int argc, char **argv) {
    assert(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> payload{std::istreambuf_iterator<char>(file), {}};
    assert(!payload.empty());
    TestStorage storage; SongBoard board; cntrl::Songs songs(storage, board); board.songs = &songs;
    assert(songs.boot() && songs.import_begin(unsigned(payload.size())));
    assert(songs.import_write(payload.data(), unsigned(payload.size())));
    assert(songs.import_finish(unsigned(payload.size()), cntrl::checksum(payload.data(), unsigned(payload.size()))));
    while (songs.busy()) songs.tick();
    assert(songs.success()); songs.choose(2);
    for (unsigned volume : {100u, 80u, 100u}) {
        board.events.clear();
        assert(songs.next()); board.drain();
        assert(board.events == std::vector<unsigned>({0xc0, 6, 0xb0, 7, volume}));
    }
    assert(!songs.next());
}
