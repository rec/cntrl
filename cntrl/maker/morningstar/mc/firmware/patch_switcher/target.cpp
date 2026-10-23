#include "target.h"
#include <Arduino.h>
#include <FlexSerial.h>
#include <SD.h>
#include <USBHost_t36.h>
#include <cntrl/documents.h>
#include "buttons.h"
#include "displays.h"
#include "switches.h"
#include "renderer.h"
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

class Board : public cntrl::Hardware, public cntrl::Replies, public cntrl::splash::Replies {
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
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    bool send(const char *output, const uint8_t *data, unsigned length) override {
        unsigned route = 0;
        while (route < 8 && std::strcmp(outputs[route], output)) ++route;
        if (route == 8 || length > 14 || queued_ == 32
            || (route == 6 && !usb_configuration) || (route == 7 && !host_midi)) return false;
        if (route >= 2 && route < 6 && !enabled_[route - 2]) {
            unsigned port = route - 2;
            // Preload the latch before enabling the current-loop reference.
            switch (port) {
                case 0: digitalWriteFast(32, HIGH); break;
                case 1: digitalWriteFast(9, HIGH); break;
                case 2: digitalWriteFast(37, HIGH); break;
                case 3: digitalWriteFast(36, HIGH); break;
            }
            pinMode(ring_pins[port], OUTPUT);
            ports[port]->begin(31250);
            enabled_[port] = bool(*ports[port]);
            if (!enabled_[port]) return false;
        }
        Batch &batch = queue_[(head_ + queued_) % 32];
        batch.route = route; batch.length = length;
        std::memcpy(batch.bytes, data, length);
        ++queued_;
        return true;
    }
    void show(unsigned pane, const cntrl::Pane &content) override {
        renderer_.show(pane, content);
    }
    void set_brightness(uint8_t value) override {
        analogWrite(6, value);
    }
    void set_splash(bool enabled) override {
        splash_session.configure(enabled);
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
        // No input forwarding. Drain MIDI RX so host enumeration remains serviced.
        usbMIDI.read(); host_midi.read();
        if (response_[response_offset_] && Serial.availableForWrite() > 0) {
            unsigned n = std::strlen(response_ + response_offset_);
            unsigned room = Serial.availableForWrite();
            if (n > room) n = room;
            response_offset_ += Serial.write(response_ + response_offset_, n);
        }
        if (queued_) {
            Batch &batch = queue_[head_];
            if (batch.route < 6) {
                if (batch.route < 2) {
                    HardwareSerial &serial = batch.route == 0 ? Serial1 : Serial2;
                    if (unsigned(serial.availableForWrite()) < batch.length) return;
                    serial.write(batch.bytes, batch.length);
                } else {
                    unsigned port = batch.route - 2;
                    if (unsigned(ports[port]->availableForWrite()) < batch.length) return;
                    ports[port]->write(batch.bytes, batch.length);
                }
            } else {
                // One complete patch sequence per queue item, maintaining order.
                for (unsigned i = 0; i < batch.length;) {
                    uint8_t status = batch.bytes[i++], value = batch.bytes[i++];
                    uint8_t extra = (status & 0xf0) == 0xb0 ? batch.bytes[i++] : 0;
                    if (batch.route == 6) usbMIDI.send(status & 0xf0, value, extra, (status & 15) + 1, 0);
                    else host_midi.send(status & 0xf0, value, extra, (status & 15) + 1);
                }
            }
            head_ = (head_ + 1) % 32; --queued_;
        }
    }
    void draw() {
        if (splash_session.take_redraw()) renderer_.invalidate();
        renderer_.draw();
    }
private:
    struct Batch { uint8_t bytes[14]; unsigned length, route; };
    Batch queue_[32] = {};
    unsigned head_ = 0, queued_ = 0;
    bool enabled_[4] = {};
    DisplayRenderer renderer_;
    unsigned response_offset_ = 0;
    char response_[128] = {};
};

class Card : public cntrl::Storage {
public:
    bool ready = false;
    unsigned size(unsigned slot) override {
        if (!ready) return 0;
        auto file = SD.sdfs.open(names_[slot], O_RDONLY);
        return file ? unsigned(file.size()) : 0;
    }
    bool read(unsigned slot, unsigned offset, uint8_t *data, unsigned length) override {
        if (!ready) return false;
        auto file = SD.sdfs.open(names_[slot], O_RDONLY);
        return file && file.seekSet(offset) && file.read(data, length) == int(length);
    }
    bool begin(unsigned slot) override {
        if (!ready) return false;
        file_ = SD.sdfs.open(names_[slot], O_RDWR | O_CREAT | O_TRUNC);
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
    const char *names_[2] = {"/cntrl-show-0.bin", "/cntrl-show-1.bin"};
};

Board board;
Card card;
cntrl::Controller controller(board);
cntrl::Documents documents(card, board, controller, board);
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
    board.reply(response);
}

cntrl::splash::Router serial_router(documents, splash_assets, splash_session, board, storage_info);
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
    if (!card.ready) board.error("SD unavailable: no show");
}

void poll_target() {
    uint32_t now = millis();
    if (uint32_t(now - last_scan) >= 3) {
        last_scan = now;
        uint8_t raw = read_buttons();
        uint8_t events[6], pressed = 0;
        for (unsigned i = 0; i < 6; ++i) {
            events[i] = buttons[i].update(!(raw & (1u << button_bits[i])), now);
            if (buttons[i].pressed) pressed |= 1u << i;
        }
        if (!splash_session.controls(pressed)) {
            for (unsigned i = 0; i < 6; ++i) {
                if (events[i] & press_event) controller.button(i, true, now);
                if (events[i] & release_event) controller.button(i, false, now);
            }
            controller.tick(now);
        }
    }
    // A bounded amount of work per iteration; SD-card latency itself depends
    // on the card and must be measured on the pedal before release.
    for (unsigned i = 0; i < 256 && Serial.available() && serial_router.accepts_input(); ++i)
        serial_router.receive(Serial.read(), now);
    serial_router.poll(now);
    board.poll();
    if (!splash_session.poll(now)) board.draw();
}
