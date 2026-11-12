#include <cntrl/runtime.h>
#include <cntrl/ui/controls.h>
#include <cassert>
#include <vector>

#include "runtime_services.h"

struct Services {
    Session usb;
    Job command, transfer, bundle, history, font, source, song, disk, midi_file;
    std::vector<unsigned> order;
    unsigned serial_reads = 0;
    bool reply = true, bus = true;
    uint32_t timestamp = 42, scan_gap = 0;
    uint32_t now() const { return timestamp; }
    void complete_display() { order.push_back(0); }
    void keyboard() { order.push_back(1); }
    bool scan_available() const { return bus; }
    void scan(uint32_t, uint32_t gap) { scan_gap = gap; order.push_back(2); }
    void pages(uint32_t) { order.push_back(3); }
    void midi() { order.push_back(4); }
    Session &session() { return usb; }
    Job &commands() { assert(order.back() == 4); return command; }
    Job &disk_transfer() { return disk; }
    Job &midi_transfer() { return midi_file; }
    Job &song_transfer() { return transfer; }
    Job &bundle_transfer() { return bundle; }
    Job &history_transfer() { return history; }
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
    bool monitoring = false;
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
    bool monitor(unsigned, bool pressed) { if (monitoring) actions.push_back(pressed ? 91 : 92); return monitoring; }
    bool looper(unsigned, bool) { return false; }
    bool expression(unsigned) { return false; }
    bool port(unsigned) { return false; }
    bool clock(unsigned, bool) { return false; }
    bool has_controller() const { return true; }
    void controller(unsigned i, bool pressed, uint32_t) { actions.push_back(40 + i * 2 + !pressed); }
    void tap(uint32_t) { actions.push_back(90); }
};
int main() {
    cntrl::CooperativeRuntime runtime; Services services; runtime.poll(services);
    assert(services.order == std::vector<unsigned>({0, 1, 2, 3, 4, 5, 6, 7}));
    assert(services.serial_reads == 512 && services.usb.consumed_count == 128);
    assert(services.command.polls == 1 && services.transfer.polls == 1 && services.font.polls == 1 && services.disk.polls == 1);
    assert(services.midi_file.polls == 1 && services.midi_file.keepalives == 1);
    assert(services.source.polls == 1 && services.song.polls == 1);
    cntrl::CooperativeRuntime binary_runtime; Services binary;
    binary.usb.received = 4096; binary.usb.blocked = true; binary.usb.kind = cntrl::UsbKind::disk;
    binary.reply = false; binary_runtime.poll(binary);
    assert(binary.command.blocks == 0 && binary.usb.consumed_count == 0);
    binary.reply = true; binary.timestamp += 1; binary.order.clear(); binary_runtime.poll(binary);
    assert(binary.order == std::vector<unsigned>({0, 1, 2, 3, 4, 5, 6, 7}));
    assert(binary.command.blocks == 1 && binary.command.bytes == 4096 && binary.usb.consumed_count == 4096);
    Services resetting; resetting.usb.reset = true; runtime.poll(resetting);
    assert(resetting.disk.cancellations == 1);
    assert(resetting.midi_file.cancellations == 1);
    Services blocked; blocked.reply = false; blocked.usb.blocked = true; blocked.bus = false; runtime.poll(blocked);
    assert(blocked.serial_reads == 0 && blocked.command.polls == 0 && blocked.transfer.polls == 0);
    assert(blocked.order == std::vector<unsigned>({0, 1, 3, 4, 5, 6, 7}));
    assert(blocked.source.polls == 1 && blocked.song.polls == 1);
    assert(!runtime.scan_due(42) && runtime.scan_due(43));
    blocked.timestamp = 43; blocked.bus = false; runtime.poll(blocked);
    assert(runtime.scan_due(43)); // A busy bus does not consume the deadline.
    blocked.order.clear(); blocked.bus = true; blocked.timestamp = 44; runtime.poll(blocked);
    assert(blocked.scan_gap == 2);
    assert(blocked.order == std::vector<unsigned>({0, 1, 2, 3, 4, 5, 6, 7}));
    cntrl::CooperativeRuntime wrapping(10); Services wrap;
    wrap.timestamp = 0xfffffff0; wrapping.poll(wrap);
    assert(!wrapping.scan_due(0xfffffff5));
    wrap.timestamp = 5; wrapping.poll(wrap); assert(wrap.scan_gap == 21);
    constexpr cntrl::ui::ControlBinding bindings[] = {{7, "Left"}, {3, "Right"}, {4, "Back"}, {1, "Confirm"}};
    cntrl::ui::ControlRouter router({0, 1, 3, 0, 1, 2, 2, 0, 3, bindings, 4}); Actions actions;
    uint8_t events[4] = {1, 0, 0, 0}; router.route(actions, events, 1, 0);
    assert(actions.actions == std::vector<unsigned>{54}); // Reverse input still reaches its controller action alone.
    actions.actions.clear(); events[0] = 2; router.route(actions, events, 0, 0);
    assert(actions.actions == std::vector<unsigned>{55}); // Its release reaches the controller too.
    actions.actions.clear(); events[0] = 1; router.route(actions, events, 5, 0);
    assert(actions.actions == std::vector<unsigned>{31}); // Holding forward reserves the reverse page gesture.
    actions.actions.clear(); actions.view = Actions::menu; router.route(actions, events, 1, 0);
    assert(actions.actions == std::vector<unsigned>{unsigned(cntrl::ui::Operation::menu_previous)});
    actions.actions.clear(); actions.view = Actions::performance; events[0] = 0; events[1] = 1; router.route(actions, events, 2, 0);
    assert(actions.actions == std::vector<unsigned>{46}); // Adapter mapping, independent of physical index.
    actions.actions.clear(); actions.view = Actions::switches; actions.locked = true; events[1] = 2; router.route(actions, events, 0, 0);
    assert(actions.actions == std::vector<unsigned>{21}); // Release survives navigation lock.
    actions.view = Actions::performance; actions.locked = false; actions.monitoring = true; actions.actions.clear(); events[1] = 1;
    router.route(actions, events, 2, 0); assert(actions.actions == std::vector<unsigned>{91});
    actions.actions.clear(); events[1] = 2;
    router.route(actions, events, 0, 0); assert(actions.actions == std::vector<unsigned>{92}); // Monitor controls never reach the patch-switcher.
}
