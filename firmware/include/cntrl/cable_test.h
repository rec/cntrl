#pragma once
#include "hardware.h"
#include "midi_bridge.h"
#include <cstdio>

namespace cntrl {
// Owns test behavior only. The caller grants exclusive DIN/TRS access while open.
class CableTest {
public:
    explicit CableTest(Hardware &hardware, ScreenGraphics title = {}) : hardware_(hardware), title_(title) {}
    bool open() const { return open_; }
    bool enabled() const { return enabled_; }
    void enter() { open_ = true; enabled_ = false; reset(); render(); }
    void leave() { open_ = enabled_ = false; reset(); }
    void toggle(uint32_t now) {
        if (!open_) return;
        enabled_ = !enabled_; reset(); last_send_ = now - interval_ms;
        render();
    }
    void receive(unsigned input, uint8_t byte, uint32_t now) {
        if (!enabled_ || input >= 2) return;
        midi::Packet packet;
        if (!parsers_[input].receive(byte, packet) || packet.cin != 11
            || packet.bytes[1] != breath_controller) return;
        unsigned output = packet.bytes[0] - 0xb0;
        if (output >= 2) return;
        for (const auto &probe : probes_[output]) {
            if (probe.valid && probe.value == packet.bytes[2]
                && uint32_t(now - probe.sent) < timeout_ms) {
                bool changed = !seen_[output] || input_[output] != input;
                seen_[output] = true; input_[output] = input; received_[output] = now;
                if (changed) render();
                return;
            }
        }
    }
    void tick(uint32_t now) {
        if (!enabled_) return;
        bool changed = false;
        for (unsigned i = 0; i < 2; ++i) {
            if (seen_[i] && uint32_t(now - received_[i]) >= timeout_ms) {
                seen_[i] = false; changed = true;
            }
        }
        if (changed) render();
        if (uint32_t(now - last_send_) < interval_ms) return;
        last_send_ = now;
        for (unsigned i = 0; i < 2; ++i) {
            const uint8_t bytes[] = {uint8_t(0xb0 + i), breath_controller, value_[i]};
            if (hardware_.send(i ? "trs" : "din", bytes, sizeof(bytes))) {
                probes_[i][cursor_[i]] = {true, value_[i], now};
                cursor_[i] = (cursor_[i] + 1) % history;
                value_[i] = (value_[i] + 1) & 127;
            }
        }
    }
private:
    static constexpr uint8_t breath_controller = 2;
    static constexpr uint32_t interval_ms = 10, timeout_ms = 250;
    static constexpr unsigned history = 8;
    struct Probe { bool valid = false; uint8_t value = 0; uint32_t sent = 0; };
    Hardware &hardware_;
    ScreenGraphics title_;
    bool open_ = false, enabled_ = false, seen_[2] = {};
    unsigned input_[2] = {}, cursor_[2] = {};
    uint8_t value_[2] = {};
    uint32_t last_send_ = 0, received_[2] = {};
    Probe probes_[2][history];
    midi::Parser parsers_[2];
    void reset() {
        for (unsigned i = 0; i < 2; ++i) {
            seen_[i] = false; cursor_[i] = value_[i] = 0;
            parsers_[i].reset();
            for (auto &probe : probes_[i]) probe = Probe();
        }
    }
    void render() {
        Pane views[3];
        for (auto &view : views) {
            view.graphics.background = 0;
            view.graphics.foreground = 0xffff;
            for (auto &line : view.lines) {
                line.graphics = view.graphics;
                line.graphics.size = line.graphics.line_height = 16;
                line.centered = true;
            }
        }
        auto &center = views[1];
        std::snprintf(center.lines[0].text, 64, "MIDI cable test");
        center.lines[0].graphics = title_;
        center.lines[0].graphics.foreground = 0xffff;
        center.lines[0].graphics.background = 0;
        center.lines[0].y = 4;
        if (seen_[0]) std::snprintf(center.lines[1].text, 64, "%s", input_[0] ? "DIN to TRS" : "DIN");
        if (seen_[1]) std::snprintf(center.lines[2].text, 64, "%s", input_[1] ? "TRS" : "TRS to DIN");
        center.lines[1].y = 44;
        center.lines[2].y = 72;
        std::snprintf(center.lines[3].text, 64, "%s", enabled_ ? "On" : "Off");
        center.lines[3].y = hardware_.display_height() - 24;
        auto &back = views[2].lines[0];
        std::snprintf(back.text, 64, "Back");
        back.centered = false;
        back.x = hardware_.display_width() - 4 - 4 * 12;
        back.y = 4;
        for (unsigned i = 0; i < 3; ++i) hardware_.show(i, views[i]);
    }
};
}
