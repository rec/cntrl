#include <cntrl/songs_transfer.h>
#include "storage.h"
#include <iostream>
#include <string>
struct Board : cntrl::Hardware, cntrl::SongResolver, cntrl::Replies {
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
    bool resolve(const cntrl::SongSelection &, cntrl::SongPatch &) override { return false; }
    void reply(const char *line) override { std::cout << line << std::endl; }
};
int main() {
    Board board; TestStorage storage; cntrl::Songs songs(storage, board, board);
    if (!songs.boot()) return 1;
    cntrl::SongsTransfer transfer(songs, board);
    std::string line;
    while (std::getline(std::cin, line)) transfer.command(line.c_str());
}
