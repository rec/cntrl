#include <cntrl/history_import.h>
#include "patch_hardware.h"
#include "storage.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

struct Board : TestPatchHardware, cntrl::Replies {
    std::string last;
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
    void reply(const char *text) override { last = text; }
    void data(const uint8_t *, unsigned) override { assert(false); }
};
void upload(cntrl::HistoryImport &transfer, Board &board, uint64_t number, const std::vector<uint8_t> &record) {
    char revision[21], command[320]; cntrl::revision_text(number, revision);
    std::snprintf(command, sizeof(command), "CNTRL HISTORY IMPORT 4576656e696e67 %s %u %08lx", revision,
        unsigned(record.size()), (unsigned long)cntrl::checksum(record.data(), unsigned(record.size())));
    assert(transfer.command(command, 0) && board.last == "CNTRL SEND");
    for (unsigned offset = 0; offset < record.size();) {
        unsigned n = std::min(128u, unsigned(record.size()) - offset);
        assert(transfer.data(record.data() + offset, n, 0) && board.last == "CNTRL OK"); offset += n;
    }
    assert(transfer.command("CNTRL HISTORY END", 0));
    for (unsigned now = 0; transfer.busy() && now < 10000; ++now) transfer.tick(now);
    assert(!transfer.busy());
}
void read(cntrl::Revisions &revisions, cntrl::MemoryBuffer &body, uint64_t number) {
    revisions.read("Evening", number, body);
    for (unsigned i = 0; revisions.busy() && i < 10000; ++i) revisions.tick();
    assert(!revisions.busy() && revisions.success());
}
int main(int argc, char **argv) {
    assert(argc == 2); std::ifstream input(argv[1], std::ios::binary);
    std::vector<uint8_t> record(std::istreambuf_iterator<char>(input), {});
    Board board; TestStorage disk; TestController controller(board);
    cntrl::Documents documents(disk, board, controller, board); documents.boot();
    cntrl::HistoryImport transfer(disk, documents, board, board);
    disk.choose_revision("Evening", 42); auto first = disk.directory;
    disk.files[first][0] = record;
    assert(transfer.command("CNTRL HISTORY RESERVE 4576656e696e67 99", 0) && board.last == "CNTRL OK");
    upload(transfer, board, 42, record);
    assert(board.last == "CNTRL OK" && disk.files[first][0] == record && disk.files[first][1] == record);
    upload(transfer, board, 42, record); assert(board.last == "CNTRL EXISTS");
    disk.files[first][1][100] ^= 1; // Same size and DONE trailer, but invalid payload CRC.
    upload(transfer, board, 42, record);
    assert(board.last == "CNTRL OK" && disk.files[first][0] == record && disk.files[first][1] == record);
    auto conflict = record; conflict[12] ^= 1;
    cntrl::put32(conflict.data() + 84, cntrl::checksum(conflict.data(), 84));
    upload(transfer, board, 42, conflict);
    assert(board.last == "CNTRL ERROR history-import-conflict" && disk.files[first][1] == record);
    disk.choose_revision("Evening", 43); auto second = disk.directory;
    disk.files[second][0] = record; disk.remaining = 100;
    upload(transfer, board, 43, record);
    assert(board.last == "CNTRL ERROR history-import-write" && disk.files[second][0] == record);
    cntrl::Revisions revisions(disk); cntrl::MemoryBuffer body;
    read(revisions, body, 43); // An incomplete slot 1 falls back to unchanged slot 0.
    disk.remaining = -1; upload(transfer, board, 43, record);
    assert(board.last == "CNTRL OK" && disk.files[second][0] == record);
    auto &converted = disk.files[second][1]; unsigned at = 88 + cntrl::u32(converted.data() + 16) * 64;
    converted[at + 4] ^= 1;
    cntrl::put32(converted.data() + 8, cntrl::checksum(converted.data() + 88, unsigned(converted.size()) - 92));
    cntrl::put32(converted.data() + 84, cntrl::checksum(converted.data(), 84));
    read(revisions, body, 43); // A CRC-valid unsupported record also falls back.
    disk.choose_revision("Evening", 43); assert(disk.files[second][0] == record);
    revisions.append("Evening", "Next", 123, body, body.size);
    for (unsigned i = 0; revisions.busy() && i < 10000; ++i) revisions.tick();
    assert(revisions.success() && revisions.info().number == 100);
    revisions.erase("Evening", 42);
    for (unsigned i = 0; revisions.busy() && i < 10000; ++i) revisions.tick();
    assert(revisions.success() && disk.files.find(first) == disk.files.end() && disk.revisions["Evening"].count(42));
}
