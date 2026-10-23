#include "target.h"
#include <Arduino.h>
#include <FlexSerial.h>
#include <SD.h>
#include <glcdfont.c>
#include <USBHost_t36.h>
#include <cntrl/documents.h>
#include <cntrl/layout.h>
#include "buttons.h"
#include "displays.h"
#include "switches.h"
#include "renderer.h"

// Only the pinned public-core font data is included, not the display library's
// driver or its DMA global constructors. Panel transactions are our shared code.

namespace {
USBHost host;
MIDIDevice host_midi(host);
FlexSerial port1(-1, 26), port2(-1, 38), port3(-1, 39), port4(-1, 14);
FlexSerial *ports[] = {&port1, &port2, &port3, &port4};
constexpr uint8_t ring_pins[] = {32, 9, 37, 36};
const char *outputs[] = {"din", "trs", "configurable-1", "configurable-2",
                         "configurable-3", "configurable-4", "usb-device", "usb-host"};

class Board : public cntrl::Hardware, public cntrl::Replies {
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
    void error(const char *message) override {
        cntrl::Pane view;
        std::snprintf(view.title, sizeof(view.title), "CNTRL");
        std::snprintf(view.patch, sizeof(view.patch), "%s", message);
        show(1, view);
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
        renderer_.draw(glcdfont);
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
ButtonState buttons[6];
uint32_t last_scan = 0;

} // namespace

void initialize_target() {
    initialize_switches();
    initialize_displays(false);
    board.begin();
    card.ready = SD.begin(BUILTIN_SDCARD);
    documents.boot();
    if (!card.ready) board.error("SD unavailable: no show");
}

void poll_target() {
    uint32_t now = millis();
    if (uint32_t(now - last_scan) >= 3) {
        last_scan = now;
        uint8_t raw = read_buttons();
        for (unsigned i = 0; i < 6; ++i) {
            uint8_t events = buttons[i].update(!(raw & (1u << button_bits[i])), now);
            if (events & press_event) controller.button(i, true, now);
            if (events & release_event) controller.button(i, false, now);
        }
        controller.tick(now);
    }
    // A bounded amount of work per iteration; SD-card latency itself depends
    // on the card and must be measured on the pedal before release.
    for (unsigned i = 0; i < 256 && Serial.available(); ++i) documents.receive(Serial.read(), now);
    documents.tick(now);
    board.poll();
    board.draw();
}
