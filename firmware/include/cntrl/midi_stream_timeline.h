#pragma once
#include "midi_bridge.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

namespace cntrl::midi {
struct PlayMidiFilePatch;
struct StreamFileSource {
    virtual ~StreamFileSource() = default;
    virtual void poll(uint32_t now, Sink &sink) = 0;
    virtual bool failed() const = 0;
    virtual bool finished() const = 0;
    virtual bool clock() const { return false; }
};
struct StreamFileProvider {
    virtual ~StreamFileProvider() = default;
    virtual std::unique_ptr<StreamFileSource> open_stream(const PlayMidiFilePatch &settings, unsigned route) = 0;
};

// A reversed release becomes an onset with the paired original velocity.
// Unmatched releases cannot manufacture notes.
inline bool reverse_note(Packet &packet, uint8_t onset_velocity) {
    const unsigned kind = packet.bytes[0] >> 4;
    if (kind != 8 && kind != 9) return true;
    const bool release = kind == 8 || packet.bytes[2] == 0;
    if (release && !onset_velocity) return false;
    packet.cin = release ? 9 : 8;
    packet.bytes[0] = uint8_t((release ? 0x90 : 0x80) | (packet.bytes[0] & 15));
    packet.bytes[2] = release ? onset_velocity : 0;
    return true;
}

// Uses rational phase accumulation so tempos do not accumulate integer-period
// drift. Late service returns at most one tick and counts skipped ticks.
class StreamClock {
public:
    void start(uint64_t now, unsigned bpm_milli) {
        previous_ = now; phase_ = 0; bpm_milli_ = bpm_milli;
        skipped_ = 0; running_ = bpm_milli != 0;
    }
    void stop() { running_ = false; }
    bool poll(uint64_t now) {
        if (!running_) return false;
        const uint64_t elapsed = now - previous_;
        previous_ = now;
        // Split long elapsed intervals before multiplying by tempo.
        const uint64_t whole = elapsed / denominator;
        const uint64_t remainder = elapsed % denominator;
        const uint64_t rate = uint64_t(bpm_milli_) * 24;
        const uint64_t phase = phase_ + remainder * rate;
        const uint64_t ticks = whole * rate + phase / denominator;
        phase_ = phase % denominator;
        if (!ticks) return false;
        skipped_ += ticks - 1;
        return true;
    }
    uint64_t skipped() const { return skipped_; }
private:
    static constexpr uint64_t denominator = 60000000000ull;
    uint64_t previous_ = 0, phase_ = 0, skipped_ = 0;
    unsigned bpm_milli_ = 0;
    bool running_ = false;
};

struct TimelinePacket {
    uint64_t time = 0;
    Packet packet;
    unsigned source = 0;
    uint64_t token = 0;
    uint64_t owner = 0;
};

// Bounded reversal of a finite sequence. SysEx packets belonging to one frame
// retain their byte order and take their placement from frame completion time.
// Capture code supplies boundary releases for notes still held at clip end.
inline bool reverse_sequence(std::vector<TimelinePacket> &events, uint64_t duration,
                             unsigned limit = 512) {
    if (events.size() > limit) return false;
    struct Voice { unsigned source; uint8_t channel, note, velocity; unsigned index; };
    struct Frame { unsigned source, group; bool open; };
    struct Ordered { TimelinePacket event; unsigned group, part, priority; };
    std::vector<Voice> voices;
    std::vector<Frame> frames;
    std::vector<Ordered> result;
    result.reserve(events.size());
    unsigned next_group = 0;
    for (const auto &event : events) {
        if (event.time > duration) return false;
        TimelinePacket reversed = event;
        unsigned kind = event.packet.bytes[0] >> 4;
        uint8_t velocity = 0;
        unsigned paired_index = unsigned(-1);
        if (kind == 9 && event.packet.bytes[2]) {
            voices.push_back({event.source, uint8_t(event.packet.bytes[0] & 15),
                event.packet.bytes[1], event.packet.bytes[2], unsigned(result.size())});
        } else if (kind == 8 || (kind == 9 && !event.packet.bytes[2])) {
            auto voice = std::find_if(voices.begin(), voices.end(), [&](const Voice &v) {
                return v.source == event.source && v.channel == (event.packet.bytes[0] & 15)
                    && v.note == event.packet.bytes[1];
            });
            if (voice == voices.end()) continue;
            velocity = voice->velocity;
            if (result[voice->index].event.time == duration - event.time) paired_index = voice->index;
            // Use the onset's identity for both sides of the reversed voice.
            reversed.token = result[voice->index].event.token;
            voices.erase(voice);
        }
        if (!reverse_note(reversed.packet, velocity)) continue;
        reversed.time = duration - event.time;
        unsigned group = next_group++, part = 0;
        unsigned priority = reversed.packet.bytes[0] >> 4 == 8 ? 0 : 2;
        if (paired_index != unsigned(-1)) {
            priority = 1;
            auto &release = result[paired_index];
            release.group = group; release.part = 1; release.priority = 1;
        }
        const unsigned cin = event.packet.cin;
        const bool sysex_packet = cin == 4 || (cin >= 5 && cin <= 7
            && event.packet.bytes[event.packet.size() - 1] == 0xf7);
        if (sysex_packet) {
            auto frame = std::find_if(frames.begin(), frames.end(), [&](const Frame &f) {
                return f.source == event.source && f.open;
            });
            if (event.packet.bytes[0] == 0xf0) {
                if (frame != frames.end()) return false;
                frames.push_back({event.source, group, true}); frame = frames.end() - 1;
            }
            if (frame == frames.end()) return false;
            group = frame->group;
            for (auto &previous : result) if (previous.group == group) {
                previous.event.time = reversed.time; ++part;
            }
            if (cin != 4) frame->open = false;
        }
        result.push_back({reversed, group, part, priority});
    }
    if (!voices.empty() || std::any_of(frames.begin(), frames.end(), [](const Frame &f) { return f.open; })) return false;
    std::stable_sort(result.begin(), result.end(), [](const Ordered &a, const Ordered &b) {
        if (a.event.time != b.event.time) return a.event.time < b.event.time;
        if (a.priority != b.priority) return a.priority < b.priority;
        if (a.group == b.group) return a.part < b.part;
        // Existing voices release first, then zero-duration pairs. Other
        // simultaneous events retain their original order.
        return a.group < b.group;
    });
    events.clear();
    for (const auto &item : result) events.push_back(item.event);
    return true;
}
} // namespace cntrl::midi
