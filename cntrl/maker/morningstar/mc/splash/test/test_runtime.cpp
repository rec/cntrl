#include "../router.h"
#include "storage.h"
#include <cassert>
#include <fstream>
#include <string>
#include <vector>

struct Device : cntrl::Hardware, cntrl::Replies, cntrl::splash::Replies {
    cntrl::splash::Session *session = nullptr;
    std::vector<std::string> replies;
    unsigned sent = 0;
    bool send(const char *, const uint8_t *, unsigned) override { ++sent; return true; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned font, unsigned size) const override { return font == 0 && size % 8 == 0; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned, const cntrl::Pane &) override {}
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

int main(int argc, char **argv) {
    assert(argc == 2);
    Memory show_card, asset_card;
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
    assert(device.replies.back() == "CNTRL READY 5 0 00000000");
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
    std::snprintf(begin, sizeof(begin), "CNTRL BEGIN %u %08lx", unsigned(show.size()),
        (unsigned long)cntrl::checksum(show.data(), unsigned(show.size())));
    command(router, begin);
    assert(device.replies.back() == "CNTRL SEND");
    assert(storage_queries == 2);
    for (uint8_t c : show) router.receive(c, 0);
    while (documents.busy()) router.poll(0);
    assert(device.replies.back().find("CNTRL DONE ") == 0);
    assert(!session.poll(0)); // Enabled but absent asset: normal UI, no stuck splash.
    session.configure(false, 3000, 0);
    assert(!session.poll(10000) && session.brightness(192, 10000) == 192);

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
    command(router, "CNTRL SPLASH STATUS");
    assert(device.replies.back().find("CNTRL SPLASH READY ") == 0);
    command(router, "CNTRL STATUS");
    assert(device.replies.back().find("CNTRL READY 5 ") == 0);

    device.set_splash(true);
    assert(session.poll(1) && panels.rows == 1);
    assert(session.controls(0, 1));
    if (!session.controls(1u << 2, 2)) controller.button(2, true, 2);
    assert(device.sent == 0 && !session.take_redraw());
    assert(session.brightness(192, 2) == 192);
    assert(session.controls((1u << 2) | (1u << 5), 3));
    assert(session.controls(1u << 5, 4));
    assert(session.controls(0, 5));
    assert(!session.controls(0, 6));
    unsigned before_fade = panels.rows;
    assert(session.poll(502) && panels.rows > before_fade);
    for (unsigned target : {0u, 64u, 192u, 255u})
        assert(session.brightness(target, 502) == target / 2);
    // Normal musical actions resume after dismissal, not after the fade.
    if (!session.controls(1u << 2, 503)) controller.button(2, true, 503);
    assert(device.sent == 1);

    assert(!session.poll(1002, true));
    assert(session.brightness(192, 1002) == 0);
    assert(session.take_redraw() && !session.take_redraw());
    assert(!session.poll(1100) && session.brightness(192, 1100) == 0);
    assert(!session.poll(1150, true));
    assert(session.brightness(192, 1150) == 0);
    assert(!session.poll(1650, true) && session.brightness(192, 1650) == 96);
    assert(!session.poll(2150, true) && session.brightness(192, 2150) == 192);

    // Millisecond wraparound preserves each one-second ramp.
    uint32_t start = 0xffffff00u;
    session.configure(true, 0, start); assert(session.poll(start));
    assert(session.controls(1, start)); session.controls(0, start);
    assert(session.poll(start + 500) && session.brightness(192, start + 500) == 96);
    assert(!session.poll(start + 1000) && session.take_redraw());
    assert(!session.poll(start + 1100, true));
    assert(session.brightness(192, start + 1600) == 96);
    session.stop(); // Upload cancellation restores the configured brightness.
    assert(session.brightness(192, start + 1600) == 192 && session.take_redraw());

    device.set_splash(true); assert(session.poll(5));
    command(router, "CNTRL SPLASH STATUS"); assert(session.poll(6)); // Queries do not dismiss.
    command(router, begin); assert(!session.poll(7) && session.take_redraw());
    router.poll(10001); // Finish timed-out asset upload, leaving old slot available.
    assert(!assets.busy());
    command(router, "CNTRL STATUS");
    assert(device.replies.back().find("CNTRL READY 5 ") == 0);

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
    assert(!session.poll(45001) && session.brightness(192, 45001) == 96);
    assert(!session.controls(1u << 2, 45001)); // Interrupt entry without a brightness jump.
    assert(session.brightness(192, 45001) == 96);
    assert(session.poll(45101) && session.brightness(192, 45101) == 48);
    assert(!session.poll(45201) && session.take_redraw());
    assert(!session.controls(0, 45202));
    assert(!session.poll(45300, true));
    assert(!session.poll(45400, true) && session.brightness(192, 45400) == 96);
    assert(!session.poll(45500, true) && session.brightness(192, 45500) == 192);

    session.configure(false, 3000, 50000);
    assert(!session.poll(53000));
    assert(!session.poll(55000) && session.brightness(192, 55000) == 96);
    assert(session.poll(57000) && session.brightness(192, 57000) == 0);
    for (unsigned i = 0; i < 255; ++i) assert(session.poll(57001));
    assert(session.brightness(192, 58000) == 0); // Never reveal a partially drawn frame.
    assert(session.poll(58000)); // Last row completes the first frame; fade-in begins.
    assert(session.poll(60000) && session.brightness(192, 60000) == 96);
    assert(session.poll(62000) && session.brightness(192, 62000) == 192);
    unsigned sent_before_wake = device.sent;
    if (!session.controls(1u << 2, 62001)) controller.button(2, true, 62001);
    assert(device.sent == sent_before_wake + 1); // Wake sends now, not after the fade.
    assert(!session.controls(0, 62002));
    assert(session.poll(62101) && session.brightness(192, 62101) == 96);
    assert(!session.poll(62201) && session.take_redraw());
    assert(!session.poll(62300, true));
    assert(!session.poll(62400, true) && session.brightness(192, 62400) == 96);
    assert(!session.poll(62500, true) && session.brightness(192, 62500) == 192);

    session.configure(false, 3000, start);
    assert(!session.poll(start + 3000));
    assert(!session.poll(start + 5000) && session.brightness(192, start + 5000) == 96);
    session.stop(); session.take_redraw();
    // Show transfers inhibit idle entry, but ordinary status queries do not.
    session.configure(false, 3000, 70000);
    assert(!session.poll(80000, true, true));
    assert(!session.poll(82999) && session.brightness(192, 82999) == 192);
    command(router, "CNTRL STATUS");
    assert(!session.poll(83000));
    assert(!session.poll(85000) && session.brightness(192, 85000) == 96);
    session.configure(false, 0, 85000);
    assert(!session.poll(100000) && session.brightness(192, 100000) == 192);

    for (unsigned i = 0; i < 120; ++i) router.receive('x', 10003);
    router.receive('\n', 10003);
    assert(device.replies.back() == "CNTRL ERROR command-too-long");
    router.receive('x', 10003); router.poll(20004);
    assert(device.replies.back() == "CNTRL ERROR transfer-timeout");
}
