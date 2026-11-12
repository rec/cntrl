#include <cntrl/ui/input.h>
#include <cntrl/runtime.h>
#include <cntrl/navigation_snapshots.h>
#include "patch_hardware.h"
#include "storage.h"
#include "runtime_services.h"
#include <cassert>
#include <fstream>
#include <new>
#include <optional>

struct Hardware : TestPatchHardware, cntrl::Replies, cntrl::ui::ApplicationServices,
                  cntrl::SongResolver, cntrl::SongCapture, cntrl::midi::Sink {
    cntrl::midi::Bridge bridge{test_midi_outputs};
    uint32_t timestamp = 0;
    bool delivering = false;
    std::vector<uint32_t> sent_at;
    std::vector<std::vector<uint8_t>> packets;
    bool send(const char *, const uint8_t *, unsigned) override { return false; }
    bool send_patch(unsigned owner, const char *name, const uint8_t *bytes, unsigned length) override {
        const auto *output = midi_outputs().find(name);
        return output && bridge.owned_stream(owner, output->route, bytes, length);
    }
    cntrl::midi::Bridge::Delivery patch_delivery(unsigned owner) const override { return bridge.delivery(owner); }
    void cancel_patch(unsigned owner) override { bridge.cancel_owner(owner); }
    bool write_packet(unsigned, const cntrl::midi::Packet &packet) override {
        assert(delivering); sent_at.push_back(timestamp); packets.emplace_back(packet.bytes, packet.bytes + packet.size()); return true;
    }
    bool supports(const char *name) const override { return midi_outputs().find(name); }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override { assert(false); }
    void reply(const char *) override {}
    void data(const uint8_t *, unsigned) override { assert(false); }
    uint32_t application_time() const override { return timestamp; }
    void blank_performance() override {}
    bool output_idle() const override { return bridge.idle(); }
    bool song_transfer_busy() const override { return false; }
    bool switch_editor_open() const override { return false; }
    void open_switch_editor(const cntrl::Document &, uint64_t) override {}
    bool shift_switch_editor(bool) override { return false; }
    bool activate(const cntrl::SongSelection &, const cntrl::SongContinuation &, bool) override { return false; }
    cntrl::SongExecution execution_status() const override { return cntrl::SongExecution::completed; }
    bool capture_source(const cntrl::Document *&) override { return false; }
    bool revision(const cntrl::SongSelection &, uint32_t &) override { return false; }
    bool capture(cntrl::Song &, bool) override { return false; }
    void recalled() override {}
};
struct Actions {
    cntrl::ui::Application &application;
    bool controls_locked() const { return application.controls_locked; }
    bool menu_open() const { return application.menu.open(); }
    bool setup_open() const { return application.setups.open(); }
    bool songs_open() const { return application.song_ui.open(); }
    bool switches_open() const { return false; }
    void switch_button(unsigned, bool, uint32_t) {}
    void invoke(cntrl::ui::Operation) { assert(false); }
    void browse(bool) {}
    bool midi_reserved() const { return false; }
    bool keyboard(unsigned, bool) { return false; }
    bool monitor(unsigned, bool) { return false; }
    bool looper(unsigned, bool) { return false; }
    bool expression(unsigned) { return false; }
    bool port(unsigned) { return false; }
    bool clock(unsigned, bool) { return false; }
    bool has_controller() const { return true; }
    void controller(unsigned input, bool pressed, uint32_t now) { application.controller().button(input, pressed, now); }
    void tap(uint32_t) {}
};
struct Services {
    Hardware &hardware;
    cntrl::ui::Application &application;
    cntrl::ui::InputCoordinator &input;
    Actions routing;
    Session usb;
    Job job;
    uint32_t pending_until;
    uint8_t sampled = 1;
    unsigned scans = 0, displays = 0;
    uint32_t now() const { return hardware.timestamp; }
    void complete_display() {}
    void keyboard() {}
    bool scan_available() const { return hardware.timestamp >= pending_until; }
    void scan(uint32_t now, uint32_t) { ++scans; input.scan(sampled, now, *this); }
    std::optional<unsigned> debounce(unsigned) const { return std::nullopt; }
    void keyboard_release(unsigned) {}
    bool panic_open() const { return false; }
    void panic_release(uint32_t) {}
    bool animation_controls(uint8_t, uint32_t, bool) { return false; }
    bool diagnostic_controls(const uint8_t *, uint8_t, uint32_t) { return false; }
    cntrl::ui::FirmwareVisit *visit() { return nullptr; }
    Actions &actions() { return routing; }
    void pages(uint32_t now) { application.tick(now); }
    void midi() { hardware.delivering = true; hardware.bridge.service(hardware, now()); hardware.delivering = false; }
    Session &session() { return usb; }
    Job &commands() { return job; }
    Job &disk_transfer() { return job; }
    Job &song_transfer() { return job; }
    Job &bundle_transfer() { return job; }
    Job &history_transfer() { return job; }
    Job &fonts() { return job; }
    Job &song_sources() { return job; }
    Job &songs() { return job; }
    Hardware &replies() { return hardware; }
    bool serial_available() const { return false; }
    int serial_read() { return -1; }
    bool reply_ready() const { return true; }
    bool commands_allowed() const { return true; }
    bool expression_allowed() const { return true; }
    void cancel_listing() {}
    void expression(uint32_t) {}
    void navigation() {}
    void display(uint32_t) { ++displays; assert(!hardware.delivering); }
};
static void raw_press_reaches_midi(std::vector<uint8_t> bytes, bool fast, uint32_t display_pending_ms, bool bounce) {
    Hardware hardware; TestStorage setups, song_storage;
    cntrl::Document configuration; assert(configuration.load(bytes.data(), unsigned(bytes.size()), hardware));
    bytes[configuration.metadata() + cntrl::scrolling_offset + 5] = fast;
    cntrl::put32(bytes.data() + 20, cntrl::checksum(bytes.data() + 28, unsigned(bytes.size() - 28)));
    assert(configuration.load(bytes.data(), unsigned(bytes.size()), hardware)); assert(configuration.fast_switches() == fast);
    cntrl::Controller primary(hardware, hardware, hardware); hardware.controller = &primary;
    cntrl::Documents documents(setups, hardware, primary, hardware); cntrl::Songs songs(song_storage, hardware);
    {
        cntrl::ui::Application application(hardware, documents, primary, hardware, hardware, songs, hardware, true);
        assert(application.prepare_pages(configuration.page_layout())); application.finish_pages(true);
        application.layout = configuration.page_layout(); primary.activate(configuration, false);
        constexpr cntrl::ui::ControlBinding bindings[] = {{2, "A"}, {1, "B"}, {0, "C"}, {5, "D"}, {4, "E"}, {3, "F"}};
        cntrl::ButtonState states[6];
        cntrl::ui::InputCoordinator input(states, {0, 1, 2, 3, 4, 5, 5, 3, 2, bindings, 6}, configuration, application, hardware);
        Services services{hardware, application, input, Actions{application}, Session{}, Job{}, display_pending_ms};
        cntrl::CooperativeRuntime runtime(1);
        for (uint32_t now = 0; now <= 40; ++now) {
            hardware.timestamp = now; services.sampled = bounce && now == 2 ? 0 : 1; runtime.poll(services);
            if (now < display_pending_ms) assert(!services.scans && hardware.packets.empty());
        }
        assert(!hardware.sent_at.empty());
        assert(hardware.sent_at[0] == (fast ? std::max(1u, display_pending_ms) : 16u));
        assert((hardware.packets == std::vector<std::vector<uint8_t>>{{0xb0, 0, 1}, {0xb0, 32, 2}, {0xc0, 0}, {0xb0, 7, 100}, {0xb0, 10, 0}}));
        assert(services.displays == 41);
    }
}
static void controller_storage_preserves_active_pages_on_failure() {
    Hardware hardware; TestStorage setups, song_storage;
    cntrl::Controller primary(hardware, hardware, hardware);
    cntrl::Documents documents(setups, hardware, primary, hardware);
    cntrl::Songs songs(song_storage, hardware);
    const unsigned previous_budget = cntrl::memory_budget, initial = cntrl::memory_used;
    cntrl::memory_budget = initial + 8 * sizeof(cntrl::Controller);
    {
        cntrl::ui::Application application(hardware, documents, primary, hardware, hardware, songs, hardware, true);
        uint8_t records[8 + 4 * 34] = {};
        records[4] = 1;
        records[8 + 32] = cntrl::PageLayout::patch_switcher;
        assert(application.prepare_pages({records})); application.finish_pages(true);
        assert(cntrl::memory_used == initial);
        records[4] = 4;
        for (unsigned i = 1; i < 4; ++i) records[8 + i * 34 + 32] = cntrl::PageLayout::patch_switcher;
        cntrl::memory_budget = initial;
        assert(!application.prepare_pages({records})); application.finish_pages(false);
        assert(&application.at(0) == &primary && cntrl::memory_used == initial);
        cntrl::memory_budget = initial + 8 * sizeof(cntrl::Controller);
        assert(application.prepare_pages({records})); application.finish_pages(false);
        assert(cntrl::memory_used == initial);
        assert(application.prepare_pages({records})); application.finish_pages(true);
        assert(cntrl::memory_used == initial + 3 * sizeof(cntrl::Controller));
        auto *fourth = &application.at(3);
        records[4] = 2; cntrl::memory_budget = cntrl::memory_used;
        assert(!application.prepare_pages({records})); application.finish_pages(false);
        assert(&application.at(3) == fourth);
        records[4] = 1;
        assert(application.prepare_pages({records})); application.finish_pages(true);
        assert(cntrl::memory_used == initial);
    }
    assert(cntrl::memory_used == initial);
    cntrl::memory_budget = previous_budget;
}
int main(int argc, char **argv) {
    {
        const unsigned initial = cntrl::memory_used, budget = cntrl::memory_budget;
        cntrl::memory_budget = initial + 16384;
        Hardware hardware;
        cntrl::PatchExecutor a(hardware, hardware, hardware, 8), b(hardware, hardware, hardware, 8);
        cntrl::NavigationSnapshots snapshots;
        cntrl::Controller::NavigationState state; state.center = 17;
        assert(snapshots.put(a, state));
        const auto *first = snapshots.find(a);
        const unsigned used = cntrl::memory_used;
        assert(first && first->center == 17);
        cntrl::memory_budget = used;
        assert(!snapshots.put(b, state) && snapshots.find(a) == first);
        state.center = 18; assert(snapshots.put(a, state));
        assert(first->center == 18 && cntrl::memory_used == used);
        cntrl::memory_budget = initial + 16384;
        assert(snapshots.put(b, state));
        assert(snapshots.find(a) == first && snapshots.find(b) != first);
        snapshots.erase(b);
        snapshots.erase(a); assert(!snapshots.find(a) && cntrl::memory_used == initial);
        cntrl::memory_budget = budget;
    }
    controller_storage_preserves_active_pages_on_failure();
    assert(argc == 2); std::ifstream file(argv[1]); std::string hex; file >> hex; std::vector<uint8_t> bytes;
    for (unsigned i = 0; i < hex.size(); i += 2) bytes.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
    raw_press_reaches_midi(bytes, true, 0, false);
    raw_press_reaches_midi(bytes, true, 3, false);
    raw_press_reaches_midi(bytes, true, 0, true);
    raw_press_reaches_midi(bytes, false, 0, false);
}
