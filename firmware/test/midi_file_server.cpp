// Exercise the production named-file receiver, including failed SD operations.
#include <cntrl/midi_file_transfer.h>
#include "filesystem.h"
#include "usb_peer.h"
#include <cassert>
#include <string>

struct Replies : cntrl::Replies {
    cntrl::UsbSession usb;
    void reply(const char *text) override { usb.reply(text); }
    void data(const uint8_t *bytes, unsigned n) override { usb.data(bytes, n); }
};
struct FileSystem : TestFileSystem {
    std::string failure;
    FileSystem(const char *root, const char *mode) : TestFileSystem(root), failure(mode) {}
    bool rename(const char *from, const char *to) override {
        return !(failure == "rename" && std::strstr(from, ".cntrl-upload-")) && TestFileSystem::rename(from, to);
    }
};
struct File : TestFile {
    std::string failure;
    File(FileSystem &filesystem) : TestFile(filesystem), failure(filesystem.failure) {}
    bool write(uint32_t offset, const uint8_t *data, unsigned length) override {
        return failure != "write" && TestFile::write(offset, data, length);
    }
    bool sync() override { return failure != "sync" && TestFile::sync(); }
    bool read(uint32_t offset, uint8_t *data, unsigned length) override {
        if (failure == "read" || !TestFile::read(offset, data, length)) return false;
        if (failure == "corrupt" && length) data[0] ^= 1;
        return true;
    }
};
int main(int argc, char **argv) {
    assert(argc == 3); cntrl::memory_budget = 192 * 1024;
    FileSystem filesystem(argv[1], argv[2]); File reader(filesystem), writer(filesystem); Replies replies;
    cntrl::MidiFileTransfer transfer(filesystem, reader, writer, replies);
    std::string line;
    return usb_peer(replies.usb, [&](uint8_t byte) {
        if (byte != '\n') line += char(byte);
        else { assert(transfer.command(line.c_str(), 0)); line.clear(); }
    }, [&]() { transfer.poll(0); }, []() { return true; },
        [&]() { transfer.cancel(); line.clear(); },
        [&](cntrl::UsbKind kind, const uint8_t *bytes, unsigned n) { assert(kind == cntrl::UsbKind::midi_file); transfer.data(bytes, n, 0); });
}
