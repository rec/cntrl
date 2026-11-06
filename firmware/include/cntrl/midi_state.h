#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace cntrl::midi {
struct NoteState {
    uint8_t note;
    std::optional<uint8_t> velocity, pressure;
};

struct ControllerState {
    uint8_t controller, value;
};

struct ChannelState {
    uint8_t channel;
    std::vector<NoteState> notes;
    std::vector<ControllerState> controllers;
    std::optional<uint8_t> program, pressure;
    std::optional<uint16_t> pitch_bend;
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
private:
    friend struct Sink;
    void record(unsigned output, const uint8_t *bytes, unsigned size) {
        if (!size || bytes[0] < 0x80 || (bytes[0] >= 0xf0 && bytes[0] != 0xff)) return;
        auto o = std::find_if(outputs.begin(), outputs.end(), [output](const auto &v) { return v.output == output; });
        if (size == 1 && bytes[0] == 0xff) {
            if (o != outputs.end()) outputs.erase(o);
            return;
        }
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
            channels.push_back({channel, {}, {}, {}, {}, {}}); c = channels.end() - 1;
        }
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
            }
            auto &controllers = c->controllers;
            auto v = std::find_if(controllers.begin(), controllers.end(), [controller](const auto &s) { return s.controller == controller; });
            if (v == controllers.end()) controllers.push_back({bytes[1], bytes[2]});
            else v->value = bytes[2];
        } else if (kind == 12) c->program = bytes[1];
        else if (kind == 13) c->pressure = bytes[1];
        else c->pitch_bend = uint16_t(bytes[1] | unsigned(bytes[2]) << 7);
    }
};
} // namespace cntrl::midi
