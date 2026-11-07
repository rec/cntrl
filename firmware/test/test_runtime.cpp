#include <cntrl/runtime.h>
#include <cntrl/ui/controls.h>
#include <cassert>
#include <vector>

struct Session {
    unsigned received = 0, consumed_count = 0;
    bool blocked = false, reset = false;
    bool output_pending() const { return blocked; }
    void receive(int) { ++received; }
    bool resetting() const { return reset; }
    void reset_done() { reset = false; }
    unsigned input_size() const { return received - consumed_count; }
    char input() const { return 'x'; }
    void consumed() { ++consumed_count; }
    bool active() const { return true; }
};
struct Job {
    unsigned polls = 0, keepalives = 0, cancellations = 0;
    bool processing() const { return false; }
    bool accepts_input() const { return true; }
    void cancel_transfer() { ++cancellations; }
    void cancel() { ++cancellations; }
    void keepalive(uint32_t) { ++keepalives; }
    void receive(char, uint32_t) {}
    void poll(uint32_t) { ++polls; }
    template<class Replies> void poll(uint32_t, Replies &) { ++polls; }
    void tick(uint32_t) { ++polls; }
    void tick() { ++polls; }
};
struct Services {
    Session usb;
    Job command, transfer, bundle, font, source, song;
    std::vector<unsigned> order;
    unsigned serial_reads = 0;
    bool reply = true, bus = true, due = true;
    uint32_t now() const { return 42; }
    void complete_display() { order.push_back(0); }
    void keyboard() { order.push_back(1); }
    bool scan_available() const { return bus; }
    bool scan_due() const { return due; }
    void scan(uint32_t) { order.push_back(2); }
    void apps(uint32_t) { order.push_back(3); }
    void midi() { order.push_back(4); }
    Session &session() { return usb; }
    Job &commands() { return command; }
    Job &song_transfer() { return transfer; }
    Job &bundle_transfer() { return bundle; }
    Job &fonts() { return font; }
    Job &song_sources() { return source; }
    Job &songs() { return song; }
    Services &replies() { return *this; }
    bool serial_available() const { return serial_reads < 1000; }
    int serial_read() { assert(order.back() == 4); ++serial_reads; return 0; }
    bool reply_ready() const { return reply; }
    bool commands_allowed() const { return true; }
    bool expression_allowed() const { return true; }
    void cancel_listing() {}
    void expression(uint32_t) { order.push_back(5); }
    void navigation() { order.push_back(6); }
    void display(uint32_t) { order.push_back(7); }
};
struct Actions {
    enum View { performance, menu, switches, songs, setup } view = performance;
    std::vector<unsigned> actions;
    bool locked = false, reserved = false;
    bool switches_open() const { return view == switches; }
    bool menu_open() const { return view == menu; }
    bool songs_open() const { return view == songs; }
    bool setup_open() const { return view == setup; }
    bool controls_locked() const { return locked; }
    void invoke(cntrl::ui::Operation op) { actions.push_back(unsigned(op)); }
    void switch_button(unsigned, bool pressed, uint32_t) { actions.push_back(pressed ? 20 : 21); }
    void browse(bool reverse) { actions.push_back(reverse ? 31 : 30); }
    bool midi_reserved() const { return reserved; }
    bool keyboard(unsigned, bool) { return false; }
    bool expression(unsigned) { return false; }
    bool port(unsigned) { return false; }
    bool clock(unsigned, bool) { return false; }
    bool has_app() const { return true; }
    void app(unsigned i, bool pressed, uint32_t) { actions.push_back(40 + i * 2 + !pressed); }
    void tap(uint32_t) { actions.push_back(90); }
};
int main() {
    cntrl::CooperativeRuntime runtime; Services services; runtime.poll(services);
    assert(services.order == std::vector<unsigned>({0, 1, 2, 3, 4, 5, 6, 7}));
    assert(services.serial_reads == 512 && services.usb.consumed_count == 128);
    assert(services.command.polls == 1 && services.transfer.polls == 1 && services.font.polls == 1);
    assert(services.source.polls == 1 && services.song.polls == 1);
    Services blocked; blocked.reply = false; blocked.usb.blocked = true; blocked.bus = false; runtime.poll(blocked);
    assert(blocked.serial_reads == 0 && blocked.command.polls == 0 && blocked.transfer.polls == 0);
    assert(blocked.order == std::vector<unsigned>({0, 1, 3, 4, 5, 6, 7}));
    assert(blocked.source.polls == 1 && blocked.song.polls == 1);
    constexpr cntrl::ui::ControlBinding bindings[] = {{7, "Left"}, {3, "Right"}, {4, "Back"}, {1, "Confirm"}};
    cntrl::ui::ControlRouter router({0, 1, 3, 0, 1, 2, 2, 0, 3, bindings, 4}); Actions actions;
    uint8_t events[4] = {1, 0, 0, 0}; router.route(actions, events, 1, 0);
    assert(actions.actions.empty()); // Reserved reverse gesture needs forward held.
    router.route(actions, events, 5, 0); assert(actions.actions.back() == 31);
    actions.actions.clear(); actions.view = Actions::menu; router.route(actions, events, 1, 0);
    assert(actions.actions == std::vector<unsigned>{unsigned(cntrl::ui::Operation::menu_previous)});
    actions.actions.clear(); actions.view = Actions::performance; events[0] = 0; events[1] = 1; router.route(actions, events, 2, 0);
    assert(actions.actions == std::vector<unsigned>{46}); // Adapter mapping, independent of physical index.
    actions.actions.clear(); actions.view = Actions::switches; actions.locked = true; events[1] = 2; router.route(actions, events, 0, 0);
    assert(actions.actions == std::vector<unsigned>{21}); // Release survives navigation lock.
}
