// Production USB/file-transfer logic against a host temporary filesystem.
#include <cntrl/disk_speed.h>
#include "filesystem.h"
#include "usb_peer.h"
#include <cassert>
#include <string>

struct Replies : cntrl::Replies {
    cntrl::UsbSession usb;
    void reply(const char *text) override { usb.reply(text); }
    void data(const uint8_t *bytes, unsigned n) override { usb.data(bytes, n); }
};
struct File : TestFile {
    const std::string failure;
    bool synced = false;
    File(TestFileSystem &filesystem, const char *mode) : TestFile(filesystem), failure(mode) {}
    bool open(const char *path, cntrl::files::Mode mode) override {
        if (mode == cntrl::files::Mode::read) assert(synced);
        return TestFile::open(path, mode);
    }
    bool write(uint32_t offset, const uint8_t *data, unsigned length) override {
        return failure != "write" && TestFile::write(offset, data, length);
    }
    bool sync() override { synced = true; return failure != "sync" && TestFile::sync(); }
    bool read(uint32_t offset, uint8_t *data, unsigned length) override {
        if (failure == "read" || !TestFile::read(offset, data, length)) return false;
        if (failure == "corrupt" && length) data[0] ^= 1;
        return true;
    }
};
int main(int argc, char **argv) {
    assert(argc == 3); cntrl::memory_budget = 192 * 1024;
    TestFileSystem filesystem(argv[1]); File file(filesystem, argv[2]); Replies replies;
    cntrl::DiskSpeedTransfer transfer(filesystem, file, replies);
    std::string line;
    return usb_peer(replies.usb, [&](uint8_t byte) {
        if (byte != '\n') line += char(byte);
        else { assert(transfer.command(line.c_str(), 0)); line.clear(); }
    }, [&]() { transfer.poll(0); }, []() { return true; },
        [&]() { transfer.cancel(); line.clear(); },
        [&](cntrl::UsbKind kind, const uint8_t *bytes, unsigned n) { assert(kind == cntrl::UsbKind::disk); transfer.data(bytes, n, 0); });
}
