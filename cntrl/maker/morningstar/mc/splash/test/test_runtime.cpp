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
    void set_splash(bool enabled) override { session->configure(enabled); }
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
    assert(device.replies.back() == "CNTRL READY 4 0 00000000");
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
    assert(device.replies.back().find("CNTRL READY 4 ") == 0);

    device.set_splash(true);
    assert(session.poll(1) && panels.rows == 1);
    assert(session.controls(0));
    if (!session.controls(1u << 2)) controller.button(2, true, 2);
    assert(device.sent == 0 && session.take_redraw() && !session.take_redraw());
    assert(session.controls((1u << 2) | (1u << 5)));
    assert(session.controls(1u << 5));
    assert(session.controls(0));
    assert(!session.controls(0));
    assert(!session.poll(3));
    if (!session.controls(1u << 2)) controller.button(2, true, 4);
    assert(device.sent == 1);

    device.set_splash(true); assert(session.poll(5));
    command(router, "CNTRL SPLASH STATUS"); assert(session.poll(6)); // Queries do not dismiss.
    command(router, begin); assert(!session.poll(7) && session.take_redraw());
    router.poll(10001); // Finish timed-out asset upload, leaving old slot available.
    assert(!assets.busy());
    command(router, "CNTRL STATUS");
    assert(device.replies.back().find("CNTRL READY 4 ") == 0);

    session.configure(true);
    assert(session.controls(1)); // A press during pending startup also cancels.
    assert(!session.poll(10002));
    session.controls(0);
    session.configure(false); assert(!session.poll(10003));
    for (unsigned i = 0; i < 120; ++i) router.receive('x', 10003);
    router.receive('\n', 10003);
    assert(device.replies.back() == "CNTRL ERROR command-too-long");
    router.receive('x', 10003); router.poll(20004);
    assert(device.replies.back() == "CNTRL ERROR transfer-timeout");
}
