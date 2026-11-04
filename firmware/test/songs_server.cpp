#include <cntrl/songs_transfer.h>
#include "storage.h"
#include "usb_peer.h"
#include <iostream>
#include <string>
struct Board : cntrl::Hardware, cntrl::SongResolver, cntrl::Replies {
    cntrl::UsbSession usb;
    bool reply_ready() const override { return usb.reply_ready(); }
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
    bool revision(const cntrl::SongSelection &, uint32_t &crc) override { crc = 1; return true; }
    bool activate(const cntrl::SongSelection &, const cntrl::SongContinuation &, bool) override { return false; }
    cntrl::SongExecution execution_status() const override { return cntrl::SongExecution::completed; }
    bool capture_source(const cntrl::Document *&) override { return false; }
    void reply(const char *line) override { usb.reply(line); }
};
int main() {
    Board board; TestStorage storage; cntrl::Songs songs(storage, board);
    if (!songs.boot()) return 1;
    cntrl::SongsTransfer transfer(songs, board);
    std::string line;
    return usb_peer(board.usb, [&](uint8_t b) {
        if (b != '\n') line += char(b);
        else { transfer.command(line.c_str()); line.clear(); }
    }, [&]() { songs.tick(); transfer.poll(0); }, [&]() { return !transfer.processing(); },
        [&]() { transfer.cancel_transfer(); line.clear(); });
}
