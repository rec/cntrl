#pragma once
#include "midi_bridge.h"
#include "midi_outputs.h"
#include <array>
#include <vector>

namespace cntrl {
// The caller grants exclusive access to the supplied connections while enabled.
class CableTest {
public:
    struct Status {
        bool enabled = false;
        const uint8_t *sending = nullptr;
        const int *inputs = nullptr;
        unsigned output_count = 0, input_count = 0;
        const midi::OutputConnection *connections = nullptr;
    };
    CableTest(midi::Sink &sink, midi::Outputs outputs, unsigned inputs)
        : sink_(sink), outputs_(outputs), sending_(outputs.count), inputs_(inputs, -1),
          cursor_(outputs.count), value_(outputs.count), sent_(outputs.count), received_(inputs),
          probes_(outputs.count), parsers_(inputs) {}
    Status status() const { return {enabled_, sending_.data(), inputs_.data(), outputs_.count, unsigned(inputs_.size()), outputs_.data}; }
    const char *output_label(unsigned index) const { return index < outputs_.count ? outputs_.data[index].label : ""; }
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
        if (!enabled_ || input >= inputs_.size()) return;
        midi::Packet packet;
        if (!parsers_[input].receive(byte, packet) || packet.cin != 11
            || packet.bytes[1] != breath_controller) return;
        unsigned output = packet.bytes[0] - 0xb0;
        if (output >= outputs_.count) return;
        for (const auto &probe : probes_[output]) {
            if (probe.valid && probe.value == packet.bytes[2]
                && uint32_t(now - probe.sent) < timeout_ms) {
                int source = int(output);
                bool changed = inputs_[input] != source;
                inputs_[input] = source; received_[input] = now;
                if (changed) dirty_ = true;
                return;
            }
        }
    }
    void tick(uint32_t now) {
        if (!enabled_) return;
        bool changed = false;
        for (unsigned i = 0; i < outputs_.count; ++i) {
            if (sending_[i] && uint32_t(now - sent_[i]) >= timeout_ms) {
                sending_[i] = false; changed = true;
            }
        }
        for (unsigned i = 0; i < inputs_.size(); ++i) {
            if (inputs_[i] >= 0 && uint32_t(now - received_[i]) >= timeout_ms) {
                inputs_[i] = -1; changed = true;
            }
        }
        if (changed) dirty_ = true;
        if (uint32_t(now - last_send_) < interval_ms) return;
        last_send_ = now;
        for (unsigned i = 0; i < outputs_.count; ++i) {
            const uint8_t bytes[] = {uint8_t(0xb0 + i), breath_controller, value_[i]};
            if (sink_.submit(outputs_.data[i].route, {11, {bytes[0], bytes[1], bytes[2]}})) {
                if (!sending_[i]) dirty_ = true;
                sending_[i] = true; sent_[i] = now;
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
    midi::Sink &sink_;
    bool dirty_ = true;
    midi::Outputs outputs_;
    bool open_ = false, enabled_ = false;
    std::vector<uint8_t> sending_;
    std::vector<int> inputs_;
    std::vector<unsigned> cursor_;
    std::vector<uint8_t> value_;
    uint32_t last_send_ = 0;
    std::vector<uint32_t> sent_, received_;
    std::vector<std::array<Probe, history>> probes_;
    std::vector<midi::Parser> parsers_;
    void reset() {
        for (auto &parser : parsers_) parser.reset();
        for (auto &input : inputs_) input = -1;
        for (unsigned i = 0; i < outputs_.count; ++i) {
            sending_[i] = false; cursor_[i] = value_[i] = 0;
            for (auto &probe : probes_[i]) probe = Probe();
        }
    }
};
}
