#include "target.h"
#include <Arduino.h>
#include <FlexSerial.h>
#include <SD.h>
#include <USBHost_t36.h>
#include <cntrl/documents.h>
#include <cntrl/setup_ui.h>
#include <cntrl/midi_bridge.h>
#include "buttons.h"
#include "displays.h"
#include "switches.h"
#include "renderer.h"
#include "storage_listing.h"
#include "../../splash/board.h"
#include "../../splash/router.h"

// Library graphics draw into RAM; only our bounded driver touches panel pins.

namespace {
extern cntrl::splash::Session splash_session;
USBHost host;
MIDIDevice host_midi(host);
FlexSerial port1(-1, 26), port2(-1, 38), port3(-1, 39), port4(-1, 14);
FlexSerial *ports[] = {&port1, &port2, &port3, &port4};
constexpr uint8_t ring_pins[] = {32, 9, 37, 36};
const char *outputs[] = {"din", "trs", "configurable-1", "configurable-2",
                         "configurable-3", "configurable-4", "usb-device", "usb-host"};

class Board : public cntrl::Hardware, public cntrl::Replies, public cntrl::splash::Replies, public cntrl::midi::Sink {
public:
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
    bool output_idle() const override { return bridge_.idle(); }
    void set_usb_midi_interface(bool enabled) override {
        bridge_.configure(enabled);
        if (enabled) for (unsigned i = 0; i < 4; ++i) enable_port(i);
    }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    bool send(const char *output, const uint8_t *data, unsigned length) override {
        unsigned route = 0;
        while (route < 8 && std::strcmp(outputs[route], output)) ++route;
        if (route == 8 || length > 14
            || (route == 6 && !usb_configuration) || (route == 7 && !host_midi)) return false;
        if (route >= 2 && route < 6 && !enable_port(route - 2)) return false;
        return bridge_.pedal(route == 7 ? 8 : route, data, length);
    }
    bool submit(unsigned route, const cntrl::midi::Packet &p) override {
        if (route >= 6 && route < 8) return usb_midi_try_write_packed(p.packed(route - 6));
        if (route == 8) return host_midi.try_write_packed(p.packed());
        unsigned n = p.size();
        if (route < 2) {
            HardwareSerial &serial = route == 0 ? Serial1 : Serial2;
            if (unsigned(serial.availableForWrite()) < n) return false;
            return serial.write(p.bytes, n) == n;
        }
        unsigned port = route - 2;
        if (!enabled_[port] || unsigned(ports[port]->availableForWrite()) < n) return false;
        return ports[port]->write(p.bytes, n) == n;
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
            (unsigned long)scan_count_, (unsigned long)display_count_, init_errors_);
        reply(response);
    }
    void scanned(uint32_t gap) {
        if (gap > max_scan_gap_) max_scan_gap_ = gap;
        ++scan_count_;
    }
    void show(unsigned pane, const cntrl::Pane &content) override {
        renderer_.show(pane, content);
    }
    void transition(bool undo, unsigned duration_ms) override { renderer_.transition(undo, duration_ms); }
    void set_brightness(uint8_t value) override {
        brightness_ = value;
    }
    void set_splash(bool enabled, uint32_t idle_ms) override {
        splash_session.configure(enabled, idle_ms, millis());
    }
    void error(const char *message) override {
        renderer_.error(message);
    }
    void reply(const char *text) override {
        // Replies are bounded and only produced in response to host requests.
        std::snprintf(response_, sizeof(response_), "%s\n", text);
        response_offset_ = 0;
    }
    void poll() {
        host.Task();
        uint32_t begin = micros(), now = millis();
        bridge_.connected(usb_configuration != 0);
        // Separate source limits plus an aggregate time budget. Rotate the
        // first source, so a saturated cable cannot starve later sources.
        for (unsigned i = 0; i < 3 && uint32_t(micros() - begin) < 180; ++i) {
            unsigned source = (source_cursor_ + i) % 3;
            for (unsigned n = 0; n < 64 && uint32_t(micros() - begin) < 180; ++n) {
                if (source == 0) {
                    uint32_t packet = usb_midi_read_message();
                    if (!packet) break;
                    bridge_.usb(packet, now);
                } else {
                    HardwareSerial &serial = source == 1 ? Serial1 : Serial2;
                    int byte = serial.read(); if (byte < 0) break;
                    bridge_.receive(source + 5, uint8_t(byte), now);
                }
            }
        }
        source_cursor_ = (source_cursor_ + 1) % 3;
        // Existing USB-host input is deliberately not bridged.
        host_midi.read();
        bridge_.service(*this, now);
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
        uint8_t brightness = splash_session.brightness(brightness_, now);
        if (brightness != applied_brightness_) {
            analogWrite(6, brightness); applied_brightness_ = brightness;
        }
        if (!splash) draw();
    }
private:
    uint8_t brightness_ = 192;
    int applied_brightness_ = -1;
    cntrl::midi::Bridge bridge_;
    unsigned source_cursor_ = 0, init_errors_ = 0;
    uint32_t max_midi_us_ = 0, max_scan_gap_ = 0, scan_count_ = 0, display_count_ = 0;
    bool enabled_[4] = {};
    DisplayRenderer renderer_;
    unsigned response_offset_ = 0;
    char response_[600] = {};
    bool enable_port(unsigned port) {
        if (enabled_[port]) return true;
        // Preload the reference before enabling the current loop.
        switch (port) {
            case 0: digitalWriteFast(32, HIGH); break;
            case 1: digitalWriteFast(9, HIGH); break;
            case 2: digitalWriteFast(37, HIGH); break;
            case 3: digitalWriteFast(36, HIGH); break;
        }
        pinMode(ring_pins[port], OUTPUT);
        ports[port]->begin(31250);
        enabled_[port] = bool(*ports[port]);
        if (!enabled_[port]) init_errors_ |= 1u << port;
        return enabled_[port];
    }
};

class Card : public cntrl::SetupStorage {
public:
    bool ready = false;
    bool available() const override { return ready && !SD.sdfs.card()->errorCode(); }
    void choose(const char *name) override {
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
        char path[112]; filename(slot, path);
        auto file = SD.sdfs.open(path, O_RDONLY);
        return file ? unsigned(file.size()) : 0;
    }
    bool read(unsigned slot, unsigned offset, uint8_t *data, unsigned length) override {
        if (!ready) return false;
        char path[112]; filename(slot, path);
        auto file = SD.sdfs.open(path, O_RDONLY);
        return file && file.seekSet(offset) && file.read(data, length) == int(length);
    }
    bool begin(unsigned slot) override {
        if (!ready) return false;
        if (!SD.sdfs.exists(directory_) && !SD.sdfs.mkdir(directory_, true)) return false;
        char path[112]; filename(slot, path);
        file_ = SD.sdfs.open(path, O_RDWR | O_CREAT | O_TRUNC);
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
    FsFile file_;
    char directory_[96] = {};
    void filename(unsigned slot, char *path) const { std::snprintf(path, 112, "%s/%u.bin", directory_, slot); }
};

Board board;
Card card;
cntrl::Controller controller(board);
// Keep document/transfer buffers out of RAM1's code and stack budget.
DMAMEM cntrl::Documents documents(card, board, controller, board);
cntrl::SetupUi setup_ui(documents, board);
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
cntrl::splash::Router serial_router(documents, splash_assets, splash_session, board, storage_info, midi_status);
ButtonState buttons[6];
uint32_t last_scan = 0;

} // namespace

void initialize_target() {
    initialize_switches();
    initialize_displays(false);
    board.begin();
    card.ready = SD.begin(BUILTIN_SDCARD);
    splash_card.ready = card.ready;
    splash_assets.boot();
    documents.boot();
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
        bool consumed = splash_session.controls(pressed, now);
        if (consumed) controller.cancel_gesture();
        setup_ui.poll(pressed);
        if (!consumed) {
            for (unsigned i = 0; i < 6; ++i) {
                if (setup_ui.open()) {
                    if (!documents.busy()) {
                        if (events[i] & press_event) setup_ui.button(i, true, now);
                        if (events[i] & release_event) setup_ui.button(i, false, now);
                    }
                } else {
                    if ((events[i] & press_event) && controller.button(i, true, now) == cntrl::Chord::ad && splash_session.management_allowed()) setup_ui.enter();
                    if (events[i] & release_event) controller.button(i, false, now);
                }
            }
            if (!setup_ui.open()) controller.tick(now);
        }
    }
    // A bounded amount of work per iteration; SD-card latency itself depends
    // on the card and must be measured on the pedal before release.
    for (unsigned i = 0; i < 256 && Serial.available() && serial_router.accepts_input(); ++i)
        serial_router.receive(Serial.read(), now);
    serial_router.poll(now);
    board.poll();
    board.display(now, documents.busy() || setup_ui.open());
}
