#include "patch_hardware.h"
#include "storage.h"
#include "usb_peer.h"
#include "filesystem.h"
#include <cntrl/bundle_transfer.h>
#include <cntrl/history_import.h>
#include <cntrl/file_transfer.h>
#include <cntrl/songs_transfer.h>
#include <cntrl/global_commands.h>

struct Peer : TestPatchHardware, cntrl::SongResolver, cntrl::Replies {
    cntrl::UsbSession usb;
    cntrl::GlobalCommands *globals = nullptr;
    cntrl::GlobalSettings *global_store = nullptr;
    const cntrl::GlobalSettings::Values *global_values() const override { return global_store ? &global_store->values() : nullptr; }
    bool global_setting(unsigned field, uint32_t value, bool clear) override { return globals && globals->request(field, value, clear); }
    bool reply_ready() const override { return usb.reply_ready(); }
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    bool supports(const char *) const override { return true; }
    bool supports_trs_mode(unsigned jack, cntrl::midi::TrsMode mode) const override {
        return jack < 4 && mode <= cntrl::midi::TrsMode::aux;
    }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    unsigned display_count() const override { return 3; }
    const char *control_label(cntrl::ControlRole role) const override {
        const char *labels[] = {"A", "B", "C", "D", "E", "F"}; return labels[unsigned(role)];
    }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
    bool revision(const cntrl::SongSelection &, uint32_t &crc) override { crc = 1; return true; }
    bool activate(const cntrl::SongSelection &, const cntrl::SongContinuation &, bool) override { return false; }
    cntrl::SongExecution execution_status() const override { return cntrl::SongExecution::completed; }
    bool capture_source(const cntrl::Document *&) override { return false; }
    void reply(const char *text) override { usb.reply(text); }
    void data(const uint8_t *bytes, unsigned n) override { usb.data(bytes, n); }
};

int main(int argc, char **argv) {
    Peer peer; TestStorage disk; cntrl::BundledStorage storage(disk, peer);
    assert(storage.boot()); TestController controller(peer);
    cntrl::Documents documents(storage, peer, controller, peer); documents.boot();
    cntrl::BundleTransfer transfer(storage, documents, peer, peer);
    cntrl::HistoryImport history(disk, documents, peer, peer);
    const auto root = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path(argv[0]).parent_path() / "device";
    std::filesystem::create_directories(root);
    TestFileSystem filesystem(root);
    cntrl::GlobalSettings global_settings(filesystem); assert(global_settings.boot());
    documents.bind_globals(global_settings); documents.boot();
    cntrl::GlobalCommands global_commands(global_settings, documents, peer, peer); peer.globals = &global_commands; peer.global_store = &global_settings;
    TestFile reader(filesystem), writer(filesystem);
    cntrl::FileTransfer midi(filesystem, reader, writer, peer);
    TestFile source_reader(filesystem), source_writer(filesystem);
    cntrl::FileTransfer source(filesystem, source_reader, source_writer, peer, true);
    cntrl::Songs songs(disk, peer); assert(songs.boot());
    cntrl::SongsTransfer song_transfer(songs, peer);
    char line[cntrl::usb_text_size] = {}; unsigned used = 0;
    return usb_peer(peer.usb, [&](uint8_t byte) {
        if (documents.busy()) { documents.receive(byte, 0); return; }
        if (byte == '\n') {
            line[used] = 0;
            if (!std::strcmp(line, "CNTRL MEMORY")) {
                char response[64]; std::snprintf(response, sizeof(response), "CNTRL MEMORY %u %u", peer.memory_capacity(), peer.memory_runtime());
                peer.reply(response);
            } else if (!global_commands.command(line) && !song_transfer.command(line) && !source.command(line, 0) && !midi.command(line, 0) && !history.command(line, 0) && !transfer.command(line, 0)) {
                for (unsigned i = 0; i < used; ++i) documents.receive(uint8_t(line[i]), 0);
                documents.receive('\n', 0);
            }
            used = 0;
        } else if (used + 1 < sizeof(line)) line[used++] = char(byte);
    }, [&]() { documents.tick(0); transfer.poll(0); history.tick(0); midi.poll(0); source.poll(0); songs.tick(); song_transfer.poll(0); global_commands.poll(controller.executor().busy()); },
       [&]() { return documents.accepts_input() && !song_transfer.processing(); },
       [&]() { documents.cancel_transfer(); transfer.cancel(); history.cancel(); midi.cancel(); source.cancel(); song_transfer.cancel_transfer(); },
       [&](cntrl::UsbKind kind, const uint8_t *bytes, unsigned n) {
           if (kind == cntrl::UsbKind::bundle) transfer.data(bytes, n, 0);
           else if (kind == cntrl::UsbKind::history) history.data(bytes, n, 0);
           else if (kind == cntrl::UsbKind::midi_file) midi.data(bytes, n, 0);
           else if (kind == cntrl::UsbKind::source) source.data(bytes, n, 0);
           else if (kind == cntrl::UsbKind::songs) song_transfer.data(bytes, n, 0);
           else { assert(kind == cntrl::UsbKind::setup); documents.receive_data(bytes, n, 0); }
       });
}
