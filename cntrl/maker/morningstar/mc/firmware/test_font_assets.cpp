#include "patch_switcher/font_storage.h"
#include <cntrl/documents.h>
#include "storage.h"
#include <cassert>
#include <iostream>
#include <string>

struct Peer : cntrl::Hardware, cntrl::Replies, TestStorage {
    font_assets::Files fonts;
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    bool supports(const char *name) const override { return !std::strcmp(name, "din") || !std::strcmp(name, "trs"); }
    bool supports_font(unsigned face, unsigned size) const override { return face == 0 && size >= 8 && size <= 96 && size % 8 == 0; }
    bool supports_font_asset(uint32_t key, unsigned size) const override { return fonts.supports(key, size); }
    void begin_fonts() override { font_assets::cache.begin(); }
    bool stage_font(uint32_t key, unsigned size) override { return fonts.stage(key, size); }
    void finish_fonts(bool commit) override { font_assets::cache.finish(commit); }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned, const cntrl::Pane &pane) override {
        if (pane.graphics.asset) assert(font_assets::cache.get(pane.graphics.asset, pane.graphics.size));
        for (const auto &line : pane.lines) if (line.text[0] && line.graphics.asset)
            assert(font_assets::cache.get(line.graphics.asset, line.graphics.size));
    }
    void set_brightness(uint8_t, bool) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
    void reply(const char *text) override { std::cout << text << std::endl; }
};
int main(int argc, char **argv) {
    if (argc != 2) return 1;
    SD.sdfs.root = argv[1]; Peer peer; peer.fonts.ready = true;
    cntrl::Controller controller(peer);
    cntrl::Documents documents(peer, peer, controller, peer); documents.boot();
    std::string line; char byte;
    while (std::cin.get(byte)) {
        if (documents.busy()) { documents.receive(uint8_t(byte), 0); }
        else if (byte != '\n') { if (byte != '\r') line += byte; }
        else {
            unsigned long key; unsigned size;
            if (peer.fonts.command(line.c_str(), peer)) {}
            else if (std::sscanf(line.c_str(), "STAGE %lx %u", &key, &size) == 2) {
                font_assets::cache.begin();
                bool good = peer.fonts.supports(uint32_t(key), size) && peer.fonts.stage(uint32_t(key), size);
                font_assets::cache.finish(good);
                peer.reply(good ? "CNTRL OK" : "CNTRL ERROR font-load");
            } else if (std::sscanf(line.c_str(), "ACTIVE %lx %u", &key, &size) == 2) {
                peer.reply(font_assets::cache.get(uint32_t(key), size) ? "CNTRL OK" : "CNTRL ERROR absent");
            } else {
                for (char c : line) documents.receive(uint8_t(c), 0);
                documents.receive('\n', 0);
            }
            line.clear();
        }
        for (unsigned i = 0; i < 129; ++i) documents.tick(0);
    }
}
