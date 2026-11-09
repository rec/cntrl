#include "patch_hardware.h"
#include <cntrl/ui/font_storage.h>
#include "filesystem.h"
#include <cntrl/documents.h>
#include "storage.h"
#include "usb_peer.h"
#include <cassert>
#include <iostream>
#include <string>

struct Peer : TestPatchHardware, cntrl::Replies, TestStorage {
    TestFileSystem filesystem;
    TestFile reader, writer;
    font_assets::Storage fonts;
    explicit Peer(const char *root) : filesystem(root), reader(filesystem), writer(filesystem), fonts(filesystem, reader, writer) {}
    cntrl::UsbSession usb;
    bool reply_ready() const override { return usb.reply_ready(); }
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    bool supports(const char *name) const override { return !std::strcmp(name, "din") || !std::strcmp(name, "trs"); }
    bool supports_function(unsigned code) const override { return code == 1; }
    bool supports_font(unsigned face, unsigned size) const override { return face == 0 && size >= 8 && size <= 96 && size % 8 == 0; }
    bool supports_font_asset(uint32_t key, unsigned size) const override { return fonts.supports(key, size); }
    void begin_fonts() override { font_assets::cache.begin(); }
    bool stage_font(uint32_t key, unsigned size) override { return fonts.stage(key, size); }
    void finish_fonts(bool commit) override { font_assets::cache.finish(commit); }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    unsigned display_count() const override { return 3; }
    const char *control_label(cntrl::ControlRole role) const override {
        const char *labels[] = {"A", "B", "C", "D", "E", "F"}; return labels[unsigned(role)];
    }
    void show(unsigned, const cntrl::Pane &pane) override {
        if (pane.graphics.asset) assert(font_assets::cache.get(pane.graphics.asset, pane.graphics.size));
        for (const auto &line : pane.lines) if (line.text[0] && line.graphics.asset)
            assert(font_assets::cache.get(line.graphics.asset, line.graphics.size));
    }
    void set_brightness(uint8_t, bool) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
    void reply(const char *text) override { usb.reply(text); }
    void data(const uint8_t *bytes, unsigned n) override { usb.data(bytes, n); }
};
int main(int argc, char **argv) {
    if (argc != 2) return 1;
    Peer peer(argv[1]); peer.fonts.ready = true;
    {
        struct Replies : cntrl::Replies {
            std::string last;
            void reply(const char *text) override { last = text; }
            void data(const uint8_t *, unsigned) override { assert(false); }
        } replies;
        TestFile reader(peer.filesystem), writer(peer.filesystem);
        font_assets::Storage files(peer.filesystem, reader, writer); files.ready = true;
        assert(files.command("CNTRL FONT BEGIN 1 8", replies, 1) && files.busy());
        files.poll(10002, replies);
        assert(!files.busy() && replies.last == "CNTRL ERROR font-timeout");
        assert(files.command("CNTRL FONT BEGIN 1 8", replies, 10003) && files.busy());
        assert(files.data(nullptr, 0, replies, 10004));
        assert(!files.busy() && replies.last == "CNTRL ERROR font-asset");
    }
    TestController controller(peer);
    cntrl::Documents documents(peer, peer, controller, peer); documents.boot();
    std::string line;
    return usb_peer(peer.usb, [&](uint8_t byte) {
        if (documents.busy()) { documents.receive(uint8_t(byte), 0); }
        else if (byte != '\n') { if (byte != '\r') line += byte; }
        else {
            unsigned long key; unsigned size;
            if (peer.fonts.command(line.c_str(), peer)) {}
            else if (std::sscanf(line.c_str(), "STAGE %lx %u", &key, &size) == 2) {
                const auto *active = font_assets::cache.get(uint32_t(key), size);
                const uint8_t *original = active ? active->data.data : nullptr;
                font_assets::cache.begin();
                bool good = peer.fonts.supports(uint32_t(key), size) && peer.fonts.stage(uint32_t(key), size);
                if (good && original) {
                    font_assets::cache.finish(false);
                    assert(font_assets::cache.get(uint32_t(key), size)->data.data == original);
                    font_assets::cache.begin(); assert(peer.fonts.stage(uint32_t(key), size));
                }
                font_assets::cache.finish(good);
                if (good && original) assert(font_assets::cache.get(uint32_t(key), size)->data.data == original);
                peer.reply(good ? "CNTRL OK" : "CNTRL ERROR font-load");
            } else if (std::sscanf(line.c_str(), "ACTIVE %lx %u", &key, &size) == 2) {
                peer.reply(font_assets::cache.get(uint32_t(key), size) ? "CNTRL OK" : "CNTRL ERROR absent");
            } else {
                for (char c : line) documents.receive(uint8_t(c), 0);
                documents.receive('\n', 0);
            }
            line.clear();
        }
    }, [&]() { documents.tick(0); }, [&]() { return documents.accepts_input(); },
        [&]() { documents.cancel_transfer(); peer.fonts.cancel_transfer(); line.clear(); },
        [&](cntrl::UsbKind kind, const uint8_t *bytes, unsigned n) {
            if (kind == cntrl::UsbKind::font) peer.fonts.data(bytes, n, peer, 0);
            else { assert(kind == cntrl::UsbKind::setup); documents.receive_data(bytes, n, 0); }
        });
}
