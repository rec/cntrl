#pragma once
#include "midi_bridge.h"
#include "midi_outputs.h"
#include <cstdio>

namespace cntrl {
// Owns test behavior only. The caller grants exclusive DIN/TRS access while enabled.
class CableTest {
public:
    struct Status { bool enabled = false; char routes[2][32] = {}; };
    explicit CableTest(midi::Sink &sink) : sink_(sink) {}
    Status status() const {
        Status result; result.enabled = enabled_;
        for (unsigned i = 0; i < output_count; ++i) if (seen_[i])
            std::snprintf(result.routes[i], sizeof(result.routes[i]), input_[i] == i ? "%s" : "%s to %s",
                midi::output_connections[i].label, input_names[input_[i]]);
        return result;
    }
    bool take_redraw() { bool result = dirty_; dirty_ = false; return result; }
    bool enabled() const { return enabled_; }
    void enter() { open_ = true; enabled_ = false; reset(); dirty_ = true; }
    void leave() { open_ = enabled_ = false; reset(); }
    void toggle(uint32_t now) {
        if (!open_) return;
        enabled_ = !enabled_; reset(); last_send_ = now - interval_ms;
        dirty_ = true;
    }
    void receive(unsigned input, uint8_t byte, uint32_t now) {
        if (!enabled_ || input >= input_names.size()) return;
        midi::Packet packet;
        if (!parsers_[input].receive(byte, packet) || packet.cin != 11
            || packet.bytes[1] != breath_controller) return;
        unsigned output = packet.bytes[0] - 0xb0;
        if (output >= output_count) return;
        for (const auto &probe : probes_[output]) {
            if (probe.valid && probe.value == packet.bytes[2]
                && uint32_t(now - probe.sent) < timeout_ms) {
                bool changed = !seen_[output] || input_[output] != input;
                seen_[output] = true; input_[output] = input; received_[output] = now;
                if (changed) dirty_ = true;
                return;
            }
        }
    }
    void tick(uint32_t now) {
        if (!enabled_) return;
        bool changed = false;
        for (unsigned i = 0; i < output_count; ++i) {
            if (seen_[i] && uint32_t(now - received_[i]) >= timeout_ms) {
                seen_[i] = false; changed = true;
            }
        }
        if (changed) dirty_ = true;
        if (uint32_t(now - last_send_) < interval_ms) return;
        last_send_ = now;
        for (unsigned i = 0; i < output_count; ++i) {
            const uint8_t bytes[] = {uint8_t(0xb0 + i), breath_controller, value_[i]};
            if (sink_.submit(midi::output_connections[i].route, {11, {bytes[0], bytes[1], bytes[2]}})) {
                probes_[i][cursor_[i]] = {true, value_[i], now};
                cursor_[i] = (cursor_[i] + 1) % history;
                value_[i] = (value_[i] + 1) & 127;
            }
        }
    }
private:
    static constexpr std::array<const char *, 2> input_names = {"DIN", "TRS"};
    static constexpr unsigned output_count = midi::output_connections.size();
    static constexpr uint8_t breath_controller = 2;
    static constexpr uint32_t interval_ms = 10, timeout_ms = 250;
    static constexpr unsigned history = 8;
    struct Probe { bool valid = false; uint8_t value = 0; uint32_t sent = 0; };
    midi::Sink &sink_;
    bool dirty_ = true;
    bool open_ = false, enabled_ = false, seen_[output_count] = {};
    unsigned input_[output_count] = {}, cursor_[output_count] = {};
    uint8_t value_[output_count] = {};
    uint32_t last_send_ = 0, received_[output_count] = {};
    Probe probes_[output_count][history];
    midi::Parser parsers_[input_names.size()];
    void reset() {
        for (auto &parser : parsers_) parser.reset();
        for (unsigned i = 0; i < output_count; ++i) {
            seen_[i] = false; cursor_[i] = value_[i] = 0;
            for (auto &probe : probes_[i]) probe = Probe();
        }
    }
};
}
