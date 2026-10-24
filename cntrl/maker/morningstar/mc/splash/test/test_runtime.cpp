#include "../router.h"
#include "storage.h"
#include "../../../../../../firmware/test/storage.h"
#include <cassert>
#include <fstream>
#include <string>
#include <vector>

struct Device : cntrl::Hardware, cntrl::Replies, cntrl::splash::Replies {
    cntrl::splash::Session *session = nullptr;
    std::vector<std::string> replies;
    unsigned sent = 0;
    cntrl::Pane panes[3];
    bool send(const char *, const uint8_t *, unsigned) override { ++sent; return true; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned font, unsigned size) const override { return font == 0 && size % 8 == 0; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned slot, const cntrl::Pane &pane) override { panes[slot] = pane; }
    void set_brightness(uint8_t) override {}
    void set_splash(bool enabled, uint32_t idle_ms = 0) override { session->configure(enabled, idle_ms, 0); }
    void error(const char *) override {}
    void reply(const char *line) override { replies.emplace_back(line); }
};

struct Panels : cntrl::splash::Output {
    unsigned rows = 0;
    void row(uint8_t, unsigned, const uint8_t *) override { ++rows; }
};

void command(cntrl::splash::Router &router, const std::string &line) {
    for (unsigned char c : line + "\n") router.receive(c, 0);
}

int storage_queries = 0;
int storage_scans = 0;
void storage_info(bool scan) { ++storage_queries; if (scan) ++storage_scans; }

void test_cancel_idle_entry(cntrl::splash::Receiver &assets, const std::vector<uint8_t> &show) {
    for (unsigned phase = 0; phase < 3; ++phase) for (unsigned button = 0; button < 6; ++button) {
        Device device;
        Panels panels;
        cntrl::splash::Player player(assets, panels);
        cntrl::splash::Session session(assets, player);
        device.session = &session;
        cntrl::Controller controller(device);
        cntrl::Document document;
        assert(document.load(show.data(), unsigned(show.size()), device));
        controller.activate(document);
        session.configure(false, 3000, 0);
        assert(!session.poll(0, true));
        assert(!session.poll(3000, true));
        uint32_t now;
        if (phase == 0) {
            now = 5000;
            assert(!session.poll(now) && session.brightness(192, now) == 48);
        } else {
            assert(session.poll(7000));
            unsigned rows = phase == 1 ? 64 : 256;
            for (unsigned i = 0; i < rows; ++i) assert(session.poll(7000));
            now = phase == 1 ? 7000 : 9000;
            assert(session.poll(now));
            assert(session.brightness(192, now) == (phase == 1 ? 0 : 48));
        }
        unsigned rows = panels.rows;
        uint8_t mask = 1u << button, other = 1u << ((button + 1) % 6);
        if (!session.controls(mask, now)) controller.button(button, true, now);
        assert(device.sent == 0 && !std::strcmp(device.panes[2].patch, "Flute"));
        assert(session.brightness(192, now) == 192);
        assert(!session.poll(now, true) && session.take_redraw());
        assert(panels.rows == rows); // Stopped animation cannot paint over the UI.
        assert(session.controls(mask | other, now + 1));
        assert(session.controls(other, now + 2));
        if (!session.controls(0, now + 3)) controller.button(button, false, now + 3);
        assert(device.sent == 0 && !std::strcmp(device.panes[2].patch, "Flute"));
        assert(!session.controls(0, now + 4));
        assert(!session.controls(1u << 2, now + 5));
        controller.button(2, true, now + 5);
        assert(device.sent == 1); // The very next press sends normally.
    }
}

int main(int argc, char **argv) {
    assert(argc == 2);
    TestStorage show_card;
    Memory asset_card;
    Device device;
    Panels panels;
    cntrl::Controller controller(device);
    cntrl::Documents documents(show_card, device, controller, device);
    cntrl::splash::Receiver assets(asset_card, device);
    cntrl::splash::Player player(assets, panels);
    cntrl::splash::Session session(assets, player);
    cntrl::splash::Router router(documents, assets, session, device, storage_info);
    device.session = &session;
    assets.boot();
    while (!router.accepts_input()) router.poll(0);
    command(router, "CNTRL STATUS");
    assert(device.replies.back() == "CNTRL READY 8 0 00000000");
    command(router, "CNTRL STORAGE");
    assert(storage_queries == 1 && storage_scans == 0);
    command(router, "CNTRL STORAGE SCAN");
    assert(storage_queries == 2 && storage_scans == 1);
    command(router, "CNTRL STORAGE unknown");
    assert(storage_queries == 2 && device.replies.back() == "CNTRL ERROR invalid-command");

    std::ifstream input(argv[1]);
    std::string hex; input >> hex;
    std::vector<uint8_t> show;
    for (unsigned i = 0; i < hex.size(); i += 2)
        show.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
    show[19] = 1;
    char begin[96];
    std::snprintf(begin, sizeof(begin), "CNTRL SETUP PUT 54657374 %u %08lx 0", unsigned(show.size()),
        (unsigned long)cntrl::checksum(show.data(), unsigned(show.size())));
    command(router, begin);
    assert(device.replies.back() == "CNTRL SEND");
    assert(storage_queries == 2);
    for (uint8_t c : show) router.receive(c, 0);
    while (documents.busy()) router.poll(0);
    assert(device.replies.back() == "CNTRL OK");
    command(router, "CNTRL SETUP FINISH");
    assert(!session.poll(0)); // Enabled but absent asset: normal UI, no stuck splash.
    session.configure(false, 3000, 0);
    assert(!session.poll(10000, true) && session.brightness(192, 10000) == 192);

    using namespace cntrl::splash;
    std::vector<uint8_t> animation(header_bytes + frame_bytes, 0);
    std::memcpy(animation.data(), "CNSA", 4);
    animation[4] = 1; animation[6] = 160; animation[8] = 128; animation[10] = 1;
    put32(animation.data() + 12, unsigned(animation.size()));
    put32(animation.data() + header_bytes, 100);
    // Command-shaped pixel bytes must never reach any line-oriented handler.
    constexpr char embedded[] = "\nCNTRL STORAGE SCAN\n";
    std::memcpy(animation.data() + 1000, embedded, sizeof(embedded) - 1);
    uint32_t crc = cntrl::checksum(animation.data(), unsigned(animation.size()));
    std::snprintf(begin, sizeof(begin), "CNTRL SPLASH BEGIN %u %08lx", unsigned(animation.size()),
                  (unsigned long)crc);
    command(router, begin);
    assert(device.replies.back() == "CNTRL SPLASH SEND");
    for (uint8_t c : animation) router.receive(c, 0);
    assert(!router.accepts_input());
    while (!router.accepts_input()) router.poll(0);
    assert(storage_queries == 2 && storage_scans == 1);
    assert(device.replies.back().find("CNTRL SPLASH DONE ") == 0);
    test_cancel_idle_entry(assets, show);
    command(router, "CNTRL SPLASH STATUS");
    assert(device.replies.back().find("CNTRL SPLASH READY ") == 0);
    command(router, "CNTRL STATUS");
    assert(device.replies.back().find("CNTRL READY 8 ") == 0);

    {
        Player boot_player(assets, panels);
        Session boot(assets, boot_player);
        boot.configure(true, 0, 0);
        assert(boot.brightness(192, 0) == 0);
        for (unsigned i = 0; i < 255; ++i) {
            assert(boot.poll(0));
            assert(boot.brightness(192, 0) == 0);
        }
        assert(boot.poll(0) && boot.brightness(192, 0) == 0);
        assert(boot.poll(500) && boot.brightness(192, 500) == 12);
        assert(boot.poll(1000) && boot.brightness(192, 1000) == 48);
        assert(boot.poll(1500) && boot.brightness(192, 1500) == 108);
        assert(boot.poll(2000) && boot.brightness(192, 2000) == 192);
        boot.stop();
    }
    {
        Player boot_player(assets, panels);
        Session boot(assets, boot_player);
        boot.configure(false, 0, 0);
        assert(!boot.poll(0) && boot.brightness(192, 0) == 0);
        assert(!boot.poll(1, true) && boot.brightness(192, 1) == 192);
    }
    {
        Player boot_player(assets, panels);
        Session boot(assets, boot_player);
        boot.configure(true, 0, 0);
        assert(boot.controls(1, 0)); // Cancelling before the asset opens also stays dark.
        assert(!boot.poll(0) && boot.brightness(192, 0) == 0);
        assert(!boot.poll(1, true) && boot.brightness(192, 1) == 192);
    }
    {
        Player boot_player(assets, panels);
        Session boot(assets, boot_player);
        boot.configure(true, 0, 0);
        assert(boot.poll(0));
        assert(boot.controls(1, 1)); // Dismiss a partially painted startup frame.
        for (unsigned i = 0; i < 255; ++i) assert(boot.poll(1));
        assert(boot.brightness(192, 1) == 0);
        assert(!boot.poll(1001) && boot.take_redraw());
        assert(!boot.poll(1010, true) && boot.brightness(192, 1010) == 0);
        assert(!boot.poll(1510, true) && boot.brightness(192, 1510) == 48);
    }
    {
        Player boot_player(assets, panels);
        Session boot(assets, boot_player);
        boot.configure(true, 0, 0);
        for (unsigned i = 0; i < 256; ++i) assert(boot.poll(0));
        assert(boot.poll(1000) && boot.brightness(192, 1000) == 48);
        assert(boot.controls(1, 1000)); // Startup fade-in remains dismissible.
        assert(boot.brightness(192, 1000) == 48);
        assert(boot.controls(0, 1001));
        assert(boot.poll(1500) && boot.brightness(192, 1500) == 12);
        assert(!boot.poll(2000) && boot.take_redraw());
    }
    panels.rows = 0;
    device.set_splash(true);
    assert(session.brightness(192, 0) == 0); // Reloading a show also blanks immediately.
    assert(session.poll(1) && panels.rows == 1);
    assert(session.brightness(192, 1) == 0);
    for (unsigned i = 0; i < 255; ++i) assert(session.poll(1));
    assert(session.poll(2001) && session.brightness(192, 2001) == 192);
    assert(session.controls(0, 2001));
    if (!session.controls(1u << 2, 2002)) controller.button(2, true, 2002);
    assert(device.sent == 0 && !session.take_redraw());
    assert(session.brightness(192, 2002) == 192);
    assert(session.brightness(192, 2252) == 108);
    assert(session.brightness(192, 2752) == 12);
    unsigned previous = 255;
    for (unsigned elapsed = 0; elapsed <= 1000; ++elapsed) {
        unsigned brightness = session.brightness(255, 2002 + elapsed);
        assert(brightness <= previous);
        assert(brightness == 255u * (1000 - elapsed) * (1000 - elapsed) / 1000000);
        previous = brightness;
    }
    assert(session.controls((1u << 2) | (1u << 5), 2003));
    assert(session.controls(1u << 5, 2004));
    assert(session.controls(0, 2005));
    assert(!session.controls(0, 2006));
    unsigned before_fade = panels.rows;
    assert(session.poll(2502) && panels.rows > before_fade);
    for (unsigned target : {0u, 64u, 192u, 255u})
        assert(session.brightness(target, 2502) == target / 4);
    // Normal musical actions resume after dismissal, not after the fade.
    if (!session.controls(1u << 2, 2503)) controller.button(2, true, 2503);
    assert(device.sent == 1);

    assert(!session.poll(3002, true));
    assert(session.brightness(192, 3002) == 0);
    assert(session.take_redraw() && !session.take_redraw());
    assert(!session.poll(3100) && session.brightness(192, 3100) == 0);
    assert(!session.poll(3150, true));
    assert(session.brightness(192, 3150) == 0);
    assert(session.brightness(192, 3400) == 12);
    assert(session.brightness(192, 3900) == 108);
    previous = 0;
    for (unsigned elapsed = 0; elapsed <= 1000; ++elapsed) {
        unsigned brightness = session.brightness(255, 3150 + elapsed);
        assert(brightness >= previous);
        assert(brightness == 255u * elapsed * elapsed / 1000000);
        previous = brightness;
    }
    assert(!session.poll(3650, true) && session.brightness(192, 3650) == 48);
    assert(!session.poll(4150, true) && session.brightness(192, 4150) == 192);

    // Millisecond wraparound preserves startup and dismissal ramp timings.
    uint32_t start = 0xffffff00u;
    session.configure(true, 0, start);
    for (unsigned i = 0; i < 256; ++i) assert(session.poll(start));
    assert(session.poll(start + 1000) && session.brightness(192, start + 1000) == 48);
    assert(session.poll(start + 2000) && session.brightness(192, start + 2000) == 192);
    assert(session.controls(1, start + 2000)); session.controls(0, start + 2000);
    assert(session.poll(start + 2500) && session.brightness(192, start + 2500) == 48);
    assert(!session.poll(start + 3000) && session.take_redraw());
    assert(!session.poll(start + 3100, true));
    assert(session.brightness(192, start + 3600) == 48);
    session.stop(); // Upload cancellation restores the configured brightness.
    assert(session.brightness(192, start + 3600) == 192 && session.take_redraw());

    device.set_splash(true); assert(session.poll(5));
    command(router, "CNTRL SPLASH STATUS"); assert(session.poll(6)); // Queries do not dismiss.
    command(router, begin); assert(!session.poll(7) && session.take_redraw());
    router.poll(10001); // Finish timed-out asset upload, leaving old slot available.
    assert(!assets.busy());
    command(router, "CNTRL STATUS");
    assert(device.replies.back().find("CNTRL READY 8 ") == 0);

    session.configure(true, 0, 10002);
    assert(session.controls(1, 10002)); // A press during pending startup also cancels.
    assert(!session.poll(10002));
    session.controls(0, 10002);
    session.configure(false, 0, 10003); assert(!session.poll(10003));

    // Held buttons and releases count as physical activity; no saver while held.
    session.configure(false, 3000, 30000);
    assert(!session.poll(32999));
    assert(!session.controls(1, 32999));
    assert(!session.poll(40000) && session.brightness(192, 40000) == 192);
    assert(!session.controls(0, 40001));
    assert(!session.poll(43000) && session.brightness(192, 43000) == 192);
    assert(!session.poll(43001)); // UI fades out, but remains the rendering owner.
    assert(!session.poll(45001) && session.brightness(192, 45001) == 48);
    assert(session.controls(1u << 2, 45001)); // Entry cancellation consumes the press.
    assert(session.brightness(192, 45001) == 192);
    assert(!session.poll(45001) && session.take_redraw());
    assert(session.controls(0, 45002));
    assert(!session.controls(0, 45003));
    assert(!session.poll(45500, true) && session.brightness(192, 45500) == 192);

    session.configure(false, 3000, 50000);
    assert(!session.poll(53000));
    assert(!session.poll(55000) && session.brightness(192, 55000) == 48);
    assert(session.poll(57000) && session.brightness(192, 57000) == 0);
    for (unsigned i = 0; i < 255; ++i) assert(session.poll(57001));
    assert(session.brightness(192, 58000) == 0); // Never reveal a partially drawn frame.
    assert(session.poll(58000)); // Last row completes the first frame; fade-in begins.
    assert(session.poll(60000) && session.brightness(192, 60000) == 48);
    assert(session.poll(62000) && session.brightness(192, 62000) == 192);
    unsigned sent_before_wake = device.sent;
    if (!session.controls(1u << 2, 62001)) controller.button(2, true, 62001);
    assert(device.sent == sent_before_wake + 1); // Wake sends now, not after the fade.
    assert(!session.management_allowed());
    assert(!session.controls(9, 62001) && !session.management_allowed());
    assert(!session.controls(8, 62002) && !session.management_allowed());
    assert(!session.controls(0, 62002));
    assert(session.management_allowed());
    assert(!session.controls(9, 62003) && session.management_allowed()); // Second A+D.
    session.controls(0, 62004);
    assert(session.poll(62101) && session.brightness(192, 62101) == 48);
    assert(!session.poll(62201) && session.take_redraw());
    assert(!session.poll(62300, true));
    assert(!session.poll(62400, true) && session.brightness(192, 62400) == 48);
    assert(!session.poll(62500, true) && session.brightness(192, 62500) == 192);

    session.configure(false, 3000, start);
    assert(!session.poll(start + 3000));
    assert(!session.poll(start + 5000) && session.brightness(192, start + 5000) == 48);
    session.stop(); session.take_redraw();
    // Show transfers inhibit idle entry, but ordinary status queries do not.
    session.configure(false, 3000, 70000);
    assert(!session.poll(80000, true, true));
    assert(!session.poll(82999) && session.brightness(192, 82999) == 192);
    command(router, "CNTRL STATUS");
    assert(!session.poll(83000));
    assert(!session.poll(85000) && session.brightness(192, 85000) == 48);
    session.configure(false, 0, 85000);
    assert(!session.poll(100000) && session.brightness(192, 100000) == 192);

    for (unsigned i = 0; i < 300; ++i) router.receive('x', 10003);
    router.receive('\n', 10003);
    assert(device.replies.back() == "CNTRL ERROR command-too-long");
    router.receive('x', 10003); router.poll(20004);
    assert(device.replies.back() == "CNTRL ERROR transfer-timeout");
}
