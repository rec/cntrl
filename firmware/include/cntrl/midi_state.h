#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>
#include "midi_parameter.h"

namespace cntrl::midi {
struct NoteState {
    uint8_t note;
    std::optional<uint8_t> velocity, pressure;
};

struct ControllerState {
    uint8_t controller, value;
    bool known = true;
};

struct ParameterState {
    uint16_t parameter;
    std::optional<uint16_t> value;
    bool registered = false;
    std::optional<uint8_t> coarse = {};
};

struct ChannelState {
    uint8_t channel;
    std::vector<NoteState> notes;
    std::vector<ControllerState> controllers;
    std::optional<uint8_t> program, pressure;
    std::optional<uint16_t> pitch_bend;
    std::optional<uint8_t> bank_msb, bank_lsb;
    std::vector<ParameterState> parameters;
    bool nrpn = false, pitch_known = true;
};

struct OutputState {
    unsigned output;
    std::vector<ChannelState> channels;
};

// Last accepted channel messages, rather than a model of an instrument's voices.
// Channel numbers are zero based, as they are in MIDI status bytes.
class MidiState {
public:
    std::vector<OutputState> outputs;
    unsigned revision() const { return revision_; }
    void forget(unsigned output) {
        auto o = std::find_if(outputs.begin(), outputs.end(), [output](const auto &v) { return v.output == output; });
        if (o != outputs.end()) { outputs.erase(o); ++revision_; }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_state_value)
#endif
    int value(unsigned output, unsigned channel, unsigned control) const {
        auto o = std::find_if(outputs.begin(), outputs.end(), [output](const auto &v) { return v.output == output; });
        if (o == outputs.end()) return -1;
        auto c = std::find_if(o->channels.begin(), o->channels.end(), [channel](const auto &v) { return v.channel == channel; });
        if (c == o->channels.end()) return -1;
        if (control == 128) return c->program ? *c->program : -1;
        if (control == 129) return c->pitch_bend && c->pitch_known ? *c->pitch_bend : -1;
        if (control == 130) return c->bank_msb ? *c->bank_msb : -1;
        if (control == 131) return c->bank_lsb ? *c->bank_lsb : -1;
        if (control >= 256) {
            bool registered = control >= 33024, coarse = control >= 16640 && !registered;
            unsigned parameter = control - (registered ? 33024 : coarse ? 16640 : 256);
            for (const auto &p : c->parameters) if (p.registered == registered && p.parameter == parameter)
                return coarse ? p.coarse ? *p.coarse : -1 : p.value ? *p.value : -1;
            return -1;
        }
        for (const auto &v : c->controllers)
            if (v.controller == control && v.known) return v.value;
        return -1;
    }
    int parameter_value(unsigned output, const Parameter &parameter) const {
        for (const auto &o : outputs) if (o.output == output)
            for (const auto &c : o.channels) if (c.channel == parameter.channel) {
                if (parameter.kind == Parameter::cc7) { int v = value(output, c.channel, parameter.address); return v < 0 ? Parameter::unknown : v; }
                if (parameter.kind == Parameter::cc14) {
                    int hi = value(output, c.channel, parameter.address), lo = value(output, c.channel, parameter.address + 32);
                    return hi < 0 || lo < 0 ? Parameter::unknown : hi * 128 + lo;
                }
                if (parameter.kind == Parameter::bend) return c.pitch_known && c.pitch_bend ? int(*c.pitch_bend) - 8192 : Parameter::unknown;
                if (parameter.kind == Parameter::pressure) return c.pressure ? *c.pressure : Parameter::unknown;
                if (parameter.kind == Parameter::program) return c.program ? *c.program + 1 : Parameter::unknown;
                if (parameter.kind == Parameter::key_pressure)
                    for (const auto &n : c.notes) if (n.note == parameter.address) return n.pressure ? *n.pressure : Parameter::unknown;
                for (const auto &p : c.parameters) if (p.parameter == parameter.address && p.registered == (parameter.kind == Parameter::rpn14))
                    return parameter.kind == Parameter::nrpn7 ? p.coarse ? *p.coarse : Parameter::unknown : p.value ? *p.value : Parameter::unknown;
            }
        return Parameter::unknown;
    }
    void forget_parameter(unsigned output, const Parameter &parameter) {
        for (auto &o : outputs) if (o.output == output)
            for (auto &c : o.channels) if (c.channel == parameter.channel) {
                for (auto &v : c.controllers)
                    if ((parameter.kind == Parameter::cc7 && v.controller == parameter.address)
                        || (parameter.kind == Parameter::cc14 && (v.controller == parameter.address || v.controller == parameter.address + 32))) v.known = false;
                if (parameter.kind == Parameter::bend) c.pitch_known = false;
                if (parameter.kind == Parameter::pressure) c.pressure.reset();
                if (parameter.kind == Parameter::program) c.program.reset();
                if (parameter.kind == Parameter::key_pressure) for (auto &n : c.notes) if (n.note == parameter.address) n.pressure.reset();
                if (parameter.kind == Parameter::nrpn7 || parameter.kind == Parameter::nrpn14 || parameter.kind == Parameter::rpn14)
                    for (auto &p : c.parameters) if (p.parameter == parameter.address && p.registered == (parameter.kind == Parameter::rpn14)) { p.value.reset(); p.coarse.reset(); }
                ++revision_;
            }
    }
private:
    friend struct Sink;
    unsigned revision_ = 0;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_state_record)
#endif
    void record(unsigned output, const uint8_t *bytes, unsigned size) {
        if (size && (bytes[0] == 0xf0 || bytes[0] == 0xff)) { forget(output); return; }
        if (!size || bytes[0] < 0x80 || (bytes[0] >= 0xf0 && bytes[0] != 0xff)) return;
        auto o = std::find_if(outputs.begin(), outputs.end(), [output](const auto &v) { return v.output == output; });
        if (bytes[0] >= 0xf0) return;
        const unsigned kind = bytes[0] >> 4;
        if (size != (kind == 12 || kind == 13 ? 2u : 3u)) return;
        const uint8_t channel = bytes[0] & 15;
        const bool off = kind == 8 || (kind == 9 && !bytes[2]);
        if (o == outputs.end()) {
            if (off) return;
            outputs.push_back({output, {}}); o = outputs.end() - 1;
        }
        auto &channels = o->channels;
        auto c = std::find_if(channels.begin(), channels.end(), [channel](const auto &v) { return v.channel == channel; });
        if (c == channels.end()) {
            if (off) return;
            ChannelState state{}; state.channel = channel;
            channels.push_back(state); c = channels.end() - 1;
        }
        ++revision_;
        if (kind <= 10) {
            auto &notes = c->notes;
            auto n = std::find_if(notes.begin(), notes.end(), [bytes](const auto &v) { return v.note == bytes[1]; });
            if (off) {
                if (n != notes.end()) notes.erase(n);
            } else {
                if (n == notes.end()) { notes.push_back({bytes[1], {}, {}}); n = notes.end() - 1; }
                if (kind == 9) n->velocity = bytes[2];
                else n->pressure = bytes[2];
            }
        } else if (kind == 11) {
            const unsigned controller = bytes[1];
            if (controller == 120 || controller >= 123) c->notes.clear();
            if (controller == 121) {
                // Reset values depend on the receiver. Invalidate affected state
                // instead of claiming that the previous values still apply.
                c->controllers.clear(); c->pressure.reset(); c->pitch_bend.reset();
                for (auto &n : c->notes) n.pressure.reset();
                c->parameters.clear();
            }
            auto &controllers = c->controllers;
            auto v = std::find_if(controllers.begin(), controllers.end(), [controller](const auto &s) { return s.controller == controller; });
            if (v == controllers.end()) controllers.push_back({bytes[1], bytes[2]});
            else { v->value = bytes[2]; v->known = true; }
            if (controller == 99 || controller == 98 || controller == 101 || controller == 100) {
                c->nrpn = controller == 99 || controller == 98;
                for (auto &entry : controllers) if (entry.controller == 6 || entry.controller == 38) entry.known = false;
            }
            if (controller == 96 || controller == 97) {
                c->parameters.clear();
                for (auto &entry : controllers) if (entry.controller == 6 || entry.controller == 38) entry.known = false;
            }
            if (controller == 6 || controller == 38) {
                int high = value(output, channel, c->nrpn ? 99 : 101), low = value(output, channel, c->nrpn ? 98 : 100);
                if (high >= 0 && low >= 0 && (high != 127 || low != 127)) {
                    uint16_t parameter = uint16_t(high * 128 + low);
                    std::optional<uint16_t> data;
                    int msb = -1, lsb = -1;
                    for (const auto &entry : controllers) if (entry.known) {
                        if (entry.controller == 6) msb = entry.value;
                        if (entry.controller == 38) lsb = entry.value;
                    }
                    if (msb >= 0 && lsb >= 0) data = uint16_t(msb * 128 + lsb);
                    auto p = std::find_if(c->parameters.begin(), c->parameters.end(), [&](const auto &v) { return v.parameter == parameter && v.registered == !c->nrpn; });
                    if (p == c->parameters.end()) { c->parameters.push_back({parameter, data, !c->nrpn, {}}); p = c->parameters.end() - 1; }
                    else p->value = data;
                    p->coarse = msb < 0 ? std::nullopt : std::optional<uint8_t>(uint8_t(msb));
                }
            }
        } else if (kind == 12) {
            int msb = value(output, channel, 0), lsb = value(output, channel, 32);
            c->bank_msb = msb >= 0 ? std::optional<uint8_t>(uint8_t(msb)) : std::nullopt;
            c->bank_lsb = lsb >= 0 ? std::optional<uint8_t>(uint8_t(lsb)) : std::nullopt;
            c->program = bytes[1]; c->parameters.clear(); c->pitch_known = false;
            for (auto &entry : c->controllers) entry.known = entry.controller == 0 || entry.controller == 32;
        }
        else if (kind == 13) c->pressure = bytes[1];
        else { c->pitch_bend = uint16_t(bytes[1] | unsigned(bytes[2]) << 7); c->pitch_known = true; }
    }
};
} // namespace cntrl::midi
