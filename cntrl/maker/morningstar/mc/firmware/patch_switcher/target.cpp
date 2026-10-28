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
#include <cntrl/setup_ui.h>
#include <cntrl/midi_bridge.h>
#include <cntrl/module_menu.h>
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
font_assets::Files font_files;
USBHost host;
MIDIDevice host_midi(host);
const char *outputs[] = {"din", "trs", "usb-device", "usb-host"};

#if CNTRL_ORIGINAL_FIRMWARE
bool request_stock(uint8_t pair, const cntrl::SongContinuation *continuation);
#endif
class Board : public cntrl::Hardware, public cntrl::Replies, public cntrl::splash::Replies, public cntrl::midi::Sink
#if CNTRL_PANIC
    , public cntrl::PanicOutputs
#endif
#if CNTRL_MIDI_INTERFACE
    , public cntrl::MidiInterfaceControl
#endif
{
public:
#if CNTRL_ORIGINAL_FIRMWARE
    bool supports_function(unsigned function) const override { return function == 1; }
    bool stock_firmware(uint8_t pair, const cntrl::SongContinuation *continuation = nullptr) override { return request_stock(pair, continuation); }
#endif
#if CNTRL_PANIC
    cntrl::Panic panic{*this, *this};
    void enter_panic() { bridge_.configure(false); panic.enter(brightness_); }
    void leave_panic() { panic.leave(); bridge_.configure(interface_enabled_); }
    bool available(cntrl::PanicPort port) const override {
        if (port == cntrl::PanicPort::usb_device) return usb_configuration != 0;
        if (port == cntrl::PanicPort::usb_host) return bool(host_midi);
        return true;
    }
    bool transmit(cntrl::PanicPort port, const uint8_t *bytes) override {
        static constexpr unsigned routes[] = {0, 1, 6, 8};
        return submit(routes[unsigned(port)], {uint8_t(bytes[0] >> 4), {bytes[0], bytes[1], bytes[2]}});
    }
#endif
#if CNTRL_MIDI_INTERFACE
    // Still Time 20: measured line height 21, no top adjustment.
    cntrl::MidiInterface midi_interface{*this, *this, {5, 20, 4, 0, 0xffff, 0, 21, 0}};
    void configure_interface(bool enabled) override { bridge_.configure(enabled); }
    void restore_interface() override { bridge_.configure(interface_enabled_); }
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
        bridge_.configure(interface_enabled_);
    }
#endif
    void begin() {
        Serial1.begin(31250);
        Serial2.begin(31250);
        host.begin();
    }
    bool supports(const char *output) const override {
        for (auto name : outputs) if (!std::strcmp(name, output)) return true;
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
        interface_enabled_ = enabled;
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
        unsigned route = 0;
        while (route < 4 && std::strcmp(outputs[route], output)) ++route;
        if (route == 4 || length > 14
            || (route == 2 && !usb_configuration) || (route == 3 && !host_midi)) return false;
#if CNTRL_CABLE_TEST
        if (cable_test.open()) {
            if (route >= 2 || length != 3) return false;
            return submit(route, {11, {data[0], data[1], data[2]}});
        }
#endif
        return bridge_.pedal(route == 2 ? 6 : route == 3 ? 8 : route, data, length);
    }
    bool submit(unsigned route, const cntrl::midi::Packet &p) override {
        if (route >= 6 && route < 8) return usb_midi_try_write_packed(p.packed(route - 6));
        if (route == 8) return host_midi.try_write_packed(p.packed());
        unsigned n = p.size();
        if (route < 2) {
            HardwareSerial &serial = route == 0 ? Serial1 : Serial2;
            if (unsigned(serial.availableForWrite()) < n) return false;
            if (serial.write(p.bytes, n) != n) return false;
#if CNTRL_MIDI_INTERFACE
            midi_interface.activity(route == 0 ? cntrl::MidiInterface::Port::din_out
                : cntrl::MidiInterface::Port::trs_out, millis());
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
        brightness_ = value; brightness_feedback_ = temporary;
        if (temporary) splash_session.stop();
        apply_brightness(millis());
    }
    void set_splash(bool enabled, uint32_t idle_ms) override {
        splash_session.configure(enabled, idle_ms, millis());
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
                    if (((packet >> 4) & 15) < 2) bridge_.usb(packet, now);
                } else {
                    HardwareSerial &serial = source == 1 ? Serial1 : Serial2;
                    int byte = serial.read(); if (byte < 0) break;
#if CNTRL_CABLE_TEST
                    if (cable_test.open()) { cable_test.receive(source - 1, uint8_t(byte), now); continue; }
#endif
                    bool complete = bridge_.receive(source + 5, uint8_t(byte), now);
#if CNTRL_MIDI_INTERFACE
                    if (complete) midi_interface.activity(source == 1 ? cntrl::MidiInterface::Port::din_in
                        : cntrl::MidiInterface::Port::trs_in, now);
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
        uint8_t brightness = brightness_feedback_ ? brightness_ : splash_session.brightness(brightness_, now);
        if (brightness != applied_brightness_) {
            analogWrite(6, brightness); applied_brightness_ = brightness;
        }
    }
    bool brightness_feedback_ = false;
    uint8_t brightness_ = 192;
    int applied_brightness_ = -1;
    cntrl::midi::Bridge bridge_;
    unsigned source_cursor_ = 0;
    uint32_t max_midi_us_ = 0, max_scan_gap_ = 0, scan_count_ = 0, display_count_ = 0;
    bool interface_enabled_ = false;
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
    FsFile file_, history_directory_;
    uint64_t history_before_ = 0;
    bool history_records_ = false;
    bool revision_ = false;
    char directory_[128] = {};
    void filename(unsigned slot, char *path) const { std::snprintf(path, 144, "%s/%u.bin", directory_, slot); }
};

Board board;
Card card;
cntrl::Controller controller(board);
// Keep document/transfer buffers out of RAM1's code and stack budget.
DMAMEM cntrl::Documents documents(card, board, controller, board);
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
class SongSources : public cntrl::SongResolver, public cntrl::SongCapture {
public:
    void recalled() override { controller.external_selection(); }
    bool capture(cntrl::Song &song, bool reference) override {
        const auto &d = documents.document(); if (!d.valid() || documents.busy()) return false;
        unsigned index = controller.selected_patch(); song.patch = cntrl::song_patch(d, index);
        const uint8_t *p = d.patch(index);
        std::snprintf(song.name, 64, "%s", reinterpret_cast<const char *>(p + 12));
        if (reference) {
            if (song.patch.function) return false;
            std::snprintf(song.document, 64, "%s", documents.selected());
            std::memcpy(song.instrument, d.instrument(cntrl::u16(p)), 32); song.bank = cntrl::u32(d.bank(index));
        }
        return true;
    }
    bool revision(const cntrl::SongSelection &song, uint32_t &crc) override {
        cntrl::Document d; if (!documents.reference_document(song.document, d)) return false;
        crc = cntrl::checksum(d.data(), d.length()); return true;
    }
    bool resolve(const cntrl::SongSelection &song, cntrl::SongPatch &patch) override {
        cntrl::Document d; if (!documents.reference_document(song.document, d)) return false;
        int found = -1;
        for (unsigned i = 0; i < d.patches(); ++i) {
            const uint8_t *p = d.patch(i);
            if (cntrl::u16(p) != 65535 && !std::strcmp(song.instrument, reinterpret_cast<const char *>(d.instrument(cntrl::u16(p))))
                && p[4] == song.patch.program && cntrl::u32(d.bank(i)) == song.bank) {
                auto value = cntrl::song_patch(d, i);
                if (found >= 0 && std::memcmp(&patch, &value, sizeof(value))) return false;
                found = int(i); patch = value;
            }
        }
        return found >= 0;
    }
};
SongFiles song_files;
SongSources song_sources;
DMAMEM cntrl::Songs songs(song_files, board, song_sources);
DMAMEM cntrl::SongsTransfer songs_transfer(songs, board);
cntrl::SongsUi songs_ui(songs, board, song_sources);
enum ModuleIndex { setups_module, songs_module,
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
    uint32_t magic = 0x46564931, token = 0, setup_crc = 0;
    char setup[64] = {};
    cntrl::SongContinuation continuation;
    uint32_t crc = 0;
};
FirmwareVisitRecord firmware_visit;
FsFile firmware_visit_file;
unsigned firmware_visit_stage = 0;
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
    firmware_visit = FirmwareVisitRecord(); firmware_exit_pair = pair;
    if (continuation) {
        firmware_visit.token = state.next; firmware_visit.continuation = *continuation;
        std::snprintf(firmware_visit.setup, 64, "%s", documents.selected());
        const auto &document = documents.document();
        firmware_visit.setup_crc = document.valid() ? cntrl::checksum(document.data(), document.length()) : 0;
        firmware_visit.crc = cntrl::checksum(reinterpret_cast<const uint8_t *>(&firmware_visit), sizeof(firmware_visit) - 4);
        firmware_visit_stage = 1;
    } else firmware_visit_stage = 5;
    return true;
}
bool start_original_firmware() { return request_stock(33, nullptr); }
void poll_firmware_visit(bool released) {
    auto fail = [&]() { firmware_visit_file.close(); firmware_visit_stage = 0; songs.cancel_function(); board.error("Firmware switch failed"); };
    if (firmware_visit_stage >= 8) {
        static uint32_t token = 0;
        static bool valid = false;
        switch (firmware_visit_stage) {
        case 8:
            token = firmware_visit.token;
            firmware_visit_file = SD.sdfs.open("/cntrl/firmware-visit.bin", O_RDONLY);
            valid = firmware_visit_file && firmware_visit_file.size() == sizeof(firmware_visit);
            break;
        case 9:
            valid = valid && firmware_visit_file.read(&firmware_visit, sizeof(firmware_visit)) == sizeof(firmware_visit);
            firmware_visit_file.close(); break;
        case 10: {
            const auto &document = documents.document();
            valid = valid && firmware_visit.magic == 0x46564931 && firmware_visit.token == token
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
            if (!valid || !songs.resume(continuation)) { board.error("Song return unavailable"); return; }
            controller.external_selection(); songs_ui.perform(); return;
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
        case 2:
            if (firmware_visit_file.write(&firmware_visit, sizeof(firmware_visit)) != sizeof(firmware_visit)) { fail(); return; }
            break;
        case 3:
            if (!firmware_visit_file.sync()) { fail(); return; }
            break;
        case 4: {
            FirmwareVisitRecord verify;
            bool good = firmware_visit_file.seekSet(0) && firmware_visit_file.read(&verify, sizeof(verify)) == sizeof(verify) && !std::memcmp(&verify, &firmware_visit, sizeof(verify));
            firmware_visit_file.close(); if (!good) { fail(); return; } break;
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
bool module_controls_locked = false;
bool feature_open() {
    return module_menu.open() || setup_ui.open() || songs_ui.open()
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
                if (module_controls_locked) continue;
                if (module_menu.open()) {
                    int selected = module_menu.button(i, events[i] & press_event);
                    if (selected == -2) { documents.redraw(); module_controls_locked = true; }
                    else if (selected == songs_module) {
                        if (!documents.busy() && !songs_transfer.busy() && board.output_idle()) { songs_ui.enter(); module_controls_locked = true; }
                        else module_menu.enter();
                    }
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
                            board.midi_interface.enter(usb_configuration != 0);
                            module_controls_locked = true;
                        } else module_menu.enter();
                    }
#endif
#if CNTRL_CABLE_TEST
                } else if (board.cable_test.open()) {
                    if (events[i] & press_event) {
                        if (i == 1) board.cable_test.toggle(now);
                        else if (i == 5) {
                            board.leave_cable_test(); module_menu.enter(); module_controls_locked = true;
                        }
                    }
#endif
#if CNTRL_MIDI_INTERFACE
                } else if (board.midi_interface.open()) {
                    if (events[i] & press_event) {
                        if (i == 1) board.midi_interface.toggle();
                        else if (i == 5) {
                            board.midi_interface.leave(); module_menu.enter(); module_controls_locked = true;
                        }
                    }
#endif
#if CNTRL_ORIGINAL_FIRMWARE
                } else if (original_firmware.open()) {
                    if (events[i] & press_event) {
                        if (i == 2) original_firmware.start();
                        else if (i == 5) { original_firmware.leave(); module_menu.enter(); module_controls_locked = true; }
                    }
#endif
                } else if (songs_ui.open()) {
                    if (events[i] & press_event) songs_ui.button(i, true);
                    if (!songs_ui.open()) { documents.redraw(); module_controls_locked = true; }
#if CNTRL_PANIC
                } else if (board.panic.open()) {
                    if (i == 1) {
                        if (events[i] & press_event) board.panic.button(true);
                        if (events[i] & release_event) board.panic.button(false);
                    } else if (i == 5 && (events[i] & press_event)) {
                        board.leave_panic(); module_menu.enter(); module_controls_locked = true;
                    }
#endif
                } else if (setup_ui.open()) {
                    if (!documents.busy()) {
                        if (events[i] & press_event) setup_ui.button(i, true, now);
                        if (events[i] & release_event) setup_ui.button(i, false, now);
                    }
                } else {
                    if ((events[i] & press_event) && controller.button(i, true, now) == cntrl::Chord::ad && splash_session.management_allowed()) { controller.cancel_gesture(); module_menu.enter(); }
                    if (events[i] & release_event) controller.button(i, false, now);
                }
            }
            if (!feature_open()) controller.tick(now);
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
    songs.tick();
    if (board.reply_ready()) font_files.poll(now, board);
    board.poll();
    board.display(now, documents.busy() || feature_open());
}
