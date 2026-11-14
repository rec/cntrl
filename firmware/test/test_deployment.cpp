#include <cntrl/deployment.h>
#include <cntrl/state_transfer.h>
#include <cntrl/setup_names.h>
#include "filesystem.h"
#include <cassert>
#include <string>
#include <vector>

struct Faults : TestFileSystem {
    using TestFileSystem::TestFileSystem;
    int remaining = -1;
    bool sync_ok = true;
    bool hide_songs = false;
    bool exists(const char *path) override {
        return !(hide_songs && !std::strncmp(path, "/cntrl/songs/", 13)) && TestFileSystem::exists(path);
    }
    struct Handle : TestFile {
        Faults &owner;
        bool activation = false;
        explicit Handle(Faults &disk) : TestFile(disk), owner(disk) {}
        bool open(const char *path, cntrl::files::Mode mode) override {
            activation = std::strstr(path, "/deploy/") != nullptr;
            return TestFile::open(path, mode);
        }
        bool write(uint32_t offset, const uint8_t *data, unsigned n) override {
            if (!activation) return TestFile::write(offset, data, n);
            unsigned amount = owner.remaining < 0 ? n : std::min(n, unsigned(owner.remaining));
            if (amount && !TestFile::write(offset, data, amount)) return false;
            if (owner.remaining >= 0) owner.remaining -= int(amount);
            return amount == n;
        }
        bool sync() override { return TestFile::sync() && (!activation || owner.sync_ok); }
    };
    std::unique_ptr<cntrl::files::File> file() override { return std::unique_ptr<cntrl::files::File>(new (std::nothrow) Handle(*this)); }
};

int main(int argc, char **argv) {
    assert(argc == 2); cntrl::memory_budget = 1024 * 1024;
    Faults disk(argv[1]); cntrl::Deployment initial(disk);
    assert(initial.boot() && initial.root() == 0 && !initial.pending());
    assert(disk.make_directory("/cntrl/songs"));
    auto library = disk.file(); assert(library->open("/cntrl/songs/0.bin", cntrl::files::Mode::truncate));
    const uint8_t content[] = {'l', 'i', 'b'};
    assert(library->write(0, content, sizeof(content)) && library->sync()); library->close();
    assert(initial.prepare_runtime(true));
    assert(!initial.stores_complete(true, false));
    disk.hide_songs = true;
    cntrl::Deployment missing_library(disk); assert(!missing_library.boot() && !missing_library.ready());
    disk.hide_songs = false;
    assert(initial.begin(1));
    char path[224];
    assert(cntrl::persistent_path("/cntrl/setups/bundle/0.bin", path));
    assert(!std::strcmp(path, "/cntrl/generations/00000001/setups/bundle/0.bin"));
    for (const char *shared : {"/cntrl/midi/255-3.mid", "/cntrl/starts.toml", "/cntrl/fonts/123.cfont", "/cntrl/movies/123.bin"}) {
        assert(cntrl::persistent_path(shared, path) && !std::strcmp(path, shared));
    }
    // Losing power during staging must select the committed legacy root.
    cntrl::Deployment before_commit(disk);
    assert(before_commit.boot() && before_commit.root() == 0);
    assert(!before_commit.recover(1)); // An abandoned stage is not a recovery root.
    assert(before_commit.begin(2) && before_commit.commit() && before_commit.root() == 2);
    assert(!before_commit.begin(2)); // A retained generation is never overwritten.
    // Interrupt each byte of the inactive activation record.
    for (unsigned length = 0; length < 24; ++length) {
        cntrl::Deployment staged(disk); assert(staged.boot() && staged.root() == 2);
        assert(staged.begin(100 + length)); disk.remaining = int(length);
        assert(!staged.commit()); disk.remaining = -1;
        cntrl::Deployment reboot(disk); assert(reboot.boot() && reboot.root() == 2);
    }
    cntrl::Deployment failed_sync(disk); assert(failed_sync.boot());
    assert(failed_sync.begin(200)); disk.sync_ok = false;
    assert(!failed_sync.commit()); disk.sync_ok = true;
    cntrl::Deployment uncertain(disk);
    // A complete record may survive a failed sync acknowledgement. Either
    // complete generation is safe; never resume the partially staged one.
    assert(uncertain.boot() && (uncertain.root() == 2 || uncertain.root() == 200));
    assert(uncertain.begin(201)); uncertain.abort();
    assert(!uncertain.pending() && cntrl::persistent_root == uncertain.root());
    {
        auto raw = disk.file(); assert(raw->open("/cntrl/opaque.bin", cntrl::files::Mode::truncate));
        const uint8_t bytes[] = {'N', 'E', 'W', '!', 255};
        assert(raw->write(0, bytes, sizeof(bytes)) && raw->sync()); raw->close();
        struct Replies {
            std::string text; std::vector<uint8_t> bytes;
            void reply(const char *value) { text = value; }
            void data(const uint8_t *data, unsigned n) { bytes.assign(data, data + n); }
        } replies;
        cntrl::StateTransfer transfer(disk); char hex[128], command[192];
        cntrl::hex_text("/cntrl/opaque.bin", hex);
        std::snprintf(command, sizeof(command), "CNTRL STATE SIZE %s", hex);
        assert(transfer.command(command, replies) && replies.text == "CNTRL STATE 5");
        std::snprintf(command, sizeof(command), "CNTRL STATE READ %s 0", hex);
        assert(transfer.command(command, replies) && replies.bytes == std::vector<uint8_t>(bytes, bytes + 5));
        cntrl::hex_text("/cntrl/../private", hex);
        std::snprintf(command, sizeof(command), "CNTRL STATE SIZE %s", hex);
        assert(transfer.command(command, replies) && replies.text == "CNTRL ERROR state-path");
    }
    // An unsupported committed format requires recovery, rather than guessing.
    auto file = disk.file();
    assert(file->open("/cntrl/deploy/0.bin", cntrl::files::Mode::read));
    uint8_t record[24]; assert(file->read(0, record, 24)); file->close();
    cntrl::binary::write32(record + 4, 0xfffffffeu); record[12] = 3; record[13] = 0;
    cntrl::binary::write32(record + 16, cntrl::binary::crc(record, 16));
    assert(file->open("/cntrl/deploy/0.bin", cntrl::files::Mode::truncate));
    assert(file->write(0, record, 24) && file->sync()); file->close();
    cntrl::Deployment unsupported(disk); assert(!unsupported.boot() && !unsupported.ready());
    assert(unsupported.begin(300) && unsupported.pending());
    cntrl::Deployment interrupted_recovery(disk);
    assert(!interrupted_recovery.boot() && !interrupted_recovery.ready());
    unsupported.abort(); assert(!unsupported.ready());
    assert(unsupported.recover(2) && unsupported.pending() && unsupported.commit());
    cntrl::Deployment repaired(disk); assert(repaired.boot() && repaired.root() == 2);
    assert(!repaired.prepare_runtime(false) && !repaired.ready());
}
