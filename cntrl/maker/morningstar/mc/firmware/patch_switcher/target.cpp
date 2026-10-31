#include "target.h"
#if CNTRL_ORIGINAL_FIRMWARE
#include "../original_firmware.h"
#include "../boot_switch/selection.h"
extern "C" void eepromemu_flash_write(void *, const void *, uint32_t);
#endif
#include <Arduino.h>
#include <SD.h>
#include <USBHost_t36.h>
#include <cntrl/documents.h>
#include <cntrl/patch_sources.h>
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
#if CNTRL_MIDI_INTERFACE
#include <cntrl/midi_interface.h>
#endif
#if CNTRL_CABLE_TEST
#include <cntrl/cable_test.h>
#endif
#include "buttons.h"
#include "displays.h"
#include "switches.h"
#include "renderer.h"
#include "storage_listing.h"
#include "font_storage.h"
#include "../../splash/board.h"
#include "../../splash/router.h"

// Library graphics draw into RAM; only our bounded driver touches panel pins.

namespace {
extern cntrl::splash::Session splash_session;
extern cntrl::splash::Panels splash_panels;
font_assets::Files font_files;
USBHost host;
MIDIDevice host_midi(host);
extern cntrl::StoredPatchSources patch_sources;
extern cntrl::Controller controller;
bool patch_state(unsigned operation, uint32_t value);
enum class UiOperation : unsigned {
    menu_previous = 128, menu_next, menu_activate, menu_back, menu_open, performance_show,
    setup_previous, setup_next, setup_confirm, setup_back,
    songs_previous, songs_next, songs_confirm, songs_previous_section, songs_next_section, songs_back,
    cable_toggle, cable_back, interface_toggle, interface_back,
    panic_begin, panic_end, panic_back, stock_confirm, stock_back, switches_return,
};
bool state_ui(UiOperation operation);
bool patch_stock(uint8_t pair, cntrl::PatchExecutor &invocation);
bool patch_work_pending();
DMAMEM cntrl::Controller::NavigationState invocation_navigation[6];
bool restoring_invocation = false;
unsigned pending_ui_operation = 0;
bool return_to_switches = false;
cntrl::PatchExecutor *visiting_invocation = nullptr;

#if CNTRL_ORIGINAL_FIRMWARE
bool request_stock(uint8_t pair, const cntrl::SongContinuation *continuation);
#endif
class Board : public cntrl::Hardware, public cntrl::Replies, public cntrl::splash::Replies, public cntrl::midi::Sink, public cntrl::PatchOutputs
#if CNTRL_PANIC
    , public cntrl::PanicOutputs
#endif
#if CNTRL_MIDI_INTERFACE
    , public cntrl::MidiInterfaceControl
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
    bool state(unsigned operation, uint32_t value) override { return patch_state(operation, value); }
    bool stock(uint8_t pair, cntrl::PatchExecutor &invocation) override { return patch_stock(pair, invocation); }
    void validation(bool begin) override { controller.validation(begin); }
    void validation_context(cntrl::PatchExecutor &invocation, bool begin) override {
        if (begin && !restoring_invocation && !invocation.reconstructing())
            invocation_navigation[invocation.owner()] = controller.navigation();
        controller.validation(begin);
    }
    bool preview_state(unsigned operation, uint32_t value) override { return controller.preview_state(operation, value); }
    bool selection(unsigned operation, const cntrl::Document *&document, unsigned &node) override {
        return controller.selection(operation, document, node);
    }
    void selection_complete(unsigned operation) override { controller.selection_complete(operation); }
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
    void enter_panic() { bridge_.configure(false); panic.enter(runtime.brightness); }
    void leave_panic() { panic.leave(); bridge_.configure(runtime.interface); }
    bool transmit(const char *output, const uint8_t *bytes) override {
        const auto *connection = cntrl::midi::find_output(output);
        return connection && submit(connection->route, {uint8_t(bytes[0] >> 4), {bytes[0], bytes[1], bytes[2]}});
    }
#endif
#if CNTRL_MIDI_INTERFACE
    // Still Time 20: measured line height 21, no top adjustment.
    cntrl::MidiInterface midi_interface{*this, *this, {5, 20, 4, 0, 0xffff, 0, 21, 0}};
    void configure_interface(bool enabled) override { bridge_.configure(enabled); }
    void restore_interface() override { bridge_.configure(runtime.interface); }
#endif
#if CNTRL_CABLE_TEST
    // Still Time 28: measured line height 30, top-bearing adjustment 1.
    cntrl::CableTest cable_test{*this, {5, 28, 4, 0, 0xffff, 0, 30, 1}};
    void enter_cable_test() {
        bridge_.configure(false);
        cable_test.enter();
    }
    void leave_cable_test() {
        cable_test.leave();
        bridge_.configure(runtime.interface);
    }
#endif
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
        case cntrl::midi::OutputTransport::physical: return true;
        case cntrl::midi::OutputTransport::usb_device: return usb_configuration != 0;
        case cntrl::midi::OutputTransport::usb_host: return bool(host_midi);
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
    bool output_idle() const override { return bridge_.idle(); }
    void set_usb_midi_interface(bool enabled) override {
        runtime.interface = enabled;
#if CNTRL_SWITCHES
        if (switches.open()) return;
#endif
#if CNTRL_PANIC
        if (panic.open()) return;
#endif
#if CNTRL_MIDI_INTERFACE
        if (midi_interface.open()) return;
#endif
#if CNTRL_CABLE_TEST
        if (cable_test.open()) return;
#endif
        bridge_.configure(enabled);
    }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    bool send(const char *output, const uint8_t *data, unsigned length) override {
        const auto *connection = cntrl::midi::find_output(output);
        if (!connection || length > 14 || !available(output)) return false;
#if CNTRL_CABLE_TEST
        if (cable_test.open()) {
            if (connection->transport != cntrl::midi::OutputTransport::physical || length != 3) return false;
            return submit(connection->route, {11, {data[0], data[1], data[2]}});
        }
#endif
        return bridge_.pedal(connection->route, data, length);
    }
    bool submit(unsigned route, const cntrl::midi::Packet &p) override {
        if (route >= 6 && route < 8) return usb_midi_try_write_packed(p.packed(route - 6));
        if (route == 8) return host_midi.try_write_packed(p.packed());
        unsigned n = p.size();
        if (const auto *connection = cntrl::midi::physical_output(route)) {
            HardwareSerial *serial;
            switch (route) {
            case 0: serial = &Serial1; break;
            case 1: serial = &Serial2; break;
            default: return false;
            }
            if (unsigned(serial->availableForWrite()) < n) return false;
            if (serial->write(p.bytes, n) != n) return false;
#if CNTRL_MIDI_INTERFACE
            midi_interface.activity(unsigned(connection - cntrl::midi::output_connections.data()), true, millis());
#else
            (void)connection;
#endif
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
        std::snprintf(response, sizeof(response),
            "CNTRL MIDI %u %u %lu %lu %lu %lu %u %lu %lu %lu %lu %u",
            unsigned(bridge_.enabled()), route, (unsigned long)c.received,
            (unsigned long)c.transmitted, (unsigned long)c.dropped, (unsigned long)c.aborted,
            c.high_water, (unsigned long)max_midi_us_, (unsigned long)max_scan_gap_,
            (unsigned long)scan_count_, (unsigned long)display_count_, 0u);
        reply(response);
    }
    void scanned(uint32_t gap) {
        if (gap > max_scan_gap_) max_scan_gap_ = gap;
        ++scan_count_;
    }
    void show(unsigned pane, const cntrl::Pane &content) override {
        renderer_.show(pane, content);
    }
    void transition(bool undo, std::optional<unsigned> duration_ms) override { renderer_.transition(undo, duration_ms); }
    void set_brightness(uint8_t value, bool temporary = false) override {
        brightness_feedback_ = temporary;
        if (temporary) feedback_brightness_ = value;
        else { runtime.brightness = value; controller.runtime_brightness(value); }
        if (temporary) splash_session.stop();
        apply_brightness(millis());
    }
    void set_splash(bool enabled, uint32_t idle_ms) override {
        splash_session.configure(enabled, idle_ms, millis());
    }
    void set_animation_color_cycle(bool enabled) override {
        splash_panels.set_color_cycle(enabled);
    }
    void error(const char *message) override {
        renderer_.error(message);
    }
    bool reply_ready() const override { return !response_[response_offset_]; }
    void reply(const char *text) override {
        // Replies are bounded and only produced in response to host requests.
        std::snprintf(response_, sizeof(response_), "%s\n", text);
        response_offset_ = 0;
    }
    void poll() {
        host.Task();
        bridge_.host_connected(bool(host_midi));
        uint32_t begin = micros(), now = millis();
        bridge_.connected(usb_configuration != 0);
#if CNTRL_MIDI_INTERFACE
        midi_interface.connected(usb_configuration != 0);
#endif
        // Separate source limits plus an aggregate time budget. Rotate the
        // first source, so a saturated cable cannot starve later sources.
        for (unsigned i = 0; i < 3 && uint32_t(micros() - begin) < 180; ++i) {
            unsigned source = (source_cursor_ + i) % 3;
            for (unsigned n = 0; n < 64 && uint32_t(micros() - begin) < 180; ++n) {
                if (source == 0) {
                    uint32_t packet = usb_midi_read_message();
                    if (!packet) break;
#if CNTRL_CABLE_TEST
                    if (cable_test.open()) continue;
#endif
#if CNTRL_PANIC
                    if (panic.open()) continue;
#endif
                    // Omniport USB cables remain enumerated but are unused.
                    if (cntrl::midi::physical_output((packet >> 4) & 15)) bridge_.usb(packet, now);
                } else {
                    HardwareSerial &serial = source == 1 ? Serial1 : Serial2;
                    int byte = serial.read(); if (byte < 0) break;
#if CNTRL_CABLE_TEST
                    if (cable_test.open()) { cable_test.receive(source - 1, uint8_t(byte), now); continue; }
#endif
                    bool complete = bridge_.receive(source + 5, uint8_t(byte), now);
#if CNTRL_MIDI_INTERFACE
                    if (complete) midi_interface.activity(source - 1, false, now);
#else
                    (void)complete;
#endif
                }
            }
        }
        source_cursor_ = (source_cursor_ + 1) % 3;
        // Existing USB-host input is deliberately not bridged.
        host_midi.read();
#if CNTRL_PANIC
        if (panic.open()) { if (bridge_.handoff(*this)) panic.tick(); }
        else
#endif
#if CNTRL_CABLE_TEST
        if (cable_test.open()) { if (bridge_.handoff(*this)) cable_test.tick(now); }
        else
#endif
        bridge_.service(*this, now);
#if CNTRL_SWITCHES
        switches.tick();
#endif
#if CNTRL_MIDI_INTERFACE
        midi_interface.tick(millis());
#endif
        // Flush only already-buffered packets; this operation never waits.
        usb_midi_flush_output();
        uint32_t elapsed = micros() - begin;
        if (elapsed > max_midi_us_) max_midi_us_ = elapsed;
        if (response_[response_offset_] && Serial.availableForWrite() > 0) {
            unsigned n = std::strlen(response_ + response_offset_);
            unsigned room = Serial.availableForWrite();
            if (n > room) n = room;
            response_offset_ += Serial.write(response_ + response_offset_, n);
        }
    }
    void draw() {
        if (splash_session.take_redraw()) renderer_.invalidate();
        renderer_.draw(millis());
    }
    void display(uint32_t now, bool transfer_busy) {
        ++display_count_;
        bool splash = splash_session.poll(now, renderer_.ready(), transfer_busy);
        apply_brightness(now);
        if (!splash) draw();
    }
private:
    void apply_brightness(uint32_t now) {
        uint8_t brightness = brightness_feedback_ ? feedback_brightness_ : splash_session.brightness(runtime.brightness, now);
        if (brightness != applied_brightness_) {
            analogWrite(6, brightness); applied_brightness_ = brightness;
        }
    }
    bool brightness_feedback_ = false;
    uint8_t feedback_brightness_ = 255;
    int applied_brightness_ = -1;
    cntrl::midi::Bridge bridge_;
    unsigned source_cursor_ = 0;
    uint32_t max_midi_us_ = 0, max_scan_gap_ = 0, scan_count_ = 0, display_count_ = 0;
    DisplayRenderer renderer_;
    unsigned response_offset_ = 0;
    char response_[600] = {};
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
    bool next(const char *after, char *name) override {
        if (!ready) return false;
        auto root = SD.sdfs.open("/cntrl/setups", O_RDONLY);
        if (!root) return false;
        char best[64] = {}, candidate[128]; FsFile entry;
        while (entry.openNext(&root, O_RDONLY)) {
            if (entry.isDirectory() && entry.getName(candidate, sizeof(candidate))
                && cntrl::setup_name(candidate) && cntrl::name_compare(candidate, after) > 0
                && (!best[0] || cntrl::name_compare(candidate, best) < 0))
                std::snprintf(best, sizeof(best), "%.63s", candidate);
            entry.close();
        }
        std::snprintf(name, 64, "%s", best); return best[0];
    }
    void catalogue_begin() override {
        catalogue_.close(); catalogue_ = SD.sdfs.open("/cntrl/setups", O_RDONLY);
    }
    bool catalogue_step(char *name) override {
        name[0] = 0; FsFile entry; char candidate[128];
        if (!catalogue_ || !entry.openNext(&catalogue_, O_RDONLY)) { catalogue_.close(); return true; }
        if (entry.isDirectory() && entry.getName(candidate, sizeof(candidate)) && cntrl::setup_name(candidate))
            std::snprintf(name, 64, "%.63s", candidate);
        return false;
    }
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
private:
    FsFile file_, history_directory_, catalogue_;
    uint64_t history_before_ = 0;
    bool history_records_ = false;
    bool revision_ = false;
    char directory_[128] = {};
    void filename(unsigned slot, char *path) const { std::snprintf(path, 144, "%s/%u.bin", directory_, slot); }
};

Board board;
Card card;
cntrl::Controller controller(board, board, patch_sources);
// Keep document/transfer buffers out of RAM1's code and stack budget.
DMAMEM cntrl::Documents documents(card, board, controller, board);
cntrl::StoredPatchSources patch_sources(documents, board);
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
    ~SongSources() override { executor.cancel(); std::free(embedded_); }
    void tick() {
        executor.tick();
        if (embedded_ && !executor.busy()) {
            std::free(embedded_); embedded_ = nullptr; embedded_document_ = cntrl::Document();
        }
    }
    const cntrl::SongContinuation &context() const { return context_; }
    void recalled() override { controller.cancel_gesture(); }
    bool capture(cntrl::Song &song, bool reference) override {
        const auto &d = documents.document();
        if (!d.valid() || !d.patches() || documents.busy() || documents.dirty() || patch_work_pending()) return false;
        unsigned index = controller.selected_patch();
        const uint8_t *p = d.patch(index);
        std::snprintf(song.name, 64, "%s", reinterpret_cast<const char *>(p + 12));
        std::snprintf(song.label, 64, "%s", song.name);
        song.node = d.browser_root(index);
        if (reference) {
            const char *key = nullptr;
            const uint8_t *node = d.node(song.node);
            if (node[0] == 6 && !node[4]) key = reinterpret_cast<const char *>(node + 68);
            for (unsigned i = 0; !key && i < d.named_patches(); ++i)
                if (cntrl::u16(d.named_patch(i) + 64) == song.node) key = reinterpret_cast<const char *>(d.named_patch(i));
            if (!key) return false;
            std::snprintf(song.document, 64, "%s", documents.selected());
            std::snprintf(song.key, 64, "%s", key); song.source = 0xffffffff;
        } else song.source = 0xfffffffe;
        return true;
    }
    bool capture_source(const cntrl::Document *&source) override {
        if (documents.busy() || documents.dirty() || controller.busy() || songs.executing()) return false;
        source = &documents.document(); return source->valid();
    }
    bool revision(const cntrl::SongSelection &song, uint32_t &crc) override {
        patch_sources.begin();
        const cntrl::Document *source = nullptr;
        bool valid = song.document[0] && patch_sources.acquire(song.document, source, crc);
        patch_sources.end(); return valid;
    }
    bool activate(const cntrl::SongSelection &song, const cntrl::SongContinuation &context, bool resume) override {
        if (controller.busy() || executor.busy() || documents.busy() || documents.dirty()) return false;
        std::free(embedded_); embedded_ = nullptr;
        patch_sources.begin();
        const cntrl::Document *source = nullptr;
        unsigned node = song.node;
        uint32_t revision = 0;
        bool valid = false;
        if (song.document[0]) {
            valid = patch_sources.acquire(song.document, source, revision);
            if (valid) {
                int root = source->named_patch(song.key);
                valid = root >= 0; node = valid ? unsigned(root) : 0;
            }
        } else {
            unsigned size = songs.source_size(song.source);
            if (size && size <= cntrl::max_document) embedded_ = static_cast<uint8_t *>(std::malloc(size));
            if (embedded_ && songs.read_source(song.source, 0, embedded_, size)
                && embedded_document_.load(embedded_, size, board)) {
                source = &embedded_document_; valid = true;
            }
        }
        context_ = context;
        if (valid) {
            auto previous = controller.navigation();
            if (!resume) controller.external_selection();
            valid = resume ? node == context.node && executor.resume(*source, node, context.completed, context.sources_crc)
                           : executor.start(*source, node);
            if (!valid && !resume) controller.navigation(previous);
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
    uint8_t *embedded_ = nullptr;
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
#if CNTRL_PANIC
    panic_module,
#endif
#if CNTRL_ORIGINAL_FIRMWARE
    original_firmware_module,
#endif
#if CNTRL_CABLE_TEST
    cable_test_module,
#endif
#if CNTRL_MIDI_INTERFACE
    midi_interface_module,
#endif
};
const char *const module_names[] = {"Setups", "Songs and setlists",
#if CNTRL_SWITCHES
    "Switches",
#endif
#if CNTRL_PANIC
    "Panic",
#endif
#if CNTRL_ORIGINAL_FIRMWARE
    "Original firmware",
#endif
#if CNTRL_CABLE_TEST
    "MIDI cable test",
#endif
#if CNTRL_MIDI_INTERFACE
    "MIDI interface",
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
    auto state = inspect_boot_selection(reinterpret_cast<const volatile uint32_t *>(0x60084000));
    if (!can_enter_stock(state) || (continuation && !card.ready)) return false;
    firmware_visit = FirmwareVisitRecord(); firmware_exit_pair = pair; firmware_visit_offset = 0;
    if (continuation) {
        firmware_visit.token = state.next; firmware_visit.continuation = *continuation;
        firmware_visit.initial_navigation = invocation_navigation[visiting_invocation->owner()];
        firmware_visit.return_navigation = controller.navigation();
        firmware_visit.runtime = board.runtime; firmware_visit.ui_operation = pending_ui_operation;
        std::snprintf(firmware_visit.setup, 64, "%s", documents.selected());
        const auto &document = documents.document();
        firmware_visit.setup_crc = document.valid() ? cntrl::checksum(document.data(), document.length()) : 0;
        firmware_visit.crc = cntrl::checksum(reinterpret_cast<const uint8_t *>(&firmware_visit), sizeof(firmware_visit) - 4);
        firmware_visit_stage = 1;
    } else firmware_visit_stage = 5;
    return true;
}
bool start_original_firmware() { return controller.executor().invoke(cntrl::StockFirmwarePatch{33}); }
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
            if (!valid || !controller.navigation(firmware_visit.initial_navigation)) { board.error("Patch return unavailable"); return; }
            restoring_invocation = true;
            bool resumed = continuation.caller == 1 ? songs.resume(continuation)
                : controller.executor().resume(documents.document(), continuation.node, continuation.completed, continuation.sources_crc);
            restoring_invocation = false;
            if (!resumed || !controller.navigation(firmware_visit.return_navigation)) {
                controller.executor().cancel(); song_sources.executor.cancel();
                board.error("Patch return unavailable"); return;
            }
            invocation_navigation[5] = firmware_visit.initial_navigation;
            board.set_brightness(firmware_visit.runtime.brightness);
            board.set_usb_midi_interface(firmware_visit.runtime.interface);
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
OriginalFirmware original_firmware(board, start_original_firmware);
#endif
cntrl::ModuleMenu module_menu(board, module_names, sizeof(module_names) / sizeof(module_names[0]));
bool patch_work_pending() { return controller.busy() || songs.executing() || songs.busy(); }
bool patch_state(unsigned operation, uint32_t value) {
    if (operation == 1) { board.set_brightness(uint8_t(value)); return true; }
    if (operation == 2) { board.set_usb_midi_interface(value != 0); return true; }
    if (operation >= 3 && operation <= 8) {
        if ((operation == 7 || operation == 8) && (songs.executing() || documents.busy() || documents.dirty())) return false;
        return controller.navigate(operation);
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
        songs_ui.leave(); module_menu.enter(); module_controls_locked = true; return true;
    case UiOperation::performance_show:
        module_menu.leave(); songs_ui.leave(); documents.redraw(); module_controls_locked = true; return true;
    case UiOperation::menu_activate: {
        if (!module_menu.open()) return false;
        int selected = module_menu.button(2, true);
        if (selected == -1) return true;
        if (selected == songs_module) {
            if (!documents.busy() && !songs_transfer.busy() && board.output_idle()) { songs_ui.enter(); module_controls_locked = true; }
            else module_menu.enter();
        }
#if CNTRL_SWITCHES
        else if (selected == switches_module) {
            if (!documents.busy() && !documents.dirty() && !songs_transfer.busy() && board.output_idle() && documents.document().valid()) {
                board.enter_switches(documents.document(), held_controls);
            } else module_menu.enter();
        }
#endif
#if CNTRL_PANIC
        else if (selected == panic_module) {
            if (!documents.busy() && !songs_transfer.busy() && board.output_idle()) {
                board.enter_panic(); module_controls_locked = true;
            } else module_menu.enter();
        }
#endif
        else if (selected == setups_module) {
            if (documents.busy()) module_menu.enter();
            else { setup_ui.enter(); module_controls_locked = true; }
        }
#if CNTRL_ORIGINAL_FIRMWARE
        else if (selected == original_firmware_module) {
            original_firmware.enter(); module_controls_locked = true;
        }
#endif
#if CNTRL_CABLE_TEST
        else if (selected == cable_test_module) {
            if (!documents.busy() && board.output_idle()) {
                board.enter_cable_test(); module_controls_locked = true;
            } else module_menu.enter();
        }
#endif
#if CNTRL_MIDI_INTERFACE
        else if (selected == midi_interface_module) {
            if (!documents.busy() && board.output_idle()) {
                board.midi_interface.enter(usb_configuration != 0); module_controls_locked = true;
            } else module_menu.enter();
        }
#endif
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
#if CNTRL_CABLE_TEST
    case UiOperation::cable_toggle:
        if (!board.cable_test.open()) return false;
        board.cable_test.toggle(now); return true;
    case UiOperation::cable_back:
        if (!board.cable_test.open()) return false;
        board.leave_cable_test(); module_menu.enter(); module_controls_locked = true; return true;
#endif
#if CNTRL_MIDI_INTERFACE
    case UiOperation::interface_toggle:
        if (!board.midi_interface.open()) return false;
        board.midi_interface.toggle(); return true;
    case UiOperation::interface_back:
        if (!board.midi_interface.open()) return false;
        board.midi_interface.leave(); module_menu.enter(); module_controls_locked = true; return true;
#endif
#if CNTRL_PANIC
    case UiOperation::panic_begin:
    case UiOperation::panic_end:
        if (!board.panic.open()) return false;
        board.panic.button(operation == UiOperation::panic_begin); return true;
    case UiOperation::panic_back:
        if (!board.panic.open()) return false;
        board.leave_panic(); module_menu.enter(); module_controls_locked = true; return true;
#endif
#if CNTRL_SWITCHES
    case UiOperation::switches_return:
        board.enter_switches(documents.document(), 0); module_controls_locked = true; return true;
#endif
#if CNTRL_ORIGINAL_FIRMWARE
    case UiOperation::stock_confirm:
        if (!original_firmware.open()) return false;
        original_firmware.start(); return true;
    case UiOperation::stock_back:
        if (!original_firmware.open()) return false;
        original_firmware.leave(); module_menu.enter(); module_controls_locked = true; return true;
#endif
    default: return false;
    }
}
bool feature_open() {
    return module_menu.open() || setup_ui.open() || songs_ui.open()
#if CNTRL_SWITCHES
        || board.switches.open()
#endif
#if CNTRL_PANIC
        || board.panic.open()
#endif
#if CNTRL_ORIGINAL_FIRMWARE
        || original_firmware.open()
#endif
#if CNTRL_CABLE_TEST
        || board.cable_test.open()
#endif
#if CNTRL_MIDI_INTERFACE
        || board.midi_interface.open()
#endif
        ;
}
cntrl::splash::SdSlots splash_card;
cntrl::splash::Receiver splash_assets(splash_card, board);
cntrl::splash::Panels splash_panels;
cntrl::splash::Player splash_player(splash_assets, splash_panels);
cntrl::splash::Session splash_session(splash_assets, splash_player);

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
    if (!scan) { board.reply(response); return; }
    // The full report is streamed directly: Board's single pending reply is
    // deliberately not a queue and cannot hold a directory listing.
    Serial.println(response);
    auto root = SD.sdfs.open("/", O_RDONLY);
    unsigned entries = 0;
    if (!root || !list_storage(root, 0, entries) || device->errorCode()) {
        board.reply("CNTRL ERROR storage-list"); return;
    }
    Serial.printf("CNTRL STORAGE END %u\n", entries);
}

void midi_status(unsigned route) { board.midi_status(route); }
bool asset_command(const char *line) {
    if (board.configuration_locked()) { board.reply("CNTRL ERROR close-switches-first"); return true; }
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

void initialize_target() {
    initialize_switches();
    initialize_displays(false);
    board.begin();
    card.ready = SD.begin(BUILTIN_SDCARD);
    font_files.ready = card.ready;
    splash_card.ready = card.ready;
    splash_assets.boot();
    documents.boot();
    if (card.ready && !songs.boot()) board.error("Song storage invalid");
}

void poll_target() {
    uint32_t now = millis();
    if (uint32_t(now - last_scan) >= 3) {
        if (last_scan) board.scanned(now - last_scan);
        last_scan = now;
        uint8_t raw = read_buttons();
        uint8_t events[6], pressed = 0;
        for (unsigned i = 0; i < 6; ++i) {
            events[i] = buttons[i].update(!(raw & (1u << button_bits[i])), now);
            if (buttons[i].pressed) pressed |= 1u << i;
        }
        held_controls = pressed;
        bool consumed = splash_session.controls(pressed, now, !feature_open() && pressed == (1u << 2));
        if (consumed) controller.cancel_gesture();
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
                if (board.switches.open() && (events[i] & release_event)) board.switches.button(i, false);
#endif
                if (module_controls_locked) continue;
                if (module_menu.open()) {
                    if (events[i] & press_event) {
                        if (i == 0) controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::menu_previous)});
                        else if (i == 1) controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::menu_next)});
                        else if (i == 2) controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::menu_activate)});
                        else if (i == 5) controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::menu_back)});
                    }
#if CNTRL_CABLE_TEST
                } else if (board.cable_test.open()) {
                    if (events[i] & press_event) {
                        if (i == 1) controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::cable_toggle)});
                        else if (i == 5) controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::cable_back)});
                    }
#endif
#if CNTRL_MIDI_INTERFACE
                } else if (board.midi_interface.open()) {
                    if (events[i] & press_event) {
                        if (i == 1) controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::interface_toggle)});
                        else if (i == 5) controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::interface_back)});
                    }
#endif
#if CNTRL_ORIGINAL_FIRMWARE
                } else if (original_firmware.open()) {
                    if (events[i] & press_event) {
                        if (i == 2) controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::stock_confirm)});
                        else if (i == 5) controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::stock_back)});
                    }
#endif
#if CNTRL_SWITCHES
                } else if (board.switches.open()) {
                    if (events[i] & press_event) board.switches.button(i, true);
                    if (events[i] & release_event) board.switches.button(i, false);
#endif
                } else if (songs_ui.open()) {
                    constexpr UiOperation operations[] = {UiOperation::songs_previous, UiOperation::songs_next,
                        UiOperation::songs_confirm, UiOperation::songs_previous_section,
                        UiOperation::songs_next_section, UiOperation::songs_back};
                    if (events[i] & press_event) controller.executor().invoke(cntrl::StatePatch{unsigned(operations[i])});
#if CNTRL_PANIC
                } else if (board.panic.open()) {
                    if (i == 1) {
                        if (events[i] & press_event) controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::panic_begin)});
                        if (events[i] & release_event) controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::panic_end)});
                    } else if (i == 5 && (events[i] & press_event)) {
                        controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::panic_back)});
                    }
#endif
                } else if (setup_ui.open()) {
                    if (events[i] & press_event) {
                        if (i == 0) controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::setup_previous)});
                        else if (i == 1) controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::setup_next)});
                        else if (i == 2) controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::setup_confirm)});
                        else if (i == 5) controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::setup_back)});
                    }
                } else {
                    if ((events[i] & press_event) && controller.button(i, true, now) == cntrl::Chord::ad
                        && !patch_work_pending() && splash_session.management_allowed()) {
                        controller.cancel_gesture(); controller.executor().invoke(cntrl::StatePatch{9});
                    }
                    if (events[i] & release_event) controller.button(i, false, now);
                }
            }
        }
    }
    // A bounded amount of work per iteration; SD-card latency itself depends
    // on the card and must be measured on the pedal before release.
    for (unsigned i = 0; i < 256 && board.reply_ready() && Serial.available()
#if CNTRL_ORIGINAL_FIRMWARE
        && !firmware_visit_stage
#endif
        && serial_router.accepts_input(); ++i)
        serial_router.receive(Serial.read(), now);
    // Preserve the current serial frame under host backpressure. MIDI/buttons
    // continue while command input and reply-producing jobs wait for room.
    if (board.reply_ready()) serial_router.poll(now);
    if (board.reply_ready()) songs_transfer.poll(now);
    controller.tick(now);
    song_sources.tick();
    songs.tick();
    if (board.reply_ready()) font_files.poll(now, board);
    board.poll();
#if CNTRL_SWITCHES
    static bool switches_open = false;
    if (switches_open && !board.switches.open()) {
        board.leave_switches();
        if (pending_ui_operation == 10) controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::performance_show)});
        else controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::menu_open)});
        pending_ui_operation = 0; module_controls_locked = true;
        if (board.switches.abandoned()) board.error("Switch state unknown");
    }
    switches_open = board.switches.open();
#endif
    if (!patch_work_pending()
#if CNTRL_SWITCHES
        && !board.switches.open()
#endif
    ) {
        if (pending_ui_operation) {
            if (pending_ui_operation == 9) controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::menu_open)});
            else controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::performance_show)});
            pending_ui_operation = 0; return_to_switches = false; module_controls_locked = true;
        }
#if CNTRL_SWITCHES
        else if (return_to_switches) {
            return_to_switches = false;
            controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::switches_return)});
        }
#endif
    }
    board.display(now, documents.busy() || feature_open() || patch_work_pending());
}
