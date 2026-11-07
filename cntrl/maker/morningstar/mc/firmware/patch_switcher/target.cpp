#include "target.h"
#if CNTRL_ORIGINAL_FIRMWARE
#include "../boot_switch/selection.h"
extern "C" void eepromemu_flash_write(void *, const void *, uint32_t);
#endif
#include <Arduino.h>
// Inline shared routines need distinct sections alongside strong FLASHMEM functions.
#define CNTRL_FLASH_ROUTINE(name) __attribute__((section(".flashmem.cntrl_" #name), noinline))
#include <cntrl/runtime.h>
#include <cntrl/ui/controls.h>
#include <cntrl/ui/application.h>
#include <cntrl/ui/page_host.h>
#include <cntrl/ui/application_hardware.h>
#include <cntrl/ui/song_sources.h>
#include <cntrl/ui/firmware_visit.h>
#include <cntrl/ui/input.h>
#include <usb_keyboard.h>
#include <SD.h>
#include "expression_pedals.h"
#include "midi_files.h"
#include <cntrl/startup_counter.h>
#include <cntrl/setup_files.h>
#include <cntrl/history_import.h>
#include <cntrl/asset_commands.h>
#include <USBHost_t36.h>
#include <malloc.h>
#include <charconv>
#include <new>
extern "C" { extern char _ebss, _heap_end; extern char *__brkval; }
extern "C" char cntrl_memory_budget;
#include <cntrl/documents.h>
#include <cntrl/patch_sources.h>
#include <cntrl/bundle_transfer.h>
#include <cntrl/setup_ui.h>
#include <cntrl/midi_bridge.h>
#include <cntrl/midi_outputs.h>
#include "midi_binding.h"
#include <cntrl/midi_runtime.h>
#include "target_capabilities.h"
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
#include <cntrl/ui/pixel_throb.h>
#include "displays.h"
#include "switches.h"
#include "renderer.h"
#include <cntrl/ui/pages.h>
#include <cntrl/ui/controls.h>
#include "embedded_fonts.h"
#include "trs_ports.h"
#ifdef CNTRL_DIAGNOSTICS
#include "scroll_timing.h"
#endif
#include <cntrl/storage_listing.h>
#include <cntrl/slot_storage.h>
#include "font_storage.h"
#include "../../splash/board.h"
#include <cntrl/animation/router.h>

// Library graphics draw into RAM; only our bounded driver touches panel pins.

namespace {
using cntrl::ui::Pages;
using cntrl::ui::PerformanceStatus;
using cntrl::ui::PixelThrob;
cntrl::ui::AssetFonts fonts(embedded_fonts, sizeof(embedded_fonts) / sizeof(*embedded_fonts));
#ifdef CNTRL_DIAGNOSTICS
ScrollTimingProbe scroll_timing;
#endif
extern cntrl::Documents documents;
extern cntrl::HistoryImport history_import;
extern cntrl::SongsTransfer songs_transfer;
extern cntrl::animation::Session splash_session;
extern cntrl::animation::Player splash_player;
extern cntrl::splash::Panels splash_panels;
font_assets::Files font_files;
TrsPorts trs_ports;
USBHost host;
MIDIDevice host_midi(host);
extern cntrl::StoredPatchSources patch_sources;
using UiOperation = cntrl::ui::Operation;
extern cntrl::ui::Application application;
PerformanceStatus performance_status();
void capture_disk_space();
cntrl::StartupCounter startup_counter;
MidiFileStorage midi_storage;
cntrl::midi::MidiFiles midi_files(midi_storage.filesystem, midi_storage.source,
    midi_storage.index, midi_storage.recording, midi_storage.entries, cntrl::target::midi_connections.count,
    "/cntrl/midi", "/cntrl/midi/.playback-index");
DMAMEM cntrl::Controller::NavigationState invocation_navigation[cntrl::target::max_switches + cntrl::AppLayout::max_apps];
bool restoring_invocation = false;
cntrl::PatchExecutor *visiting_invocation = nullptr;

#if CNTRL_ORIGINAL_FIRMWARE
extern cntrl::ui::FirmwareVisit firmware_visit;
bool append_boot_record(uint32_t value);
#endif
DMAMEM cntrl::UsbSession usb_session;
static_assert(cntrl::target::button_count == sizeof(button_bits), "Button declaration must match the input wiring");


class Board : public cntrl::KeyboardSink, public cntrl::ui::ApplicationHardware<Board>, public cntrl::Replies, public cntrl::animation::Replies, public cntrl::midi::Sink, public cntrl::PatchOutputs, public cntrl::ui::ApplicationServices
#if CNTRL_PANIC
    , public cntrl::PanicOutputs
#endif
#if CNTRL_ORIGINAL_FIRMWARE
    , public cntrl::ui::FirmwareVisitServices
#endif
{
public:
    auto &page_host() { return ui_; }
    void blank_performance() override { cntrl::ui::ApplicationHardware<Board>::blank_performance(); }
    bool stop_clock() { return cntrl::ui::ApplicationHardware<Board>::stop_clock(); }
    uint32_t milliseconds() const { return millis(); }
    uint32_t microseconds() const { return micros(); }
    unsigned midi_file_clock_outputs() const { return midi_files.player.clock_outputs(); }
    bool keyboard_connected() const { return usb_configuration != 0; }
    cntrl::midi::Bridge &midi_bridge() { return bridge_; }
    cntrl::midi::TrsMode port_mode(unsigned jack) const { return trs_ports.mode(jack); }
    unsigned port_count() const { return cntrl::target::port_count; }
    auto &expression_controls() { return expression_.controls(); }
    bool expression_available(unsigned jack) const { return trs_ports.expression_pin(jack) >= 0; }
    auto &animation_session() { return splash_session; }
    bool display_priority_due() const { return switch_scan_due(); }
    bool display_busy() const { return display_dma_pending(); }
    PerformanceStatus status() const { return performance_status(); }
    void display_brightness(uint8_t value) { analogWrite(6, value); }
    void displayed() { midi_runtime_.displayed(); }
    bool switch_firmware() {
#if CNTRL_ORIGINAL_FIRMWARE
        return firmware_visit.start(33);
#else
        return false;
#endif
    }
#if CNTRL_ORIGINAL_FIRMWARE
    cntrl::ui::FirmwareSelection firmware_selection() const override {
        const auto state = inspect_boot_selection(reinterpret_cast<const volatile uint32_t *>(0x60084000));
        return {state.next, state.returned, can_enter_stock(state), state.choice == BootChoice::cntrl};
    }
    bool valid_exit(uint8_t pair) const override { return valid_exit_pair(pair); }
    bool storage_available() const override { return midi_storage.filesystem.available(); }
    bool configure_return(uint8_t pair, uint32_t token) override { return append_boot_record(visit_config | pair | (token ? 0x8000 : 0)); }
    bool choose_external_firmware() override { return append_boot_record(uint32_t(BootChoice::stock)); }
    bool consume_return(uint32_t token) override { return append_boot_record(visit_consumed | token); }
    void handoff_firmware() override {
        USB1_USBCMD = 2; USBPHY1_CTRL_SET = 0x80000000;
        delay(100); __disable_irq(); SCB_AIRCR = 0x05fa0004;
        __asm__ volatile("dsb" ::: "memory");
        while (true) __asm__ volatile("wfi");
    }
    bool switch_invocation(cntrl::PatchExecutor &execution) const override {
#if CNTRL_SWITCHES
        return switches.owns(execution);
#else
        (void)execution; return false;
#endif
    }
#endif
    unsigned display_count() const override { return display_.count(); }
    const char *display_id(unsigned index) const override {
        return index < cntrl::target::display_count ? cntrl::target::display_ids[index] : nullptr;
    }
    unsigned input_count() const override { return cntrl::target::button_count; }
    const char *input_label(unsigned index) const override {
        return index < cntrl::target::button_count ? cntrl::target::button_labels[index] : "";
    }
    unsigned control_input(cntrl::ControlRole role) const override {
        const auto &controls = cntrl::target::controls;
        const unsigned indices[] = {controls.previous, controls.next, controls.confirm,
            controls.previous_section, controls.next_section, controls.back};
        return indices[unsigned(role)];
    }
    const char *control_label(cntrl::ControlRole role) const override { return input_label(control_input(role)); }

    cntrl::midi::Outputs midi_outputs() const override { return cntrl::target::midi_connections; }
    unsigned memory_capacity() const override { return cntrl::target::memory_capacity; }
    unsigned memory_runtime() const override { return cntrl::target::memory_runtime; }
    uint32_t application_time() const override { return millis(); }
    bool song_transfer_busy() const override { return songs_transfer.busy(); }
    bool switch_editor_open() const override {
#if CNTRL_SWITCHES
        return switches.open();
#else
        return false;
#endif
    }
    void open_switch_editor(const cntrl::Document &document, uint64_t held) override {
#if CNTRL_SWITCHES
        enter_switches(document, uint8_t(held));
#else
        (void)document; (void)held;
#endif
    }
    bool shift_switch_editor(bool backwards) override {
#if CNTRL_SWITCHES
        return switches.shift(backwards);
#else
        (void)backwards; return false;
#endif
    }
    using RuntimeState = cntrl::ui::RuntimeState;
    RuntimeState runtime;
#if CNTRL_ORIGINAL_FIRMWARE
    bool supports_function(unsigned function) const override { return function == 1; }
    bool stock_firmware(uint8_t pair, const cntrl::SongContinuation *continuation = nullptr) override { return firmware_visit.request(pair, continuation); }
#endif
#if CNTRL_SWITCHES
    cntrl::Switches switches{*this, *this, patch_sources};
    unsigned switch_capacity() const override { return cntrl::target::max_switches; }
#endif
    bool configuration_locked() const override { return history_import.busy() || application.patch_pending()
#if CNTRL_SWITCHES
        || switches.open()
#endif
        ; }
    bool send_patch(unsigned owner, const char *output, const uint8_t *bytes, unsigned n) override {
        const auto *connection = midi_outputs().find(output);
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
        return midi_files.record(settings, startup_counter.count,
            startup_counter.status == cntrl::StartupCounter::Status::ready, micros());
    }
    FLASHMEM bool state(unsigned operation, uint32_t value) override { return application.state(operation, value, *this); }
    bool stock(uint8_t pair, cntrl::PatchExecutor &invocation) override {
#if CNTRL_ORIGINAL_FIRMWARE
        return firmware_visit.stock(pair, invocation);
#else
        (void)pair; (void)invocation; return false;
#endif
    }
    void validation(bool begin) override { application.controller().validation(begin); }
    void validation_context(cntrl::PatchExecutor &invocation, bool begin) override {
        if (begin && !restoring_invocation && !invocation.reconstructing())
            invocation_navigation[invocation.owner()] = application.controller().navigation();
        application.controller().validation(begin);
    }
    bool preview_state(unsigned operation, uint32_t value) override {
#if CNTRL_SWITCHES
        if (operation == 11 || operation == 12) return switches.open();
#endif
        return application.controller().preview_state(operation, value);
    }
    bool selection(unsigned operation, const cntrl::Document *&document, unsigned &node) override {
        if (!documents.document().valid() || !documents.document().app_layout().apps()) return false;
        return application.controller().selection(operation, document, node);
    }
    void selection_complete(unsigned operation) override { application.controller().selection_complete(operation); }
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
        const auto *connection = midi_outputs().find(output);
        return connection && submit(connection->route, {uint8_t(bytes[0] >> 4), {bytes[0], bytes[1], bytes[2]}});
    }
#endif
#if CNTRL_CABLE_TEST
    cntrl::CableTest cable_test{*this, cntrl::target::cable_connections, cntrl::target::cable_connections.count};
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
    bool supports_trs_mode(unsigned jack, cntrl::midi::TrsMode mode) const override {
        return jack < cntrl::target::port_count && mode <= cntrl::midi::TrsMode::aux;
    }
    void configure_trs_ports(const uint8_t *modes) override {
        for (const auto &endpoint : midi_outputs()) {
            const auto binding = cntrl::device::midi_binding(endpoint);
            if (binding.driver == cntrl::device::MidiDriver::configurable &&
                uint8_t(trs_ports.mode(binding.index)) != modes[binding.index])
                bridge_.disconnect_output(endpoint.route);
        }
        trs_ports.configure(modes);
    }
    void begin() {
        Serial1.begin(31250);
        Serial2.begin(31250);
        host.begin();
    }
    bool supports(const char *output) const override { const auto *endpoint = midi_outputs().find(output); return endpoint && endpoint->output; }
    bool available(const char *output) const {
        const auto *connection = midi_outputs().find(output);
        if (!connection || !connection->output) return false;
        switch (connection->transport) {
        case cntrl::midi::OutputTransport::physical:
            {
                const auto binding = cntrl::device::midi_binding(*connection);
                if (binding.driver == cntrl::device::MidiDriver::missing) return false;
                if (binding.driver == cntrl::device::MidiDriver::configurable &&
                    !cntrl::midi::midi_output(trs_ports.mode(binding.index))) return false;
            }
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
        return fonts.supports(font, size);
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
    unsigned display_width() const override { return cntrl::ui::minimum_display_width(display_); }
    unsigned display_height() const override { return cntrl::ui::minimum_display_height(display_); }
    bool send(const char *output, const uint8_t *data, unsigned length) override {
        const auto *connection = midi_outputs().find(output);
        if (!connection || length > 14 || !available(output)) return false;
        return bridge_.pedal(connection->route, data, length);
    }
    // Keep the transport backend in flash to leave RAM1 for runtime state.
    FLASHMEM bool write_packet(unsigned route, const cntrl::midi::Packet &p) override {
        if (!bridge_.usb_enabled(route)) return false;
        const auto *connection = midi_outputs().route(route);
        if (!connection) return false;
        const auto binding = cntrl::device::midi_binding(*connection);
        const unsigned n = p.size();
        HardwareSerial *serial;
        switch (binding.driver) {
        case cntrl::device::MidiDriver::usb_device: return usb_midi_try_write_packed(p.packed(binding.index));
        case cntrl::device::MidiDriver::usb_host: return host_midi.try_write_packed(p.packed(binding.index));
        case cntrl::device::MidiDriver::configurable: return trs_ports.send(binding.index, p.bytes, n);
        case cntrl::device::MidiDriver::din: serial = &Serial1; break;
        case cntrl::device::MidiDriver::trs: serial = &Serial2; break;
        default: return false;
        }
        return unsigned(serial->availableForWrite()) >= n && serial->write(p.bytes, n) == n;
    }
    void midi_reset() { midi_runtime_.reset(*this); }
    void midi_status(unsigned route) { midi_runtime_.status(bridge_, route, documents.document(), *this); }
    void scanned(uint32_t gap) { midi_runtime_.scanned(gap); }
#ifdef CNTRL_DIAGNOSTICS
    void timing_mode(bool enabled) {
        if (enabled) {
            scroll_timing.arm();
            analogWrite(6, runtime.brightness);
        }
        else {
            scroll_timing.stop(); renderer_.invalidate();
            documents.redraw();
            analogWrite(6, ui_.hardware_brightness());
        }
    }

#endif
    void set_animation_color_cycle(bool enabled) override {
        splash_player.set_color_cycle(enabled);
    }
    bool reply_ready() const override { return usb_session.reply_ready(); }
    void reply(const char *text) override { startup_counter.reply(text, usb_session); }
    void midi_transport_poll() { host.Task(); }
    bool midi_host_connected() const { return bool(host_midi); }
    bool midi_device_connected() const { return usb_configuration != 0; }
    bool midi_document_valid() const { return documents.document().valid(); }
    unsigned midi_source_count() const { return 4; }
    cntrl::midi::MidiFiles &runtime_midi_files() { return midi_files; }
    cntrl::ui::Pages &runtime_pages() { return ui_.pages_; }
    void midi_flush() { usb_midi_flush_output(); }
#if CNTRL_SWITCHES
    void midi_switch_tick(uint32_t now) { switches.tick(now); }
#endif
    bool midi_read_event(unsigned source, cntrl::midi::InputEvent &event) {
        if (source == 0) {
            uint32_t packet = usb_midi_read_message();
            if (!packet) return false;
            const auto *input = cntrl::device::midi_endpoint(midi_outputs(), cntrl::device::MidiDriver::usb_device);
            event.input = input ? input->route : cntrl::midi::InputEvent::absent;
            event.clock_source = (packet >> 4) & 15;
            event.packetized = true;
            event.packet = {uint8_t(packet & 15), {uint8_t(packet >> 8), uint8_t(packet >> 16), uint8_t(packet >> 24)}};
            const auto *output = event.clock_source < 2 ?
                cntrl::device::midi_endpoint(midi_outputs(), event.clock_source ? cntrl::device::MidiDriver::trs : cntrl::device::MidiDriver::din) :
                cntrl::device::midi_endpoint(midi_outputs(), cntrl::device::MidiDriver::configurable, event.clock_source - 2);
            if (output) event.forward = output->route;
            return true;
        }
        if (source < 3) {
            HardwareSerial &serial = source == 1 ? Serial1 : Serial2;
            int byte = serial.read(); if (byte < 0) return false;
            const auto *input = cntrl::device::midi_endpoint(midi_outputs(), source == 1 ? cntrl::device::MidiDriver::din : cntrl::device::MidiDriver::trs);
            const auto *output = cntrl::device::midi_endpoint(midi_outputs(), cntrl::device::MidiDriver::usb_device, source - 1);
            event.input = input ? input->route : cntrl::midi::InputEvent::absent; event.byte = uint8_t(byte);
            event.forward = output ? output->route : cntrl::midi::InputEvent::absent; event.clock_source = source + 15;
            event.cable_input = source - 1;
            return true;
        }
        // Driver storage stays unchanged until the next read(). Consume a
        // decoded SysEx array incrementally before requesting another message.
        const auto *input = cntrl::device::midi_endpoint(midi_outputs(), cntrl::device::MidiDriver::usb_host);
        event.input = input ? input->route : cntrl::midi::InputEvent::absent;
        if (host_sysex_offset_ < host_sysex_length_) {
            event.byte = host_midi.getSysExArray()[host_sysex_offset_++];
            event.clock_source = 18 + host_midi.getCable();
            return true;
        }
        if (!host_midi.read()) return false;
        unsigned type = host_midi.getType();
        event.clock_source = 18 + host_midi.getCable();
        if (type == 0xf0) {
            host_sysex_offset_ = 0; host_sysex_length_ = host_midi.getSysExArrayLength();
            if (!host_sysex_length_) return false;
            event.byte = host_midi.getSysExArray()[host_sysex_offset_++];
        } else {
            uint8_t cin = type < 0xf0 ? type >> 4 : type == 0xf1 || type == 0xf3 ? 2 : type == 0xf2 ? 3 : type == 0xf6 ? 5 : 15;
            event.packetized = true;
            event.packet = {cin, {uint8_t(type < 0xf0 ? type | (host_midi.getChannel() - 1) : type), host_midi.getData1(), host_midi.getData2()}};
        }
        return true;
    }
    void poll() {
        midi_runtime_.poll(*this);
        if (usb_session.output_pending() && Serial.availableForWrite() > 0) {
            unsigned n = std::strlen(usb_session.output());
            unsigned room = Serial.availableForWrite();
            if (n > room) n = room;
            usb_session.sent(Serial.write(usb_session.output(), n));
        }
    }
    void load_expression_calibrations() { expression_.controls().load_calibrations(); }
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
                return !application.patch_pending() && output_idle() && send(out, bytes, 3);
            });
    }
    void display(uint32_t now, bool transfer_busy) {
#ifdef CNTRL_DIAGNOSTICS
        if (scroll_timing.active()) { if (!display_dma_pending()) scroll_timing.poll(micros()); return; }
#endif
        cntrl::ui::ApplicationHardware<Board>::display(now, transfer_busy);
    }
private:

    ExpressionPedals expression_;
    BoardDisplay display_;
    cntrl::midi::Bridge bridge_{cntrl::target::midi_connections};
    cntrl::midi::Runtime midi_runtime_;
    unsigned host_sysex_offset_ = 0, host_sysex_length_ = 0;
    RenderPixels pixels_;
    cntrl::ui::Renderer renderer_{display_, fonts, pixels_.work, cntrl::target::maximum_display_area, pixels_.cache, pixels_.previous, cntrl::target::total_display_area};
    cntrl::ui::PageHost<Board> ui_{*this, application, documents, display_, fonts, renderer_, cntrl::target::controls};
};

Board board;
cntrl::device::SdFileSystem setup_filesystem;
cntrl::device::SdFile setup_reader, setup_writer;
cntrl::device::SdDirectory setup_history;
cntrl::SetupFiles card(setup_filesystem, setup_reader, setup_writer, setup_history);
cntrl::BundledStorage setup_storage(card, board);
cntrl::Controller primary_controller(board, board, patch_sources, cntrl::target::max_switches);
// Document bytes are allocated from RAM2's heap as needed.
DMAMEM cntrl::Documents documents(setup_storage, board, primary_controller, board);
cntrl::StoredPatchSources patch_sources(setup_storage.setups());
cntrl::BundleTransfer bundle_transfer(setup_storage, documents, board, board);
cntrl::HistoryImport history_import(card, documents, board, board);
alignas(cntrl::Controller) DMAMEM unsigned char extra_apps[cntrl::AppLayout::max_apps - 1][sizeof(cntrl::Controller)];
extern cntrl::Songs songs;

cntrl::device::SdFile song_writer, song_readers[4];
cntrl::files::File *song_reader_handles[] = {&song_readers[0], &song_readers[1], &song_readers[2], &song_readers[3]};
cntrl::SlotStorage song_files(setup_filesystem, song_writer, song_reader_handles, 4, "/cntrl/songs", card.ready);
cntrl::ui::SongSources song_sources(board, board, patch_sources, setup_storage.setups(), documents, application, songs, cntrl::target::max_switches);
DMAMEM cntrl::Songs songs(song_files, song_sources);
DMAMEM cntrl::SongsTransfer songs_transfer(songs, board);
cntrl::ui::Application application(board, documents, primary_controller, extra_apps, songs, song_sources, CNTRL_SWITCHES);
#if CNTRL_ORIGINAL_FIRMWARE
DMAMEM cntrl::ui::FirmwareVisitRecord firmware_visit_record;
cntrl::device::SdFile firmware_visit_file;
cntrl::ui::FirmwareVisit firmware_visit(firmware_visit_record, firmware_visit_file, board, board,
    documents, application, songs, song_sources, board.runtime, invocation_navigation,
    visiting_invocation, restoring_invocation);
bool append_boot_record(uint32_t value) {
    auto words = reinterpret_cast<const volatile uint32_t *>(0x60084000);
    auto state = inspect_boot_selection(words);
    if (!state.next) return false;
    uint32_t record[] = {value, ~value};
    eepromemu_flash_write(reinterpret_cast<void *>(0x60084000 + state.next * 8), record, sizeof(record));
    return words[state.next * 2] == value && words[state.next * 2 + 1] == ~value;
}
#endif

cntrl::splash::SdSlots splash_card;
cntrl::animation::Receiver splash_assets(splash_card, board);
cntrl::splash::Panels splash_panels;
cntrl::animation::Player splash_player(splash_assets, splash_panels);
unsigned random_movie(unsigned count) { return unsigned(random(count)); }
cntrl::animation::Session splash_session(splash_assets, splash_player, random_movie);

cntrl::StorageListing storage_listing(setup_filesystem);
void storage_info(bool scan) {
    if (!card.ready) { board.reply("CNTRL ERROR storage-unavailable"); return; }
    auto *device = SD.sdfs.card();
    cid_t cid;
    uint32_t sectors = device->sectorCount();
    if (!sectors || !device->readCID(&cid)) {
        board.reply("CNTRL ERROR storage-read"); return;
    }
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&cid);
    const uint64_t capacity = uint64_t(sectors) * 512;
    const uint32_t unit = SD.sdfs.bytesPerCluster();
    const uint64_t usable = uint64_t(SD.sdfs.clusterCount()) * unit;
    char free[24] = "-";
    if (scan) {
        // Explicit opt-in: this library call can scan the FAT/allocation bitmap.
        uint32_t count = SD.sdfs.freeClusterCount();
        if (count > SD.sdfs.clusterCount() || device->errorCode()) {
            board.reply("CNTRL ERROR storage-scan"); return;
        }
        auto end = std::to_chars(free, free + sizeof(free) - 1, uint64_t(count) * unit).ptr; *end = 0;
    }
    // Identification registers and filesystem classification belong to this driver.
    static char values[9][40];
    static const cntrl::StorageListing::Detail details[] = {
        {"card_type", values[0]}, {"manufacturer_id", values[1]}, {"oem_id", values[2]},
        {"product", values[3]}, {"revision", values[4]}, {"serial_number", values[5]},
        {"manufactured", values[6]}, {"filesystem", values[7]}, {"identity", values[8]},
    };
    unsigned kind = device->type();
    std::snprintf(values[0], sizeof(values[0]), "%s", kind == 1 ? "SD v1" : kind == 2 ? "SD v2" : capacity > (uint64_t(32) << 30) ? "SDXC" : "SDHC");
    std::snprintf(values[1], sizeof(values[1]), "0x%02X", bytes[0]);
    std::memcpy(values[2], bytes + 1, 2); values[2][2] = 0;
    std::memcpy(values[3], bytes + 3, 5); values[3][5] = 0;
    std::snprintf(values[4], sizeof(values[4]), "%u.%u", bytes[8] >> 4, bytes[8] & 15);
    uint32_t serial = uint32_t(bytes[9]) << 24 | uint32_t(bytes[10]) << 16 | uint32_t(bytes[11]) << 8 | bytes[12];
    std::snprintf(values[5], sizeof(values[5]), "0x%08lX", (unsigned long)serial);
    std::snprintf(values[6], sizeof(values[6]), "%u-%02u", 2000 + ((bytes[13] & 15) << 4) + (bytes[14] >> 4), bytes[14] & 15);
    unsigned fat = SD.sdfs.fatType();
    std::snprintf(values[7], sizeof(values[7]), "%s", fat == 16 ? "FAT16" : fat == 32 ? "FAT32" : "exFAT");
    for (unsigned i = 0; i < 16; ++i) std::snprintf(values[8] + 2 * i, 3, "%02x", bytes[i]);
    char capacity_text[24], usable_text[24];
    *std::to_chars(capacity_text, capacity_text + sizeof(capacity_text) - 1, capacity).ptr = 0;
    *std::to_chars(usable_text, usable_text + sizeof(usable_text) - 1, usable).ptr = 0;
    char response[128];
    std::snprintf(response, sizeof(response), "CNTRL STORAGE %s %s %s %lu",
        capacity_text, usable_text, free, (unsigned long)unit);
    if (scan && !storage_listing.begin(details, sizeof(details) / sizeof(*details))) { board.reply("CNTRL ERROR storage-list"); return; }
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
    result.starts = startup_counter.count; result.starts_status = cntrl::ui::StartsStatus(startup_counter.status);
    std::snprintf(result.starts_error, sizeof(result.starts_error), "%s", startup_counter.error);
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
void midi_reset() { board.midi_reset(); }
bool native_asset_command(const char *line) {
#ifdef CNTRL_DIAGNOSTICS
    if (!std::strcmp(line, "CNTRL DISPLAY EDGE")) {
        if (application.feature_open(board) || documents.busy()) { board.reply("CNTRL ERROR busy"); return true; }
        board.timing_mode(true); board.reply("CNTRL EDGE ARMED"); return true;
    }
    if (!std::strcmp(line, "CNTRL DISPLAY EDGE END")) {
        board.timing_mode(false); board.reply("CNTRL OK"); return true;
    }

#else
    (void)line;
#endif
    return false;
}
cntrl::device::SdFile counter_command_file;
struct AssetServices {
    Board &hardware, &replies;
    cntrl::StartupCounter &counter;
    cntrl::files::File &counter_file;
    cntrl::midi::MidiFileRecorder &recorder;
    decltype(history_import) &history;
    decltype(bundle_transfer) &bundle;
    cntrl::StorageListing &listing;
    cntrl::SongsUi &song_editor;
    decltype(songs_transfer) &songs;
    font_assets::Storage &fonts;
    bool (*native_extension)(const char *);
} asset_services{board, board, startup_counter, counter_command_file, midi_files.recorder,
    history_import, bundle_transfer, storage_listing, application.song_ui, songs_transfer,
    font_files, native_asset_command};
cntrl::AssetCommands<AssetServices> asset_commands(asset_services);
bool asset_command(const char *line) { return asset_commands.command(line, millis()); }
cntrl::animation::Router serial_router(documents, splash_assets, splash_session, board, storage_info, midi_status, asset_command, midi_reset);
cntrl::ButtonState buttons[cntrl::target::button_count];
cntrl::CooperativeRuntime cooperative_runtime(cntrl::target::scan_interval_ms);

} // namespace

bool switch_scan_due() { return cooperative_runtime.scan_due(millis()); }

FLASHMEM void initialize_target() {
    cntrl::memory_budget = unsigned(reinterpret_cast<uintptr_t>(&cntrl_memory_budget));
    // DMAMEM is a NOLOAD section: constant initialization is not copied at boot.
    usb_session = cntrl::UsbSession{};
    for (unsigned i = 1; i < cntrl::AppLayout::max_apps; ++i)
        new (extra_apps[i - 1]) cntrl::Controller(board, board, patch_sources, cntrl::target::max_switches + i);
    initialize_switches();
    initialize_displays(false);
    board.begin();
    card.ready = SD.begin(BUILTIN_SDCARD);
    if (card.ready) {
        if (!SD.sdfs.exists("/cntrl") && !SD.sdfs.mkdir("/cntrl", true)) {
            startup_counter.status = cntrl::StartupCounter::Status::error;
            std::strcpy(startup_counter.error, "mkdir");
        }
        else {
            cntrl::device::SdFile file; file.open(cntrl::StartupCounter::path, cntrl::files::Mode::create);
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

namespace {
class TargetControls {
public:
    bool switches_open() const {
#if CNTRL_SWITCHES
        return board.switches.open();
#else
        return false;
#endif
    }
    void switch_button(unsigned i, bool pressed, uint32_t now) {
#if CNTRL_SWITCHES
        board.switches.button(i, pressed, now);
#else
        (void)i; (void)pressed; (void)now;
#endif
    }
    bool controls_locked() const { return application.controls_locked || history_import.busy(); }
    bool menu_open() const { return application.menu.open(); }
    bool songs_open() const { return application.song_ui.open(); }
    bool setup_open() const { return application.setups.open(); }
    void invoke(cntrl::ui::Operation operation) { application.controller().executor().invoke(cntrl::StatePatch{unsigned(operation)}); }
    void browse(bool reverse) { board.browse_page(reverse); }
    bool midi_reserved() const {
        bool reserved = false;
#if CNTRL_CABLE_TEST
        reserved = reserved || board.cable_test.enabled();
#endif
#if CNTRL_PANIC
        reserved = reserved || board.panic_owns_midi;
#endif
        return reserved;
    }
    bool keyboard(unsigned i, bool pressed) { return board.keyboard_button(i, pressed); }
    bool expression(unsigned i) { return board.expression_button(i); }
    bool port(unsigned i) { return board.trs_button(i); }
    bool clock(unsigned i, bool pressed) { return board.clock_control(i, pressed); }
    bool has_app() const { return application.layout.apps(); }
    void app(unsigned i, bool pressed, uint32_t now) { application.controller().button(i, pressed, now); }
    void tap(uint32_t now) { board.tap_tempo(now); }
    void leave_switches() {
#if CNTRL_SWITCHES
        board.leave_switches();
#endif
    }
    bool abandoned() const {
#if CNTRL_SWITCHES
        return board.switches.abandoned();
#else
        return false;
#endif
    }
    bool patch_pending() const { return application.patch_pending(); }
    void error(const char *message) { board.error(message); }
};
TargetControls target_controls;
cntrl::ui::InputCoordinator input_coordinator(buttons, cntrl::target::controls, documents.document(), application, board);
class TargetRuntime {
public:
    uint32_t now() const { return millis(); }
    void complete_display() { poll_display_dma(); }
    void keyboard() { board.poll_keyboard(); }
    bool scan_available() const { return !display_spi_busy(); }
    void scan(uint32_t now, uint32_t gap) {
        if (gap) board.scanned(gap);
        const uint8_t raw = read_buttons();
        uint8_t logical = 0;
        for (unsigned i = 0; i < cntrl::target::button_count; ++i)
            if (!(raw & (1u << button_bits[i]))) logical |= uint8_t(1u << i);
        input_coordinator.scan(logical, now, *this);
    }
    std::optional<unsigned> debounce(unsigned i) const {
#if CNTRL_SWITCHES
        return board.switches.debounce_ms(i);
#else
        (void)i; return std::nullopt;
#endif
    }
    void keyboard_release(unsigned input) { board.keyboard_button(input, false); }
    bool panic_open() const {
#if CNTRL_PANIC
        return board.panic.open();
#else
        return false;
#endif
    }
    void panic_release(uint32_t now) {
#if CNTRL_PANIC
        board.panic.button(false, now);
#else
        (void)now;
#endif
    }
    bool animation_controls(uint8_t pressed, uint32_t now, bool replay) { return splash_session.controls(pressed, now, replay); }
    bool diagnostic_controls(const uint8_t *events, uint8_t, uint32_t) {
#ifdef CNTRL_DIAGNOSTICS
        if (scroll_timing.active()) {
            if (cntrl::target::controls.back < cntrl::target::button_count && (events[cntrl::target::controls.back] & cntrl::press_event)) board.timing_mode(false);
            else if (cntrl::target::controls.confirm < cntrl::target::button_count && (events[cntrl::target::controls.confirm] & cntrl::press_event)) scroll_timing.start();
            return true;
        }
#else
        (void)events;
#endif
        return false;
    }
    cntrl::ui::FirmwareVisit *visit() {
#if CNTRL_ORIGINAL_FIRMWARE
        return &firmware_visit;
#else
        return nullptr;
#endif
    }
    TargetControls &actions() { return target_controls; }
    void apps(uint32_t now) { if (application.feature_open(board) || history_import.busy()) application.controller().cancel_gesture(); application.tick(now); }
    void midi() { board.poll(); }
    auto &session() { return usb_session; }
    auto &commands() { return serial_router; }
    auto &song_transfer() { return songs_transfer; }
    auto &bundle_transfer() { return ::bundle_transfer; }
    auto &history_transfer() { return history_import; }
    auto &fonts() { return font_files; }
    auto &song_sources() { return ::song_sources; }
    auto &songs() { return ::songs; }
    auto &replies() { return board; }
    bool serial_available() const { return Serial.available(); }
    int serial_read() { return Serial.read(); }
    bool reply_ready() const { return board.reply_ready(); }
    void cancel_listing() { storage_listing.cancel(); }
    bool commands_allowed() const {
#if CNTRL_ORIGINAL_FIRMWARE
        return !firmware_visit.busy();
#else
        return true;
#endif
    }
    bool expression_allowed() const { return commands_allowed(); }
    void expression(uint32_t now) { board.poll_expression(now); }
    void navigation() { if (!history_import.busy()) input_coordinator.finish(target_controls); }
    void display(uint32_t now) { board.display(now, documents.busy() || application.feature_open(board) || application.patch_pending()); }
};
TargetRuntime target_runtime;
} // namespace

void poll_target() { cooperative_runtime.poll(target_runtime); }
