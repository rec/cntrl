#include "target.h"
#if CNTRL_ORIGINAL_FIRMWARE
#include "../boot_switch/selection.h"
extern "C" void eepromemu_flash_write(void *, const void *, uint32_t);
#endif
#include <Arduino.h>
// Inline shared routines need distinct sections alongside strong FLASHMEM functions.
#define CNTRL_FLASH_ROUTINE(name) __attribute__((section(".flashmem.cntrl_" #name), noinline))
#include <usb_keyboard.h>
#include <SD.h>
#include "expression_pedals.h"
#include "midi_files.h"
#include <USBHost_t36.h>
#include <malloc.h>
#include <new>
extern "C" { extern char _ebss, _heap_end; extern char *__brkval; }
extern "C" char cntrl_memory_budget;
#include <cntrl/documents.h>
#include <cntrl/patch_sources.h>
#include <cntrl/bundle_transfer.h>
#include <cntrl/setup_ui.h>
#include <cntrl/midi_bridge.h>
#include <cntrl/midi_outputs.h>
#include <cntrl/module_menu.h>
#if CNTRL_SWITCHES
#include <cntrl/switches.h>
#endif
#if CNTRL_PANIC
#include <cntrl/panic.h>
#endif
#include <cntrl/songs_ui.h>
#include <cntrl/songs_transfer.h>
#include <cntrl/usb_session.h>
#if CNTRL_CABLE_TEST
#include <cntrl/cable_test.h>
#endif
#include "buttons.h"
#include "pixel_throb.h"
#include "displays.h"
#include "switches.h"
#include "renderer.h"
#include "pages.h"
#include "trs_ports.h"
#ifdef CNTRL_DIAGNOSTICS
#include "scroll_timing.h"
#endif
#include "storage_listing.h"
#include "font_storage.h"
#include "../../splash/board.h"
#include "../../splash/router.h"

// Library graphics draw into RAM; only our bounded driver touches panel pins.

namespace {
#ifdef CNTRL_DIAGNOSTICS
ScrollTimingProbe scroll_timing;
#endif
extern cntrl::Documents documents;
extern cntrl::splash::Session splash_session;
extern cntrl::splash::Panels splash_panels;
font_assets::Files font_files;
TrsPorts trs_ports;
USBHost host;
MIDIDevice host_midi(host);
extern cntrl::StoredPatchSources patch_sources;
cntrl::Controller &app_controller();
void configure_apps(const cntrl::Document &document);
void refresh_app();
void configure_app_menu();
bool app_visible();
bool apps_busy();
void app_brightness(uint8_t value);
unsigned rendering_app();
extern uint8_t held_controls;
bool patch_state(unsigned operation, uint32_t value);
enum class UiOperation : unsigned {
    menu_previous = 128, menu_next, menu_activate, menu_back, menu_open, performance_show,
    setup_previous, setup_next, setup_confirm, setup_back,
    songs_previous, songs_next, songs_confirm, songs_previous_section, songs_next_section, songs_back,
    switches_return = 153,
};
bool state_ui(UiOperation operation);
bool patch_stock(uint8_t pair, cntrl::PatchExecutor &invocation);
bool patch_work_pending();
bool feature_open();
PerformanceStatus performance_status();
void capture_disk_space();
StartupCounter startup_counter;
MidiFiles midi_files;
DMAMEM cntrl::Controller::NavigationState invocation_navigation[9];
bool restoring_invocation = false;
unsigned pending_ui_operation = 0;
bool return_to_switches = false;
cntrl::PatchExecutor *visiting_invocation = nullptr;

#if CNTRL_ORIGINAL_FIRMWARE
bool request_stock(uint8_t pair, const cntrl::SongContinuation *continuation);
bool start_original_firmware();
#endif
DMAMEM cntrl::UsbSession usb_session;
class Board : public cntrl::KeyboardSink, public cntrl::Hardware, public cntrl::Replies, public cntrl::splash::Replies, public cntrl::midi::Sink, public cntrl::PatchOutputs
#if CNTRL_PANIC
    , public cntrl::PanicOutputs
#endif
{
public:
    struct RuntimeState { uint8_t brightness = 192; bool interface = false; } runtime;
#if CNTRL_ORIGINAL_FIRMWARE
    bool supports_function(unsigned function) const override { return function == 1; }
    bool stock_firmware(uint8_t pair, const cntrl::SongContinuation *continuation = nullptr) override { return request_stock(pair, continuation); }
#endif
#if CNTRL_SWITCHES
    cntrl::Switches switches{*this, *this, patch_sources};
    unsigned switch_capacity() const override { return 5; }
#endif
    bool configuration_locked() const override { return patch_work_pending()
#if CNTRL_SWITCHES
        || switches.open()
#endif
        ; }
    bool send_patch(unsigned owner, const char *output, const uint8_t *bytes, unsigned n) override {
        const auto *connection = cntrl::midi::find_output(output);
        return connection && available(output) && bridge_.owned_stream(owner, connection->route, bytes, n);
    }
    cntrl::midi::Bridge::Delivery patch_delivery(unsigned owner) const override { return bridge_.delivery(owner); }
    void cancel_patch(unsigned owner) override { bridge_.cancel_owner(owner); }
    const cntrl::midi::MidiState *midi_state() const override { return &cntrl::midi::Sink::state(); }
    bool play_midi_file(unsigned owner, const cntrl::midi::PlayMidiFilePatch &settings) override {
        return midi_files.play(owner, settings);
    }
    cntrl::midi::Bridge::Delivery midi_file_delivery(unsigned owner) const override { return midi_files.player.delivery(owner); }
    void cancel_midi_file(unsigned owner) override { if (owner == midi_files.player.owner()) midi_files.player.cancel(); }
    bool record_midi_file(const cntrl::midi::RecordMidiFilePatch &settings) override {
        return midi_files.record(settings, startup_counter, micros());
    }
    FLASHMEM bool state(unsigned operation, uint32_t value) override { return patch_state(operation, value); }
    bool stock(uint8_t pair, cntrl::PatchExecutor &invocation) override { return patch_stock(pair, invocation); }
    void validation(bool begin) override { app_controller().validation(begin); }
    void validation_context(cntrl::PatchExecutor &invocation, bool begin) override {
        if (begin && !restoring_invocation && !invocation.reconstructing())
            invocation_navigation[invocation.owner()] = app_controller().navigation();
        app_controller().validation(begin);
    }
    bool preview_state(unsigned operation, uint32_t value) override {
#if CNTRL_SWITCHES
        if (operation == 11 || operation == 12) return switches.open();
#endif
        return app_controller().preview_state(operation, value);
    }
    bool selection(unsigned operation, const cntrl::Document *&document, unsigned &node) override {
        if (!documents.document().valid() || !documents.document().app_layout().apps()) return false;
        return app_controller().selection(operation, document, node);
    }
    void selection_complete(unsigned operation) override { app_controller().selection_complete(operation); }
    cntrl::midi::Bridge::Delivery prepare_leave(cntrl::PatchExecutor &invocation) override {
#if CNTRL_SWITCHES
        if (switches.open()) {
            auto result = switches.prepare_leave(invocation);
            if (result != cntrl::midi::Bridge::Delivery::complete) return result;
        }
#else
        (void)invocation;
#endif
        return bridge_.idle() ? cntrl::midi::Bridge::Delivery::complete : cntrl::midi::Bridge::Delivery::pending;
    }
#if CNTRL_SWITCHES
    void enter_switches(const cntrl::Document &document, uint8_t held) { bridge_.configure(false); switches.enter(document, held); }
    void leave_switches() { bridge_.configure(runtime.interface); }
#endif
#if CNTRL_PANIC
    cntrl::Panic panic{*this, *this};
    bool panic_owns_midi = false;
    void leave_panic() {
        panic.leave();
        if (panic_owns_midi) bridge_.configure(runtime.interface);
        panic_owns_midi = false;
    }
    bool transmit(const char *output, const uint8_t *bytes) override {
        const auto *connection = cntrl::midi::find_output(output);
        return connection && submit(connection->route, {uint8_t(bytes[0] >> 4), {bytes[0], bytes[1], bytes[2]}});
    }
#endif
#if CNTRL_CABLE_TEST
    cntrl::CableTest cable_test{*this};
    void leave_cable_test() {
        if (cable_test.enabled()) bridge_.configure(runtime.interface);
        cable_test.leave();
    }
#endif
    bool supports_keyboard() const override { return true; }
    bool keyboard_report(const cntrl::KeyboardReport &report) override {
        keyboard_modifier_keys = report.modifiers;
        std::memcpy(keyboard_keys, report.keys, 6);
        return usb_keyboard_send() == 0;
    }
    bool keyboard_button(unsigned i, bool pressed) {
        if (pages_.page() != Pages::Page::keyboard || (i != 0 && i != 1 && i != 2)) return false;
        if (i == 2) {
            if (!pressed) keyboard_.release(0);
            else {
                const auto shortcut = pages_.keyboard_shortcut();
                pressed = shortcut.name[0] && keyboard_.press(0, shortcut);
            }
            pages_.keyboard_held(pressed);
        } else if (pressed) {
            keyboard_.release(0);
            pages_.keyboard_held(false);
            pages_.keyboard_select(i == 0);
        }
        return true;
    }
    void poll_keyboard() {
        keyboard_.poll(usb_configuration != 0, *this);
        if (!usb_configuration) pages_.keyboard_held(false);
    }
    bool supports_trs_mode(unsigned jack, cntrl::midi::TrsMode mode) const override {
        return jack < 4 && mode <= cntrl::midi::TrsMode::aux;
    }
    void configure_trs_ports(const uint8_t *modes) override {
        for (unsigned i = 0; i < 4; ++i)
            if (uint8_t(trs_ports.mode(i)) != modes[i]) bridge_.disconnect_output(2 + i);
        trs_ports.configure(modes);
    }
    void begin() {
        Serial1.begin(31250);
        Serial2.begin(31250);
        host.begin();
    }
    bool supports(const char *output) const override { return cntrl::midi::find_output(output) != nullptr; }
    bool available(const char *output) const {
        const auto *connection = cntrl::midi::find_output(output);
        if (!connection) return false;
        switch (connection->transport) {
        case cntrl::midi::OutputTransport::physical:
            if (connection->route >= 2 && !cntrl::midi::midi_output(trs_ports.mode(connection->route - 2))) return false;
#if CNTRL_CABLE_TEST
            return !cable_test.enabled();
#else
            return true;
#endif
        case cntrl::midi::OutputTransport::usb_device: return bridge_.usb_enabled(connection->route) && usb_configuration != 0;
        case cntrl::midi::OutputTransport::usb_host: return bridge_.usb_enabled(connection->route) && bool(host_midi);
        }
        return false;
    }
    bool supports_font(unsigned font, unsigned size) const override {
        return ShowGraphics::supports_font(font, size);
    }
    bool supports_font_asset(uint32_t key, unsigned size) const override { return font_files.supports(key, size); }
    void begin_fonts() override { font_assets::cache.begin(); }
    bool stage_font(uint32_t key, unsigned size) override { return font_files.stage(key, size); }
    void finish_fonts(bool commit) override {
        if (commit) renderer_.invalidate();
        font_assets::cache.finish(commit);
    }
    bool output_idle() const override { return bridge_.idle() && trs_ports.idle(); }
    void set_usb_midi(bool device, bool host) override {
        bridge_.configure_usb(device, host, millis());
    }
    void set_midi_interface(bool enabled) override {
        runtime.interface = enabled;
#if CNTRL_SWITCHES
        if (switches.open()) return;
#endif
#if CNTRL_PANIC
        if (panic_owns_midi) return;
#endif
#if CNTRL_CABLE_TEST
        if (cable_test.enabled()) return;
#endif
        bridge_.configure(enabled);
    }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    bool send(const char *output, const uint8_t *data, unsigned length) override {
        const auto *connection = cntrl::midi::find_output(output);
        if (!connection || length > 14 || !available(output)) return false;
        return bridge_.pedal(connection->route, data, length);
    }
    // Keep the transport backend in flash to leave RAM1 for runtime state.
    FLASHMEM bool write_packet(unsigned route, const cntrl::midi::Packet &p) override {
        if (!bridge_.usb_enabled(route)) return false;
        if (route >= 6 && route < 8) return usb_midi_try_write_packed(p.packed(route - 6));
        if (route == 8) return host_midi.try_write_packed(p.packed());
        unsigned n = p.size();
        if (cntrl::midi::physical_output(route)) {
            HardwareSerial *serial;
            switch (route) {
            case 0: serial = &Serial1; break;
            case 1: serial = &Serial2; break;
            default: return trs_ports.send(route - 2, p.bytes, n);
            }
            if (unsigned(serial->availableForWrite()) < n) return false;
            if (serial->write(p.bytes, n) != n) return false;
            return true;
        }
        return false;
    }
    void midi_status(unsigned route) {
        if (route == cntrl::midi::Bridge::destinations) {
            max_midi_us_ = max_scan_gap_ = 0; reply("CNTRL OK"); return;
        }
        if (route >= cntrl::midi::Bridge::destinations) { reply("CNTRL ERROR midi-route"); return; }
        const auto &c = bridge_.counters(route);
        char response[400];
        unsigned init_errors = 0;
        if (documents.document().valid()) for (unsigned i = 0; i < 4; ++i)
            if (uint8_t(trs_ports.mode(i)) != documents.document().trs_modes()[i]) init_errors |= 1u << i;
        std::snprintf(response, sizeof(response),
            "CNTRL MIDI %u %u %lu %lu %lu %lu %u %lu %lu %lu %lu %u",
            unsigned(bridge_.enabled()), route, (unsigned long)c.received,
            (unsigned long)c.transmitted, (unsigned long)c.dropped, (unsigned long)c.aborted,
            c.high_water, (unsigned long)max_midi_us_, (unsigned long)max_scan_gap_,
            (unsigned long)scan_count_, (unsigned long)display_count_, init_errors);
        reply(response);
    }
    void scanned(uint32_t gap) {
        if (gap > max_scan_gap_) max_scan_gap_ = gap;
        ++scan_count_;
    }
    void show(unsigned pane, const cntrl::Pane &content) override {
        if (content.performance) {
            if (!app_visible() || feature_open()) return;
            const auto layout = documents.document().app_layout();
            const int destination = layout.app_window(rendering_app(), pane, pages_.windows());
            if (destination < 0) return;
            if (!performance_visible_) { pages_.revisit(); enter_page(); }
            performance_visible_ = true;
            if (pane == 0) { pending_patch_ = content.pending_patch; apply_brightness(millis()); }
            renderer_.show(unsigned(destination), content);
            return;
        }
        if (pane == 0) { pending_patch_ = content.pending_patch; apply_brightness(millis()); }
        if (pane == 2) {
            if (performance_visible_) leave_page();
            performance_visible_ = false;
        }
        renderer_.show(pane, content);
    }
    void configure_apps(const cntrl::Document &document) override {
        pages_.layout(document.valid() ? document.app_layout() : cntrl::AppLayout{});
        ::configure_apps(document);
        performance_visible_ = true;
        if (document.valid()) enter_page();
    }
    bool refresh_apps() override { refresh_app(); return true; }
    void blank_performance() {
        cntrl::Pane empty;
        empty.graphics.background = 0;
        for (unsigned i = 0; i < 3; ++i) renderer_.show(i, empty);
        pending_patch_ = -1;
        performance_visible_ = true;
        pages_.revisit();
    }
    void select_trs_jack() {
        auto &page = pages_;
        page.candidate = trs_ports.mode(page.jack);
        page.port_status(page.candidate, documents.document().trs_modes()[page.jack] != uint8_t(page.candidate) ? "Driver failed" : nullptr);
    }
    bool trs_button(unsigned i) {
        if (pages_.page() != Pages::Page::ports || i > 1) return false;
        if (i == 0) {
            pages_.jack = (pages_.jack + 1) % 4;
            select_trs_jack();
        } else pages_.next_port_mode();
        return true;
    }
    void enter_page() {
        if (pages_.page() == Pages::Page::ports && documents.document().valid()) select_trs_jack();
#if CNTRL_CABLE_TEST
        if (pages_.page() == Pages::Page::cable) cable_test.enter();
#endif
#if CNTRL_PANIC
        if (pages_.page() == Pages::Page::panic) panic.enter(runtime.brightness);
#endif
    }
    void leave_page() {
        keyboard_.clear(); pages_.keyboard_held(false);
        if (pages_.page() == Pages::Page::expression) expression_.cancel();
#if CNTRL_CABLE_TEST
        if (pages_.page() == Pages::Page::cable) leave_cable_test();
#endif
#if CNTRL_PANIC
        if (pages_.page() == Pages::Page::panic) leave_panic();
#endif
    }
    unsigned clock_available() const {
        unsigned mask = 0;
        for (const auto &c : cntrl::midi::output_connections)
            if (available(c.name)) mask |= 1u << c.route;
        for (const auto &c : cntrl::midi::usb_output_connections)
            if (available(c.name)) mask |= 1u << c.route;
        return mask;
    }
    bool stop_clock() {
        pages_.generator.stop();
        pages_.generator.poll(micros(), clock_available(), *this);
        return !pages_.generator.stopping();
    }
    bool clock_control(unsigned button, bool pressed) {
        if (!pages_.clock_page()) return false;
        if (pressed) {
            if (button == 5) browse_page(held_controls & (1u << 3));
            else pages_.clock_button(button, micros(), clock_available());
        }
        return true;
    }
    FLASHMEM void configure_page_settings(const cntrl::PageSettings &settings) override {
        leave_page();
        expression_.configure(settings);
        pages_.configure(settings);
        bridge_.clock_owner(pages_.clock_owner() | midi_files.player.clock_outputs());
    }
    void browse_page(bool reverse) override {
        if (!documents.document().valid()) return;
        if (pages_.page() == Pages::Page::confirm_firmware
            && (held_controls & (1u << 2))) return;
        leave_page();
        if (pages_.browse(reverse)) {
#if CNTRL_ORIGINAL_FIRMWARE
            if (!start_original_firmware()) error("Firmware switch failed");
#else
            error("Stock firmware unavailable");
#endif
        } else {
            renderer_.transition(reverse, documents.document().scroll_ms(), pages_.windows() == 1 ? 2 : -1);
            refresh_app();
        }
        if (pages_.clock_page()) app_controller().cancel_gesture();
        enter_page();
    }
    void tap_tempo(uint32_t now) override {
        if (!documents.document().valid()) return;
        auto &page = pages_;
        if (page.page() == Pages::Page::ports) {
            auto active = trs_ports.mode(page.jack);
            if (!output_idle() || configuration_locked() || documents.busy() || documents.dirty()) page.port_status(active, "Busy");
            else if (active == page.candidate && documents.document().trs_modes()[page.jack] == uint8_t(active)) page.port_status(active, "Already set");
            else if (!documents.save_trs_mode(page.jack, page.candidate, now)) page.port_status(active, "Cannot apply");
            else { port_save_pending_ = true; page.port_status(active, "Saving"); }
            return;
        }
#if CNTRL_CABLE_TEST
        if (pages_.page() == Pages::Page::cable) {
            if (!cable_test.enabled() && (patch_work_pending() || !output_idle() || documents.busy() || !stop_clock())) return;
            app_controller().cancel_gesture();
            cable_test.toggle(now);
            if (cable_test.enabled())
                for (const auto &output : cntrl::midi::output_connections) forget_output(output.route);
            bridge_.configure(cable_test.enabled() ? false : runtime.interface);
            return;
        }
#endif
#if CNTRL_PANIC
        if (pages_.page() == Pages::Page::panic) {
            if (!panic_owns_midi && (patch_work_pending() || !output_idle() || documents.busy() || !stop_clock())) return;
            app_controller().cancel_gesture();
            if (!panic_owns_midi) bridge_.configure(false);
            panic_owns_midi = true;
            panic.button(true, now);
            return;
        }
#endif
        if (pages_.page() == Pages::Page::expression) {
            expression_.calibrate(trs_ports.expression_pin(expression_.selected()) >= 0);
            return;
        }
        pages_.tap(now);
    }
    void set_target_pulse(unsigned period, uint8_t low, uint8_t high, unsigned delay) override {
        throb_.configure(period, low, high, delay);
    }
    FLASHMEM void transition(bool undo, std::optional<unsigned> duration_ms, int pane, bool vertical = false, bool panorama = true) override {
        if (!app_visible() || feature_open()) return;
        const unsigned occupied = pages_.windows();
        if (occupied == 3) return;
        if (pane >= 0) {
            pane = documents.document().app_layout().app_window(rendering_app(), unsigned(pane), occupied);
            if (pane < 0) return;
        } else if (occupied == 2) pane = 0;
        renderer_.transition(undo, duration_ms, pane, occupied == 1, vertical, panorama);
    }
#ifdef CNTRL_DIAGNOSTICS
    void timing_mode(bool enabled) {
        if (enabled) {
            scroll_timing.arm();
            analogWrite(6, runtime.brightness);
        }
        else {
            scroll_timing.stop(); renderer_.invalidate();
            documents.redraw();
            analogWrite(6, applied_brightness_);
        }
    }

#endif
    void set_brightness(uint8_t value, bool temporary = false) override {
        brightness_feedback_ = temporary;
        if (temporary) feedback_brightness_ = value;
        else { runtime.brightness = value; app_brightness(value); }
        if (temporary) splash_session.stop();
        apply_brightness(millis());
    }
    void set_splash(bool enabled, uint32_t idle_ms) override {
        splash_session.configure(enabled, idle_ms, millis());
    }
    void set_movies(const uint8_t *startup, unsigned startup_count, const uint8_t *idle, unsigned idle_count) override {
        splash_session.movies(startup, startup_count, idle, idle_count);
    }
    void set_animation_color_cycle(bool enabled) override {
        splash_panels.set_color_cycle(enabled);
    }
    void error(const char *message) override {
        pending_patch_ = -1;
        renderer_.error(message);
    }
    bool reply_ready() const override { return usb_session.reply_ready(); }
    void reply(const char *text) override { usb_session.reply(text); }
    void poll() {
        host.Task();
        bridge_.host_connected(bool(host_midi));
        uint32_t begin = micros(), now = millis();
        bridge_.connected(usb_configuration != 0, now);
        if (!documents.document().valid()) pages_.generator.stop();
        unsigned clock_routes = clock_available();
        pages_.clock_connections(clock_routes);
        bridge_.clock_owner(pages_.clock_owner() | midi_files.player.clock_outputs());
        pages_.generator.poll(micros(), clock_routes & ~midi_files.player.clock_outputs(), *this);
        // Separate source limits plus an aggregate time budget. Rotate the
        // first source, so a saturated cable cannot starve later sources.
        for (unsigned i = 0; i < 3 && uint32_t(micros() - begin) < 180; ++i) {
            unsigned source = (source_cursor_ + i) % 3;
            for (unsigned n = 0; n < 64 && uint32_t(micros() - begin) < 180; ++n) {
                if (source == 0) {
                    uint32_t packet = usb_midi_read_message();
                    if (!packet) break;
                    if (!bridge_.usb_enabled(6)) continue;
                    midi_files.receive(6, cntrl::midi::Packet{uint8_t(packet & 15), {uint8_t(packet >> 8), uint8_t(packet >> 16), uint8_t(packet >> 24)}}, micros());
                    if (((packet >> 8) & 255) == 0xf8) pages_.clock((packet >> 4) & 15, micros());
#if CNTRL_CABLE_TEST
                    if (cable_test.enabled()) continue;
#endif
#if CNTRL_PANIC
                    if (panic_owns_midi) continue;
#endif
                    const auto *output = cntrl::midi::physical_output((packet >> 4) & 15);
                    if (output && available(output->name)) bridge_.usb(packet, now);
                } else {
                    HardwareSerial &serial = source == 1 ? Serial1 : Serial2;
                    int byte = serial.read(); if (byte < 0) break;
                    midi_files.receive(source - 1, uint8_t(byte), micros());
                    if (byte == 0xf8) pages_.clock(source + 15, micros());
#if CNTRL_CABLE_TEST
                    if (cable_test.enabled()) { cable_test.receive(source - 1, uint8_t(byte), now); continue; }
#endif
                    bridge_.receive(source + 5, uint8_t(byte), now);
                }
            }
        }
        source_cursor_ = (source_cursor_ + 1) % 3;
        // Existing USB-host input is deliberately not bridged.
        if (host_midi.read() && bridge_.usb_enabled(8)) {
            unsigned type = host_midi.getType();
            if (type == 0xf8) pages_.clock(18 + host_midi.getCable(), micros());
            if (type == 0xf0) {
                for (unsigned i = 0; i < host_midi.getSysExArrayLength(); ++i) midi_files.receive(8, host_midi.getSysExArray()[i], micros());
            } else {
                uint8_t cin = type < 0xf0 ? type >> 4 : type == 0xf1 || type == 0xf3 ? 2 : type == 0xf2 ? 3 : type == 0xf6 ? 5 : 15;
                cntrl::midi::Packet packet{cin, {uint8_t(type < 0xf0 ? type | (host_midi.getChannel() - 1) : type), host_midi.getData1(), host_midi.getData2()}};
                midi_files.receive(8, packet, micros());
            }
        }
#if CNTRL_SWITCHES
        switches.tick(now);
#endif
#if CNTRL_PANIC
        if (panic_owns_midi) {
            panic.tick(now, bridge_.handoff(*this));
            if (!panic.active()) { bridge_.configure(runtime.interface); panic_owns_midi = false; }
        }
        else
#endif
#if CNTRL_CABLE_TEST
        if (cable_test.enabled()) { if (bridge_.handoff(*this)) cable_test.tick(now); }
        else
#endif
        bridge_.service(*this, now, midi_files.player.sysex_outputs());
        // Flush only already-buffered packets; this operation never waits.
        usb_midi_flush_output();
        // File I/O follows delivery of accepted button actions.
        midi_files.recorder.poll(micros());
        if (midi_files.recorder.failed() && !recording_failed_) error("MIDI recording write failed");
        recording_failed_ = midi_files.recorder.failed();
        if (bridge_.sysex_ready(midi_files.player.sysex_outputs())) midi_files.player.poll(micros(), clock_available(), *this);
        usb_midi_flush_output();
        uint32_t elapsed = micros() - begin;
        if (elapsed > max_midi_us_) max_midi_us_ = elapsed;
        if (usb_session.output_pending() && Serial.availableForWrite() > 0) {
            unsigned n = std::strlen(usb_session.output());
            unsigned room = Serial.availableForWrite();
            if (n > room) n = room;
            usb_session.sent(Serial.write(usb_session.output(), n));
        }
    }
    void draw() {
        if (splash_session.take_redraw()) renderer_.invalidate();
        renderer_.draw(micros());
    }
    void load_expression_calibrations() { expression_.load_calibrations(); }
    bool expression_button(unsigned i) {
        if (pages_.page() != Pages::Page::expression || i > 1) return false;
        if (i == 0) expression_.next_jack(); else expression_.cancel();
        return true;
    }
    void poll_expression(uint32_t now) {
        if (!documents.document().valid()) return;
        expression_.poll(now, [](unsigned i) { return trs_ports.expression_pin(i); },
            [this](const char *out) {
#if CNTRL_PANIC
                if (panic_owns_midi) return false;
#endif
#if CNTRL_CABLE_TEST
                if (cable_test.enabled()) return false;
#endif
                return available(out) && !documents.busy() && !documents.dirty();
            },
            [this](const char *out, const uint8_t *bytes) {
                return !patch_work_pending() && output_idle() && send(out, bytes, 3);
            });
        if (pages_.page() == Pages::Page::expression && uint32_t(now - expression_refresh_) >= 80) {
            expression_refresh_ = now;
            char title[40], position[40], prompt[40];
            expression_.status(trs_ports.expression_pin(expression_.selected()) >= 0, title, position, prompt);
            pages_.expression_status(title, position, prompt);
        }
    }
    void display(uint32_t now, bool transfer_busy) {
        if (switch_scan_due()) return;
#if CNTRL_CABLE_TEST
        transfer_busy = transfer_busy || cable_test.enabled();
#endif
#if CNTRL_PANIC
        transfer_busy = transfer_busy || panic_owns_midi;
#endif
        if (port_save_pending_ && !documents.busy()) {
            port_save_pending_ = false;
            if (documents.dirty()) {
                documents.edit_cancel();
                pages_.port_status(trs_ports.mode(pages_.jack), "Save failed");
            }
        }
        pages_.time(now, micros(), held_controls & (1u << 2));
        ++display_count_;
#ifdef CNTRL_DIAGNOSTICS
        if (scroll_timing.active()) { if (!display_dma_pending()) scroll_timing.poll(micros()); return; }
#endif
        if (!display_dma_pending()) splash_visible_ = splash_session.poll(now, renderer_.ready(), transfer_busy);
        apply_brightness(now);
#if CNTRL_CABLE_TEST
        if (pages_.page() == Pages::Page::cable && cable_test.take_redraw())
            pages_.revisit();
#endif
#if CNTRL_PANIC
        if (pages_.page() == Pages::Page::panic && panic.take_redraw())
            pages_.revisit();
#endif
        if (!splash_visible_ && !feature_open() && documents.document().valid()
            && pages_.windows() && pages_.due(now)) {
            // Prepare the incoming card during the renderer's bounded transition preparation.
            PerformanceStatus status = pages_.page() == Pages::Page::status ? performance_status() : PerformanceStatus{};
            const cntrl::Pane content = pages_.view(now, micros(), status
#if CNTRL_CABLE_TEST
                , cable_test.status()
#else
                , {}
#endif
#if CNTRL_PANIC
                , panic.status()
#else
                , cntrl::Panic::Status::ready
#endif
            );
            for (unsigned i = 0; i < pages_.windows(); ++i)
                renderer_.show(3 - pages_.windows() + i, Pages::window_view(content, i, pages_.windows()));
        }
        if (!splash_visible_) draw();
    }
private:
    void apply_brightness(uint32_t now) {
        renderer_.target_brightness(throb_.brightness(
            !splash_visible_ && !brightness_feedback_ ? pending_patch_ : -1, now, 128, held_controls));
        uint8_t brightness = brightness_feedback_ ? feedback_brightness_ : splash_session.brightness(runtime.brightness, now);
        if (brightness != applied_brightness_) {
            analogWrite(6, brightness); applied_brightness_ = brightness;
        }
    }
    PixelThrob throb_;
    ExpressionPedals expression_;
    uint32_t expression_refresh_ = 0;
    cntrl::Keyboard keyboard_;
    Pages pages_;
    bool port_save_pending_ = false, recording_failed_ = false;
    bool performance_visible_ = false;
    int pending_patch_ = -1;
    bool splash_visible_ = false;
    bool brightness_feedback_ = false;
    uint8_t feedback_brightness_ = 255;
    int applied_brightness_ = -1;
    cntrl::midi::Bridge bridge_;
    unsigned source_cursor_ = 0;
    uint32_t max_midi_us_ = 0, max_scan_gap_ = 0, scan_count_ = 0, display_count_ = 0;
    DisplayRenderer renderer_;
};

class Card : public cntrl::SetupStorage {
public:
    bool ready = false;
    bool available() const override { return ready && !SD.sdfs.card()->errorCode(); }
    void choose_revision(const char *name, uint64_t number) override {
        std::snprintf(directory_, sizeof(directory_), "/cntrl/history/%s/%08lx%08lx", name, (unsigned long)(number >> 32), (unsigned long)(number & 0xffffffff));
        revision_ = true;
    }
    void revision_scan(const char *name, uint64_t before, bool records_only) override {
        char path[96]; std::snprintf(path, sizeof(path), "/cntrl/history/%s", name);
        history_directory_.close(); history_directory_ = SD.sdfs.open(path, O_RDONLY);
        history_before_ = before; history_records_ = records_only;
    }
    bool revision_scan_step(uint64_t &best) override {
        FsFile entry; char candidate[32];
        if (!history_directory_ || !entry.openNext(&history_directory_, O_RDONLY)) {
            history_directory_.close(); return true;
        }
        if (entry.isDirectory() && entry.getName(candidate, sizeof(candidate)) && std::strlen(candidate) == 16) {
            char *end; uint64_t n = std::strtoull(candidate, &end, 16);
            if (!*end && n < history_before_ && n > best) {
                bool record = true;
                if (history_records_) { FsFile file; record = file.open(&entry, "0.bin", O_RDONLY) && file.size() >= 208; }
                if (record) best = n;
            }
        }
        entry.close(); return false;
    }
    bool remove_revision(const char *name, uint64_t number) override {
        choose_revision(name, number); char path[144]; filename(0, path);
        if (SD.sdfs.exists(path) && !SD.sdfs.remove(path)) return false;
        return true; // Keep the revision number reserved after deletion.
    }
    void choose(const char *name) override {
        revision_ = false;
        std::snprintf(directory_, sizeof(directory_), name[0] ? "/cntrl/setups/%s" : "/cntrl/selection", name);
    }
    // Runtime setup enumeration belongs to the indexed bundle, not SD folders.
    bool next(const char *, char *) override { return false; }
    void catalogue_begin() override {}
    bool catalogue_step(char *name) override { name[0] = 0; return true; }
    unsigned size(unsigned slot) override {
        if (!ready) return 0;
        char path[144]; filename(slot, path);
        auto file = SD.sdfs.open(path, O_RDONLY);
        return file ? unsigned(file.size()) : 0;
    }
    bool read(unsigned slot, unsigned offset, uint8_t *data, unsigned length) override {
        if (!ready) return false;
        char path[144]; filename(slot, path);
        auto file = SD.sdfs.open(path, O_RDONLY);
        return file && file.seekSet(offset) && file.read(data, length) == int(length);
    }
    bool begin(unsigned slot) override {
        if (!ready) return false;
        if (!SD.sdfs.exists(directory_) && !SD.sdfs.mkdir(directory_, true)) return false;
        char path[144]; filename(slot, path);
        file_ = SD.sdfs.open(path, O_RDWR | O_CREAT | (revision_ ? O_EXCL : O_TRUNC));
        if (file_) return true;
        file_.close();
        return false;
    }
    bool write(const uint8_t *data, unsigned length) override {
        return file_ && file_.write(data, length) == length;
    }
    bool finish() override {
        if (!file_) return false;
        bool synced = file_.sync();
        file_.close();
        return synced; // Documents also reopens and verifies all bytes.
    }
    bool flush() override { return file_ && file_.sync(); }
    bool patch(unsigned offset, const uint8_t *data, unsigned length) override {
        if (!file_) return false;
        auto position = file_.curPosition();
        return file_.seekSet(offset) && file_.write(data, length) == length && file_.seekSet(position);
    }
private:
    FsFile file_, history_directory_;
    uint64_t history_before_ = 0;
    bool history_records_ = false;
    bool revision_ = false;
    char directory_[128] = {};
    void filename(unsigned slot, char *path) const { std::snprintf(path, 144, "%s/%u.bin", directory_, slot); }
};

Board board;
Card card;
cntrl::BundledStorage setup_storage(card, board);
cntrl::Controller primary_controller(board, board, patch_sources);
// Document bytes are allocated from RAM2's heap as needed.
DMAMEM cntrl::Documents documents(setup_storage, board, primary_controller, board);
cntrl::StoredPatchSources patch_sources(setup_storage.setups());
cntrl::BundleTransfer bundle_transfer(setup_storage, documents, board, board);
alignas(cntrl::Controller) DMAMEM unsigned char extra_apps[cntrl::AppLayout::max_apps - 1][sizeof(cntrl::Controller)];
cntrl::AppLayout app_layout;
unsigned visible_app = 0, processing_app = cntrl::AppLayout::max_apps;
cntrl::Controller &app_at(unsigned index) {
    return index ? *std::launder(reinterpret_cast<cntrl::Controller *>(extra_apps[index - 1])) : primary_controller;
}
unsigned rendering_app() { return processing_app < cntrl::AppLayout::max_apps ? processing_app : visible_app; }
cntrl::Controller &app_controller() { return app_at(rendering_app()); }
bool app_visible() { return app_layout.apps() && rendering_app() == visible_app; }
bool apps_busy() {
    for (unsigned i = 0; i < app_layout.apps(); ++i) if (app_at(i).busy()) return true;
    return primary_controller.busy();
}
void app_brightness(uint8_t value) {
    for (unsigned i = 0; i < (app_layout.apps() ? app_layout.apps() : 1); ++i)
        app_at(i).runtime_brightness(value);
}
FLASHMEM void configure_apps(const cntrl::Document &document) {
    app_layout = document.valid() ? document.app_layout() : cntrl::AppLayout{};
    visible_app = 0;
    for (unsigned i = 1; i < cntrl::AppLayout::max_apps; ++i) {
        processing_app = i;
        if (i < app_layout.apps()) app_at(i).activate(document, false);
        else app_at(i).deactivate();
    }
    processing_app = cntrl::AppLayout::max_apps;
    board.blank_performance();
    configure_app_menu();
}
void refresh_app() {
    const unsigned previous = processing_app;
    processing_app = visible_app;
    board.blank_performance();
    if (app_layout.apps()) app_at(visible_app).refresh();
    processing_app = previous;
}
void tick_apps(uint32_t now) {
    for (unsigned i = 0; i < (app_layout.apps() ? app_layout.apps() : 1); ++i) {
        processing_app = i;
        cntrl::App &app = app_at(i);
        app.tick(now);
    }
    processing_app = cntrl::AppLayout::max_apps;
}
cntrl::SetupUi setup_ui(documents, board);
class SongFiles : public cntrl::Storage {
public:
    unsigned size(unsigned slot) override { return open(slot) ? unsigned(readers_[slot].size()) : 0; }
    bool read(unsigned slot, unsigned offset, uint8_t *data, unsigned n) override {
        return open(slot) && readers_[slot].seekSet(offset) && readers_[slot].read(data, n) == int(n);
    }
    bool begin(unsigned slot) override {
        if (!card.ready || (!SD.sdfs.exists("/cntrl/songs") && !SD.sdfs.mkdir("/cntrl/songs", true))) return false;
        readers_[slot].close();
        char path[40]; filename(slot, path); writer_ = SD.sdfs.open(path, O_RDWR | O_CREAT | O_TRUNC); return bool(writer_);
    }
    bool write(const uint8_t *data, unsigned n) override { return writer_ && writer_.write(data, n) == n; }
    bool finish() override { bool good = writer_ && writer_.sync(); writer_.close(); return good; }
private:
    FsFile writer_, readers_[4];
    static void filename(unsigned slot, char *path) { std::snprintf(path, 40, "/cntrl/songs/%u.bin", slot); }
    bool open(unsigned slot) {
        if (!card.ready) return false;
        if (!readers_[slot]) { char path[40]; filename(slot, path); readers_[slot] = SD.sdfs.open(path, O_RDONLY); }
        return bool(readers_[slot]);
    }
};
extern cntrl::Songs songs;
class SongSources : public cntrl::SongResolver, public cntrl::SongCapture {
public:
    cntrl::PatchExecutor executor{board, board, patch_sources, 5};
    ~SongSources() override { executor.cancel(); }
    void tick(uint32_t now) {
        executor.tick(now);
        if (embedded_.data && !executor.busy()) {
            embedded_.clear(); embedded_document_ = cntrl::Document();
        }
    }
    const cntrl::SongContinuation &context() const { return context_; }
    void recalled() override { app_controller().cancel_gesture(); }
    bool capture(cntrl::Song &song, bool reference) override {
        const auto &d = documents.document();
        if (!d.valid() || !app_layout.apps() || !d.patches() || documents.busy() || documents.dirty() || patch_work_pending()) return false;
        unsigned index = app_controller().selected_patch();
        const uint8_t *p = d.patch(index);
        std::snprintf(song.name, 64, "%s", reinterpret_cast<const char *>(p + 12));
        std::snprintf(song.label, 64, "%s", song.name);
        song.node = d.browser_root(index);
        if (reference) {
            const char *key = nullptr;
            const uint8_t *node = d.node(song.node);
            if (node[0] == 6 && cntrl::u16(node + 4) == 65535) key = reinterpret_cast<const char *>(node + 6);
            for (unsigned i = 0; !key && i < d.named_patches(); ++i)
                if (cntrl::u16(d.named_patch(i) + 64) == song.node) key = reinterpret_cast<const char *>(d.named_patch(i));
            if (!key) return false;
            std::snprintf(song.document, 64, "%s", documents.selected());
            std::snprintf(song.key, 64, "%s", key); song.source = 0xffffffff;
        } else song.source = 0xfffffffe;
        return true;
    }
    bool capture_source(const cntrl::Document *&source) override {
        if (documents.busy() || documents.dirty() || apps_busy() || songs.executing()) return false;
        source = &documents.document(); return source->valid();
    }
    bool revision(const cntrl::SongSelection &song, uint32_t &crc) override {
        patch_sources.begin();
        const cntrl::Document *source = nullptr;
        int index = setup_storage.setups().index(song.document);
        bool valid = song.document[0] && index >= 0 && patch_sources.acquire(uint16_t(index), source, crc);
        patch_sources.end(); return valid;
    }
    bool activate(const cntrl::SongSelection &song, const cntrl::SongContinuation &context, bool resume) override {
        if (apps_busy() || executor.busy() || documents.busy() || documents.dirty()) return false;
        embedded_.clear();
        patch_sources.begin();
        const cntrl::Document *source = nullptr;
        unsigned node = song.node;
        uint32_t revision = 0;
        bool valid = false;
        if (song.document[0]) {
            int index = setup_storage.setups().index(song.document);
            valid = index >= 0 && patch_sources.acquire(uint16_t(index), source, revision);
            if (valid) {
                int root = source->named_patch(song.key);
                valid = root >= 0; node = valid ? unsigned(root) : 0;
            }
        } else {
            unsigned size = songs.source_size(song.source);
            if (size && embedded_.resize(size) && songs.read_source(song.source, 0, embedded_.data, size)
                && embedded_document_.load(embedded_.data, size, board)) {
                source = &embedded_document_; valid = true;
            }
        }
        context_ = context;
        if (valid) {
            auto previous = app_controller().navigation();
            if (!resume) app_controller().external_selection();
            valid = resume ? node == context.node && executor.resume(*source, node, context.completed, context.sources_crc)
                           : executor.start(*source, node);
            if (!valid && !resume) app_controller().navigation(previous);
        }
        patch_sources.end(); return valid;
    }
    cntrl::SongExecution execution_status() const override {
        switch (executor.status()) {
        case cntrl::PatchExecutor::Status::pending: return cntrl::SongExecution::pending;
        case cntrl::PatchExecutor::Status::failed: return cntrl::SongExecution::failed;
        case cntrl::PatchExecutor::Status::unknown: return cntrl::SongExecution::unknown;
        case cntrl::PatchExecutor::Status::suspended: return cntrl::SongExecution::suspended;
        default: return cntrl::SongExecution::completed;
        }
    }
private:
    cntrl::SongContinuation context_;
    cntrl::Document embedded_document_;
    cntrl::MemoryBuffer embedded_;
};
SongFiles song_files;
SongSources song_sources;
DMAMEM cntrl::Songs songs(song_files, song_sources);
DMAMEM cntrl::SongsTransfer songs_transfer(songs, board);
cntrl::SongsUi songs_ui(songs, board, song_sources);
enum ModuleIndex { setups_module, songs_module,
#if CNTRL_SWITCHES
    switches_module,
#endif
    app_modules,
};
const char *module_names[app_modules + cntrl::AppLayout::max_apps] = {"Setups", "Songs and setlists",
#if CNTRL_SWITCHES
    "Switches",
#endif
};
#if CNTRL_ORIGINAL_FIRMWARE
struct FirmwareVisitRecord {
    uint32_t magic = 0x46564932, token = 0, setup_crc = 0;
    char setup[64] = {};
    cntrl::SongContinuation continuation;
    cntrl::Controller::NavigationState initial_navigation, return_navigation;
    Board::RuntimeState runtime;
    uint32_t ui_operation = 0;
    uint32_t app = 0;
    uint32_t crc = 0;
};
DMAMEM FirmwareVisitRecord firmware_visit;
FsFile firmware_visit_file;
unsigned firmware_visit_stage = 0;
unsigned firmware_visit_offset = 0;
uint8_t firmware_exit_pair = 33;
bool firmware_resume_checked = false;
bool append_boot_record(uint32_t value) {
    auto words = reinterpret_cast<const volatile uint32_t *>(0x60084000);
    auto state = inspect_boot_selection(words);
    if (!state.next) return false;
    uint32_t record[] = {value, ~value};
    eepromemu_flash_write(reinterpret_cast<void *>(0x60084000 + state.next * 8), record, sizeof(record));
    return words[state.next * 2] == value && words[state.next * 2 + 1] == ~value;
}
bool request_stock(uint8_t pair, const cntrl::SongContinuation *continuation) {
    if (firmware_visit_stage || !valid_exit_pair(pair) || documents.busy() || documents.dirty() || songs_transfer.busy() || !board.output_idle()) return false;
    for (unsigned i = 0; i < app_layout.apps(); ++i)
        if (i != rendering_app() && app_at(i).busy()) return false;
    auto state = inspect_boot_selection(reinterpret_cast<const volatile uint32_t *>(0x60084000));
    if (!can_enter_stock(state) || (continuation && !card.ready)) return false;
    if (!board.stop_clock()) return false;
    firmware_visit = FirmwareVisitRecord(); firmware_exit_pair = pair; firmware_visit_offset = 0;
    if (continuation) {
        firmware_visit.token = state.next; firmware_visit.continuation = *continuation;
        firmware_visit.initial_navigation = invocation_navigation[visiting_invocation->owner()];
        firmware_visit.return_navigation = app_controller().navigation();
        firmware_visit.runtime = board.runtime; firmware_visit.ui_operation = pending_ui_operation;
        firmware_visit.app = rendering_app();
        std::snprintf(firmware_visit.setup, 64, "%s", documents.selected());
        const auto &document = documents.document();
        firmware_visit.setup_crc = document.valid() ? cntrl::checksum(document.data(), document.length()) : 0;
        firmware_visit.crc = cntrl::checksum(reinterpret_cast<const uint8_t *>(&firmware_visit), sizeof(firmware_visit) - 4);
        firmware_visit_stage = 1;
    } else firmware_visit_stage = 5;
    return true;
}
bool start_original_firmware() { return app_controller().executor().invoke(cntrl::StockFirmwarePatch{33}); }
void poll_firmware_visit(bool released) {
    auto fail = [&]() {
        firmware_visit_file.close(); firmware_visit_stage = 0; songs.cancel_function();
        if (visiting_invocation) visiting_invocation->cancel();
        visiting_invocation = nullptr; board.error("Firmware switch failed");
    };
    if (firmware_visit_stage >= 8) {
        static uint32_t token = 0;
        static bool valid = false;
        switch (firmware_visit_stage) {
        case 8:
            token = firmware_visit.token;
            firmware_visit_file = SD.sdfs.open("/cntrl/firmware-visit.bin", O_RDONLY);
            valid = firmware_visit_file && firmware_visit_file.size() == sizeof(firmware_visit);
            firmware_visit_offset = 0;
            break;
        case 9: {
            unsigned n = sizeof(firmware_visit) - firmware_visit_offset; if (n > 512) n = 512;
            valid = valid && firmware_visit_file.read(reinterpret_cast<uint8_t *>(&firmware_visit) + firmware_visit_offset, n) == int(n);
            firmware_visit_offset += n;
            if (valid && firmware_visit_offset < sizeof(firmware_visit)) return;
            firmware_visit_file.close(); break;
        }
        case 10: {
            const auto &document = documents.document();
            valid = valid && firmware_visit.magic == 0x46564932 && firmware_visit.token == token
                && firmware_visit.crc == cntrl::checksum(reinterpret_cast<const uint8_t *>(&firmware_visit), sizeof(firmware_visit) - 4)
                && !std::strncmp(firmware_visit.setup, documents.selected(), 64)
                && firmware_visit.setup_crc == (document.valid() ? cntrl::checksum(document.data(), document.length()) : 0);
            break;
        }
        case 11:
            if (!append_boot_record(visit_consumed | token)) { fail(); return; }
            break;
        case 12: {
            auto continuation = firmware_visit.continuation;
            firmware_visit_stage = 0;
            if (firmware_visit.app >= app_layout.apps()) { board.error("App return unavailable"); return; }
            visible_app = firmware_visit.app;
            if (!valid || !app_controller().navigation(firmware_visit.initial_navigation)) { board.error("Patch return unavailable"); return; }
            restoring_invocation = true;
            bool resumed = continuation.caller == 1 ? songs.resume(continuation)
                : app_controller().executor().resume(documents.document(), continuation.node, continuation.completed, continuation.sources_crc);
            restoring_invocation = false;
            if (!resumed || !app_controller().navigation(firmware_visit.return_navigation)) {
                app_controller().executor().cancel(); song_sources.executor.cancel();
                board.error("Patch return unavailable"); return;
            }
            invocation_navigation[app_controller().executor().owner()] = firmware_visit.initial_navigation;
            board.set_brightness(firmware_visit.runtime.brightness);
            board.set_midi_interface(firmware_visit.runtime.interface);
            pending_ui_operation = firmware_visit.ui_operation;
            if (continuation.caller == 1) songs_ui.perform();
            else return_to_switches = continuation.caller == 2;
            return;
        }
        }
        ++firmware_visit_stage; return;
    }
    if (firmware_visit_stage) {
        switch (firmware_visit_stage) {
        case 1:
            firmware_visit_file = SD.sdfs.open("/cntrl/firmware-visit.bin", O_RDWR | O_CREAT | O_TRUNC);
            if (!firmware_visit_file) { fail(); return; }
            break;
        case 2: {
            unsigned n = sizeof(firmware_visit) - firmware_visit_offset; if (n > 512) n = 512;
            if (firmware_visit_file.write(reinterpret_cast<const uint8_t *>(&firmware_visit) + firmware_visit_offset, n) != n) { fail(); return; }
            firmware_visit_offset += n;
            if (firmware_visit_offset < sizeof(firmware_visit)) return;
            break;
        }
        case 3:
            if (!firmware_visit_file.sync()) { fail(); return; }
            if (!firmware_visit_file.seekSet(0)) { fail(); return; }
            firmware_visit_offset = 0;
            break;
        case 4: {
            uint8_t verify[512]; unsigned n = sizeof(firmware_visit) - firmware_visit_offset; if (n > sizeof(verify)) n = sizeof(verify);
            if (firmware_visit_file.read(verify, n) != int(n)
                || std::memcmp(verify, reinterpret_cast<const uint8_t *>(&firmware_visit) + firmware_visit_offset, n)) { fail(); return; }
            firmware_visit_offset += n;
            if (firmware_visit_offset < sizeof(firmware_visit)) return;
            firmware_visit_file.close(); break;
        }
        case 5:
            if (!append_boot_record(visit_config | firmware_exit_pair | (firmware_visit.token ? 0x8000 : 0))) { fail(); return; }
            break;
        case 6:
            if (!append_boot_record(uint32_t(BootChoice::stock))) { fail(); return; }
            break;
        case 7:
            USB1_USBCMD = 2; USBPHY1_CTRL_SET = 0x80000000;
            delay(100); __disable_irq(); SCB_AIRCR = 0x05fa0004;
            __asm__ volatile("dsb" ::: "memory");
            while (true) __asm__ volatile("wfi");
        }
        ++firmware_visit_stage; return;
    }
    if (firmware_resume_checked || !released || documents.busy()) return;
    firmware_resume_checked = true;
    auto state = inspect_boot_selection(reinterpret_cast<const volatile uint32_t *>(0x60084000));
    if (state.returned && state.choice == BootChoice::cntrl) {
        firmware_visit = FirmwareVisitRecord(); firmware_visit.token = state.returned;
        firmware_visit_stage = 8;
    }
}
#endif
cntrl::ModuleMenu module_menu(board, module_names, app_modules);
void configure_app_menu() {
    for (unsigned i = 0; i < app_layout.apps(); ++i) module_names[app_modules + i] = app_layout.app_name(i);
    module_menu.configure(app_modules + app_layout.apps());
}
bool patch_work_pending() {
    return apps_busy() || songs.executing() || songs.busy();
}
bool patch_state(unsigned operation, uint32_t value) {
    if (operation == 1) { board.set_brightness(uint8_t(value)); return true; }
    if (operation == 2) { board.set_midi_interface(value != 0); return true; }
#if CNTRL_SWITCHES
    if (operation == 11 || operation == 12) return board.switches.shift(operation == 11);
#endif
    if (operation >= 3 && operation <= 8) {
        if (!app_layout.apps()) return false;
        if ((operation == 7 || operation == 8) && (songs.executing() || documents.busy() || documents.dirty())) return false;
        return app_controller().navigate(operation);
    }
    if (operation == 9 || operation == 10) { pending_ui_operation = operation; return true; }
    return state_ui(UiOperation(operation));
}
bool patch_stock(uint8_t pair, cntrl::PatchExecutor &invocation) {
#if CNTRL_ORIGINAL_FIRMWARE
    visiting_invocation = &invocation;
    if (!invocation.document().valid()) return request_stock(pair, nullptr);
    cntrl::SongContinuation saved;
    if (&invocation == &song_sources.executor) saved = song_sources.context();
#if CNTRL_SWITCHES
    else if (board.switches.owns(invocation)) saved.caller = 2;
#endif
    saved.node = invocation.root(); saved.completed = invocation.completed(); saved.sources_crc = invocation.fingerprint();
    return request_stock(pair, &saved);
#else
    (void)pair; (void)invocation; return false;
#endif
}
bool module_controls_locked = false;
uint8_t held_controls = 0;
bool state_ui(UiOperation operation) {
    uint32_t now = millis();
    switch (operation) {
    case UiOperation::menu_previous:
    case UiOperation::menu_next:
        if (!module_menu.open()) return false;
        module_menu.button(operation == UiOperation::menu_previous ? 0 : 1, true); return true;
    case UiOperation::menu_back:
        if (!module_menu.open()) return false;
        if (module_menu.button(5, true) == -2) { documents.redraw(); module_controls_locked = true; }
        return true;
    case UiOperation::menu_open:
        app_controller().cancel_gesture();
        songs_ui.leave(); module_menu.enter(); module_controls_locked = true; return true;
    case UiOperation::performance_show:
        app_controller().cancel_gesture();
        module_menu.leave(); songs_ui.leave(); documents.redraw(); module_controls_locked = true; return true;
    case UiOperation::menu_activate: {
        if (!module_menu.open()) return false;
        int selected = module_menu.button(2, true);
        if (selected == -1) return true;
        if (selected >= app_modules && unsigned(selected - app_modules) < app_layout.apps()) {
            app_at(visible_app).cancel_gesture();
            visible_app = unsigned(selected - app_modules);
            refresh_app(); module_controls_locked = true;
            return true;
        }
        if (selected == songs_module) {
            if (!documents.busy() && !songs_transfer.busy() && board.output_idle()) { songs_ui.enter(); module_controls_locked = true; }
            else module_menu.enter();
        }
#if CNTRL_SWITCHES
        else if (selected == switches_module) {
            if (!apps_busy() && !documents.busy() && !documents.dirty() && !songs_transfer.busy() && board.output_idle() && documents.document().valid()) {
                board.enter_switches(documents.document(), held_controls);
            } else module_menu.enter();
        }
#endif
        else if (selected == setups_module) {
            if (documents.busy()) module_menu.enter();
            else { setup_ui.enter(); module_controls_locked = true; }
        }
        return true;
    }
    case UiOperation::setup_previous:
    case UiOperation::setup_next:
    case UiOperation::setup_confirm:
    case UiOperation::setup_back: {
        if (!setup_ui.open() || documents.busy()) return false;
        unsigned button = operation == UiOperation::setup_previous ? 0 : operation == UiOperation::setup_next ? 1
            : operation == UiOperation::setup_confirm ? 2 : 5;
        setup_ui.button(button, true, now); return true;
    }
    case UiOperation::songs_previous:
    case UiOperation::songs_next:
    case UiOperation::songs_confirm:
    case UiOperation::songs_previous_section:
    case UiOperation::songs_next_section:
    case UiOperation::songs_back: {
        if (!songs_ui.open()) return false;
        unsigned button = operation == UiOperation::songs_previous ? 0 : operation == UiOperation::songs_next ? 1
            : operation == UiOperation::songs_confirm ? 2 : operation == UiOperation::songs_previous_section ? 3
            : operation == UiOperation::songs_next_section ? 4 : 5;
        songs_ui.button(button, true);
        if (!songs_ui.open()) { documents.redraw(); module_controls_locked = true; }
        return true;
    }
#if CNTRL_SWITCHES
    case UiOperation::switches_return:
        board.enter_switches(documents.document(), 0); module_controls_locked = true; return true;
#endif
    default: return false;
    }
}
bool feature_open() {
    return module_menu.open() || setup_ui.open() || songs_ui.open()
#if CNTRL_SWITCHES
        || board.switches.open()
#endif
        ;
}
cntrl::splash::SdSlots splash_card;
cntrl::splash::Receiver splash_assets(splash_card, board);
cntrl::splash::Panels splash_panels;
cntrl::splash::Player splash_player(splash_assets, splash_panels);
unsigned random_movie(unsigned count) { return unsigned(random(count)); }
cntrl::splash::Session splash_session(splash_assets, splash_player, random_movie);

StorageListing storage_listing;
void storage_info(bool scan) {
    if (!card.ready) { board.reply("CNTRL ERROR storage-unavailable"); return; }
    auto *device = SD.sdfs.card();
    cid_t cid;
    uint32_t sectors = device->sectorCount();
    if (!sectors || !device->readCID(&cid)) {
        board.reply("CNTRL ERROR storage-read"); return;
    }
    char identity[33];
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&cid);
    for (unsigned i = 0; i < 16; ++i) std::snprintf(identity + 2 * i, 3, "%02x", bytes[i]);
    char free[12] = "-";
    if (scan) {
        // Explicit opt-in: this library call can scan the FAT/allocation bitmap.
        uint32_t count = SD.sdfs.freeClusterCount();
        if (count > SD.sdfs.clusterCount() || device->errorCode()) {
            board.reply("CNTRL ERROR storage-scan"); return;
        }
        std::snprintf(free, sizeof(free), "%lu", (unsigned long)count);
    }
    char response[128];
    std::snprintf(response, sizeof(response), "CNTRL STORAGE %lu %u %s %u %lu %lu %s",
        (unsigned long)sectors, unsigned(device->type()), identity, unsigned(SD.sdfs.fatType()),
        (unsigned long)SD.sdfs.bytesPerCluster(), (unsigned long)SD.sdfs.clusterCount(), free);
    if (scan && !storage_listing.begin()) { board.reply("CNTRL ERROR storage-list"); return; }
    board.reply(response);
}

PerformanceStatus cached_disk;
void capture_disk_space() {
    if (!card.ready) return;
    uint32_t clusters = SD.sdfs.clusterCount();
    uint32_t free = SD.sdfs.freeClusterCount();
    if (!clusters || free > clusters || SD.sdfs.card()->errorCode()) return;
    cached_disk.disk_free = uint64_t(free) * SD.sdfs.bytesPerCluster();
    cached_disk.disk_used = uint64_t(clusters - free) * SD.sdfs.bytesPerCluster();
    cached_disk.disk_valid = true;
}

PerformanceStatus performance_status() {
    PerformanceStatus result = cached_disk;
    result.starts = startup_counter.count; result.starts_status = startup_counter.status;
    uintptr_t stack;
    __asm__ volatile("mov %0, sp" : "=r"(stack));
    uintptr_t end = reinterpret_cast<uintptr_t>(&_ebss);
    result.ram_free[0] = stack > end ? uint32_t(stack - end) : 0;
    result.ram_used[0] = 512 * 1024 - result.ram_free[0];
    const auto heap = mallinfo();
    result.ram_free[1] = uint32_t(reinterpret_cast<uintptr_t>(&_heap_end) - reinterpret_cast<uintptr_t>(__brkval)) + heap.fordblks;
    result.ram_used[1] = 512 * 1024 - result.ram_free[1];
    result.cycles = documents.document().patch_cycles(); result.patches = documents.document().patches();
    return result;
}

void midi_status(unsigned route) { board.midi_status(route); }
bool asset_command(const char *line) {
    if (!std::strncmp(line, "CNTRL BUNDLE ", 13)) return bundle_transfer.command(line, millis());
    if (!std::strcmp(line, "CNTRL STORAGE NEXT")) {
        char response[600]; storage_listing.next(response, sizeof(response)); board.reply(response); return true;
    }
    if (board.configuration_locked()) { board.reply("CNTRL ERROR close-switches-first"); return true; }
#ifdef CNTRL_DIAGNOSTICS
    if (!std::strcmp(line, "CNTRL DISPLAY EDGE")) {
        if (feature_open() || documents.busy()) { board.reply("CNTRL ERROR busy"); return true; }
        board.timing_mode(true); board.reply("CNTRL EDGE ARMED"); return true;
    }
    if (!std::strcmp(line, "CNTRL DISPLAY EDGE END")) {
        board.timing_mode(false); board.reply("CNTRL OK"); return true;
    }

#endif
    if (!std::strncmp(line, "CNTRL SONGS ", 12)) {
        if (songs_ui.open()) { board.reply("CNTRL ERROR close-song-editor"); return true; }
        return songs_transfer.command(line, millis());
    }
    return font_files.command(line, board, millis());
}
cntrl::splash::Router serial_router(documents, splash_assets, splash_session, board, storage_info, midi_status, asset_command);
ButtonState buttons[6];
uint32_t last_scan = 0;

} // namespace

bool switch_scan_due() { return uint32_t(millis() - last_scan) >= 1; }

FLASHMEM void initialize_target() {
    cntrl::memory_budget = unsigned(reinterpret_cast<uintptr_t>(&cntrl_memory_budget));
    // DMAMEM is a NOLOAD section: constant initialization is not copied at boot.
    usb_session = cntrl::UsbSession{};
    for (unsigned i = 1; i < cntrl::AppLayout::max_apps; ++i)
        new (extra_apps[i - 1]) cntrl::Controller(board, board, patch_sources, 5 + i);
    initialize_switches();
    initialize_displays(false);
    board.begin();
    card.ready = SD.begin(BUILTIN_SDCARD);
    if (card.ready) {
        if (!SD.sdfs.exists("/cntrl") && !SD.sdfs.mkdir("/cntrl", true)) startup_counter.status = StartupCounter::Status::error;
        else {
            auto file = SD.sdfs.open(StartupCounter::path, O_CREAT | O_RDWR);
            startup_counter.start(file);
        }
    }
    if (card.ready) { board.load_expression_calibrations(); midi_files.boot(); }
    capture_disk_space();
    font_files.ready = card.ready;
    splash_card.ready = card.ready;
    splash_assets.boot();
    randomSeed(Teensy3Clock.get() ^ ARM_DWT_CYCCNT);
    if (card.ready && !setup_storage.boot()) board.error("Setup bundle invalid");
    documents.boot();
    if (card.ready && !songs.boot()) board.error("Song storage invalid");
}

void poll_target() {
    uint32_t now = millis();
    poll_display_dma();
    board.poll_keyboard();
    if (!display_spi_busy() && switch_scan_due()) {
        now = millis();
        if (last_scan) board.scanned(now - last_scan);
        last_scan = now;
        uint8_t raw = read_buttons();
        uint8_t events[6], pressed = 0;
        const auto &document = documents.document();
        bool fast_switches = !document.valid() || document.fast_switches();
        for (unsigned i = 0; i < 6; ++i) {
            unsigned debounce_ms = 15;
#if CNTRL_SWITCHES
            debounce_ms = board.switches.debounce_ms(i).value_or(15);
#endif
            events[i] = buttons[i].update(!(raw & (1u << button_bits[i])), now, debounce_ms, fast_switches);
            if (buttons[i].pressed) pressed |= 1u << i;
        }
        held_controls = pressed;
        if (events[2] & release_event) board.keyboard_button(2, false);
#if CNTRL_PANIC
        if ((events[2] & release_event) && board.panic.open()) board.panic.button(false, now);
#endif
        bool consumed = splash_session.controls(pressed, now, !feature_open() && pressed == (1u << 0));
#ifdef CNTRL_DIAGNOSTICS
        if (scroll_timing.active()) {
            if (events[5] & press_event) board.timing_mode(false);
            else if (events[2] & press_event) scroll_timing.start();
            consumed = true;
        }

#endif
        if (consumed) app_controller().cancel_gesture();
        setup_ui.poll(pressed);
        module_menu.poll(pressed);
        songs_ui.poll(pressed);
        if (!pressed) module_controls_locked = false;
#if CNTRL_ORIGINAL_FIRMWARE
        poll_firmware_visit(!pressed);
        consumed = consumed || firmware_visit_stage;
#endif
        if (!consumed) {
            for (unsigned i = 0; i < 6; ++i) {
#if CNTRL_SWITCHES
                if (board.switches.open() && (events[i] & release_event)) board.switches.button(i, false, now);
#endif
                if (module_controls_locked) continue;
                if (module_menu.open()) {
                    if (events[i] & press_event) {
                        if (i == 0) app_controller().executor().invoke(cntrl::StatePatch{unsigned(UiOperation::menu_previous)});
                        else if (i == 1) app_controller().executor().invoke(cntrl::StatePatch{unsigned(UiOperation::menu_next)});
                        else if (i == 2) app_controller().executor().invoke(cntrl::StatePatch{unsigned(UiOperation::menu_activate)});
                        else if (i == 5) app_controller().executor().invoke(cntrl::StatePatch{unsigned(UiOperation::menu_back)});
                    }
#if CNTRL_SWITCHES
                } else if (board.switches.open()) {
                    if (events[i] & press_event) board.switches.button(i, true, now);
                    if (events[i] & release_event) board.switches.button(i, false, now);
#endif
                } else if (songs_ui.open()) {
                    constexpr UiOperation operations[] = {UiOperation::songs_previous, UiOperation::songs_next,
                        UiOperation::songs_confirm, UiOperation::songs_previous_section,
                        UiOperation::songs_next_section, UiOperation::songs_back};
                    if (events[i] & press_event) app_controller().executor().invoke(cntrl::StatePatch{unsigned(operations[i])});
                } else if (setup_ui.open()) {
                    if (events[i] & press_event) {
                        if (i == 0) app_controller().executor().invoke(cntrl::StatePatch{unsigned(UiOperation::setup_previous)});
                        else if (i == 1) app_controller().executor().invoke(cntrl::StatePatch{unsigned(UiOperation::setup_next)});
                        else if (i == 2) app_controller().executor().invoke(cntrl::StatePatch{unsigned(UiOperation::setup_confirm)});
                        else if (i == 5) app_controller().executor().invoke(cntrl::StatePatch{unsigned(UiOperation::setup_back)});
                    }
                } else {
#if CNTRL_CABLE_TEST
                    if (board.cable_test.enabled() && i != 2 && i != 5) continue;
#endif
#if CNTRL_PANIC
                    if (board.panic_owns_midi && i != 2 && i != 5) continue;
#endif
                    if ((events[i] & (press_event | release_event)) && board.keyboard_button(i, events[i] & press_event)) continue;
                    if ((events[i] & press_event) && board.expression_button(i)) continue;
                    if ((events[i] & press_event) && board.trs_button(i)) continue;
                    if (board.clock_control(i, events[i] & press_event)) continue;
                    if (app_layout.apps()) {
                        if (events[i] & press_event) app_controller().button(mirrored_button(i), true, now);
                        if (events[i] & release_event) app_controller().button(mirrored_button(i), false, now);
                    } else if (events[i] & press_event) {
                        if (i == 5) board.browse_page(false);
                        else if (i == 2) board.tap_tempo(now);
                    }
                }
            }
        }
    }
    if (feature_open()) app_controller().cancel_gesture();
    tick_apps(now);
    board.poll();
    // A bounded amount of work per iteration; SD-card latency itself depends
    // on the card and must be measured on the pedal before release.
    for (unsigned i = 0; i < 512 && !usb_session.output_pending() && Serial.available(); ++i)
        usb_session.receive(Serial.read());
    if (usb_session.resetting() && serial_router.accepts_input() && !songs_transfer.processing()) {
        serial_router.cancel_transfer(); songs_transfer.cancel_transfer(); bundle_transfer.cancel(); font_files.cancel_transfer(); storage_listing.cancel();
        usb_session.reset_done();
    }
    for (unsigned i = 0; i < 128 && usb_session.input_size() && board.reply_ready()
#if CNTRL_ORIGINAL_FIRMWARE
        && !firmware_visit_stage
#endif
        && serial_router.accepts_input(); ++i) {
        serial_router.receive(usb_session.input(), now);
        usb_session.consumed();
    }
    // A resumable session owns its unfinished transfers until a new session
    // replaces it. USB outages must not expire staged bytes or list cursors.
    if (usb_session.active()) {
        serial_router.keepalive(now); songs_transfer.keepalive(now); font_files.keepalive(now);
        bundle_transfer.keepalive(now);
    }
    // Preserve the current serial frame under host backpressure. MIDI/buttons
    // continue while command input and reply-producing jobs wait for room.
    if (board.reply_ready()) serial_router.poll(now);
    if (board.reply_ready()) songs_transfer.poll(now);
    if (board.reply_ready()) bundle_transfer.poll(now);
    song_sources.tick(now);
    songs.tick();
    if (board.reply_ready()) font_files.poll(now, board);
#if CNTRL_ORIGINAL_FIRMWARE
    if (!firmware_visit_stage)
#endif
        board.poll_expression(now);
#if CNTRL_SWITCHES
    static bool switches_open = false;
    if (switches_open && !board.switches.open()) {
        board.leave_switches();
        if (pending_ui_operation == 10) app_controller().executor().invoke(cntrl::StatePatch{unsigned(UiOperation::performance_show)});
        else app_controller().executor().invoke(cntrl::StatePatch{unsigned(UiOperation::menu_open)});
        pending_ui_operation = 0; module_controls_locked = true;
        if (board.switches.abandoned()) board.error("Switch state unknown");
    }
    switches_open = board.switches.open();
#endif
    if ((pending_ui_operation || !patch_work_pending())
#if CNTRL_SWITCHES
        && !board.switches.open()
#endif
    ) {
        if (pending_ui_operation) {
            if (pending_ui_operation == 9) app_controller().executor().invoke(cntrl::StatePatch{unsigned(UiOperation::menu_open)});
            else app_controller().executor().invoke(cntrl::StatePatch{unsigned(UiOperation::performance_show)});
            pending_ui_operation = 0; return_to_switches = false; module_controls_locked = true;
        }
#if CNTRL_SWITCHES
        else if (return_to_switches) {
            return_to_switches = false;
            app_controller().executor().invoke(cntrl::StatePatch{unsigned(UiOperation::switches_return)});
        }
#endif
    }
    board.display(now, documents.busy() || feature_open() || patch_work_pending());
}
