#pragma once
#include "checked_array.h"
#include "midi_bridge.h"
#include "midi_stream_timeline.h"
#include "numeric.h"
#include <algorithm>
#include <cstring>

namespace cntrl::midi {
struct MidiFile {
    virtual ~MidiFile() = default;
    virtual uint32_t size() const = 0;
    virtual bool read(uint32_t offset, uint8_t *data, unsigned length) = 0;
    virtual bool write(uint32_t offset, const uint8_t *data, unsigned length) = 0;
    virtual bool sync() = 0;
};
struct PlayMidiFilePatch {
    char filename[96] = {};
    uint16_t outputs = 1;
    uint32_t loops = 1, pre_delay = 0, post_delay = 0;
    double speed = 1; // Binary64 storage only; scheduling uses its integer representation.
    bool reverse = false;
    int8_t transpose = 0;
};
struct RecordMidiFilePatch { uint16_t channels = 0xffff, inputs = 0xffff; };

// SMF tracks are merged into fixed-size records on disk. RAM holds track heads
// and currently held notes, independently of the length of the sequence.
class MidiFilePlayer {
public:
    enum class Status { idle, indexing, playing, complete, failed };
    struct Event {
        uint64_t time = 0;
        uint32_t offset = 0, length = 0, previous = 0, next = 0;
        uint8_t status = 0, first = 0, second = 0, reverse_value = 0;
        bool complete_sysex = false, meta = false;
    };
    MidiFilePlayer(MidiFile &source, MidiFile &index) : source_(&source), index_(&index) {}
    bool bind(MidiFile &source, MidiFile &index) {
        if (active() || cleaning()) return false;
        source_ = &source; index_ = &index; return true;
    }
    uint64_t duration() const { return time_; }
    bool reserve_prepared() { return deliveries_.reserve(deliveries_.size() + 1); }
    unsigned trim_prepared_storage(unsigned budget = 1) {
        if (active() || cleaning()) return 0;
        unsigned retired = deliveries_.trim(budget);
        if (retired < budget) retired += notes_.trim(budget - retired);
        if (retired < budget) retired += sounding_.trim(budget - retired);
        if (retired < budget) retired += pedals_.trim(budget - retired);
        if (retired < budget && tracks_.capacity()) { tracks_.release(); ++retired; }
        return retired;
    }
    unsigned event_count() const { return events_; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_play_prepared)
#endif
    bool play_prepared(const PlayMidiFilePatch &settings, unsigned count, uint64_t duration) {
        if (active() || cleaning() || !settings.outputs || !duration || count > index_->size() / 32) return false;
        for (auto token : prepared_tokens_) if (token) return false;
        if (!remember_delivery()) return false;
        cancel(); settings_ = settings; events_ = count; time_ = duration;
        musical_ = prepared_ = true; has_clock_ = false; status_ = Status::playing;
        std::fill(std::begin(prepared_positions_), std::end(prepared_positions_), 0);
        return true;
    }
    bool prepared_done(unsigned output) const { return output < max_routes && prepared_positions_[output] >= events_; }
    void rewind_prepared() { std::fill(std::begin(prepared_positions_), std::end(prepared_positions_), 0); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_release_prepared)
#endif
    void release_prepared(Sink &sink, unsigned outputs = 0xffff) {
        for (unsigned i = 0; i < max_routes; ++i) if (outputs & (1u << i)) {
            if (prepared_tokens_[i]) sink.abandon_loop_group(i,prepared_tokens_[i]);
            prepared_tokens_[i] = 0;
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_finish_prepared)
#endif
    bool finish_prepared(unsigned output, uint64_t duration, Sink &sink) {
        if (output >= max_routes) return false;
        for (unsigned row = prepared_positions_[output]; row < events_; ++row) {
            Event event;
            if (!prepared_event(row,event) || event.time > duration) return false;
            unsigned kind = event.status >> 4;
            if (event.time < duration && kind != 8 && !(kind == 9 && !event.second)
                && !(kind == 11 && event.first >= 64 && event.first <= 69 && event.second < 64)) return false;
        }
        poll_prepared(duration,1u << output,sink);
        return prepared_done(output);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_poll_prepared)
#endif
    void poll_prepared(uint64_t position, unsigned available, Sink &sink) {
        if (status_ != Status::playing) return;
        unsigned work = 0, blocked = 0;
        while (work < 64) {
            bool progress = false;
            for (unsigned output = 0; output < max_routes && work < 64; ++output) if ((available & settings_.outputs & ~blocked) & (1u << output)) {
            auto &cursor = prepared_positions_[output];
            unsigned bit = 1u << output;
            if (cursor >= events_) { blocked |= bit; continue; }
            ++work;
            Event event;
            if (!prepared_event(cursor, event) || event.status < 0x80 || event.status >= 0xf0) { fail(); return; }
            if (event.time > position) { blocked |= bit; continue; }
            Packet packet{uint8_t(event.status >> 4), {event.status,event.first,event.second}};
            if (!event.previous) {
                if (sink.submit_loop(output, packet)) { ++cursor; progress = true; } else blocked |= bit;
                continue;
            }
            unsigned first = event.previous - 1, end = event.next - 1;
            if (first > cursor || end <= cursor || end > events_ || (end - first != 2 && end - first != 5 && end - first != 6)) { fail(); return; }
            Event a, b;
            if (!prepared_event(first,a) || !prepared_event(first + 1,b)) { fail(); return; }
            Parameter parameter;
            parameter.channel = a.status & 15;
            parameter.kind = end - first == 2 ? Parameter::cc14 : a.first == 99 ? (end - first == 5 ? Parameter::nrpn7 : Parameter::nrpn14) : Parameter::rpn14;
            parameter.address = uint16_t(end - first == 2 ? a.first : a.second * 128 + b.second);
            auto &token = prepared_tokens_[output];
            if (!token) token = (uint64_t(1) << 63) | ++prepared_serial_;
            if (sink.submit_loop_group(output,packet,parameter,token,cursor == first,cursor + 1 == end)) {
                ++cursor; progress = true; if (cursor == end) token = 0;
            } else blocked |= bit;
            }
            if (!progress) break;
        }
    }
    Status status() const { return status_; }
    bool active() const { return status_ == Status::indexing || status_ == Status::playing; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_cleaning)
#endif
    bool cleaning() const { return !active() && (!sounding_.empty() || !pedals_.empty() || terminate_outputs_); }
    unsigned owner() const { return owner_; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_delivery)
#endif
    Bridge::Delivery delivery(unsigned owner) const {
        if (musical_ || owner != owner_) {
            for (const auto &entry : deliveries_) if (entry.owner == owner) return entry.delivery;
            return Bridge::Delivery::idle;
        }
        if (active() || cleaning()) return Bridge::Delivery::pending;
        return status_ == Status::complete ? Bridge::Delivery::complete : Bridge::Delivery::cancelled;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_clock_outputs)
#endif
    unsigned clock_outputs() const { return active() && !musical_ && has_clock_ ? settings_.outputs : 0; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_sysex_outputs)
#endif
    unsigned sysex_outputs() const {
        return terminate_outputs_ | (active() && (parser_.sysex() || event_pending_ || (packet_pending_ && packet_.cin >= 4 && packet_.cin <= 7)) ? settings_.outputs : 0);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_start)
#endif
    bool start(const PlayMidiFilePatch &settings, unsigned owner = 0, bool musical = false) {
        if (active() || cleaning() || !remember_delivery()) return false;
        auto existing = std::find_if(deliveries_.begin(), deliveries_.end(), [owner](const auto &entry) { return entry.owner == owner; });
        if (existing == deliveries_.end() && !deliveries_.append({owner, Bridge::Delivery::idle})) return false;
        owner_ = owner;
        cancel(); settings_ = settings; has_clock_ = false; musical_ = musical; prepared_ = false;
        peak_notes_ = pedal_count_ = 0; std::memset(pedal_seen_, 0, sizeof(pedal_seen_));
        uint8_t header[14];
        if (!source_->read(0, header, sizeof(header)) || std::memcmp(header, "MThd", 4)
            || big(header + 4, 4) < 6 || big(header + 8, 2) > 1 || !big(header + 10, 2)
            || (big(header + 8, 2) == 0 && big(header + 10, 2) != 1)
            || !settings.outputs || !positive_finite_binary64(binary64_bits(settings.speed))) return fail();
        division_ = big(header + 12, 2);
        if (!division_) return fail();
        if (musical_ && (division_ & 0x8000)) return fail();
        if (division_ & 0x8000) {
            int fps = -int(int8_t(division_ >> 8));
            if (!(division_ & 255) || (fps != 24 && fps != 25 && fps != 29 && fps != 30)) return fail();
            numerator_ = fps == 29 ? 1001000000ull : 1000000ull;
            denominator_ = (fps == 29 ? 30000u : unsigned(fps)) * (division_ & 255);
        } else { numerator_ = musical_ ? uint64_t(1) << 32 : 500000; denominator_ = division_; }
        if (big(header + 4, 4) > source_->size() - 8) return fail();
        uint32_t offset = 8 + big(header + 4, 4);
        if (!tracks_.reserve(big(header + 10, 2))) return fail();
        for (unsigned i = 0; i < big(header + 10, 2); ++i) {
            uint8_t chunk[8];
            if (!source_->read(offset, chunk, 8) || std::memcmp(chunk, "MTrk", 4)) return fail();
            uint32_t length = big(chunk + 4, 4);
            if (offset > source_->size() - 8 || length > source_->size() - offset - 8) return fail();
            Track track; track.cursor = offset + 8; track.end = track.cursor + length;
            if (!tracks_.append(std::move(track))) return fail();
            offset = track.end;
        }
        events_ = loop_ = position_ = group_last_ = 0; time_ = tick_ = remainder_ = elapsed_ = 0;
        packet_pending_ = event_pending_ = clock_known_ = post_waiting_ = false;
        status_ = Status::indexing;
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_poll)
#endif
    void poll(uint32_t now, unsigned available, Sink &sink) {
        if (prepared_) return;
        if (!active()) {
            if (terminate_outputs_) {
                for (unsigned route = 0; route < max_routes; ++route)
                    if ((terminate_outputs_ & (1u << route)) && (!(available & (1u << route)) || sink.submit(route, {5, {0xf7, 0, 0}}))) terminate_outputs_ &= ~(1u << route);
                return;
            }
            release_one(available, sink);
            trim_prepared_storage(4);
            return;
        }
        if ((available & settings_.outputs) != settings_.outputs && (!musical_ || status_ != Status::indexing)) { fail(); return; }
        if (status_ == Status::indexing) {
            for (unsigned work = 0; work < 32 && status_ == Status::indexing; ++work) index_one();
            return;
        }
        if (!clock_known_) { previous_ = now; clock_known_ = true; }
        elapsed_ += uint32_t(now - previous_); previous_ = now;
        if (elapsed_ < uint64_t(settings_.pre_delay) * 1000000) return;
        uint64_t progress = scale_binary64(elapsed_ - uint64_t(settings_.pre_delay) * 1000000,binary64_bits(settings_.speed));
        // One packet per poll, retried independently on each destination.
        if (packet_pending_) {
            bool owned = packet_.cin == 8 || packet_.cin == 9
                || (packet_.cin == 11 && packet_.bytes[1] >= 64 && packet_.bytes[1] <= 69);
            packet_.source = owned ? uint16_t(0xe000 + owner_) : 65535;
            for (unsigned route = 0; route < max_routes; ++route)
                if ((pending_outputs_ & (1u << route)) && prepare_packet(route) && sink.submit(route, packet_)) {
                    pending_outputs_ &= ~(1u << route);
                    remember_packet(route);
                }
            if (pending_outputs_) return;
            packet_pending_ = false;
            return;
        }
        if (event_pending_) { stream_event(); return; }
        if (position_ < events_) {
            Event event;
            if (!read_event(*index_, settings_.reverse ? events_ - position_ - 1 : position_, event)) { fail(); return; }
            uint64_t due = settings_.reverse ? time_ - event.time : event.time;
            if (progress < due) return;
            ++position_;
            if (event.status == 0xf0 || event.status == 0xf7) {
                if (settings_.reverse && event.next) return;
                if (settings_.reverse && !event.complete_sysex) { fail(); return; }
                stream_ = event; stream_offset_ = 0; event_pending_ = true;
                seeking_group_ = settings_.reverse && event.previous;
                if (!seeking_group_ && event.status == 0xf0) parser_.receive(0xf0, packet_);
                return;
            }
            uint8_t cin = event.status < 0xf0 ? event.status >> 4
                : event.status == 0xf1 || event.status == 0xf3 ? 2 : event.status == 0xf2 ? 3 : event.status == 0xf6 ? 5 : 15;
            packet_ = {cin, {event.status, event.first, event.second}};
            unsigned kind = event.status >> 4;
            if (kind == 8 || kind == 9 || kind == 10) {
                int note = int(event.first) + settings_.transpose;
                if (note < 0 || note > 127) return;
                packet_.bytes[1] = uint8_t(note);
                if (settings_.reverse && (kind == 8 || kind == 9)) {
                    if (!reverse_note(packet_, event.reverse_value)) return;
                }
            }
            if (settings_.reverse && kind == 11 && event.first >= 64 && event.first <= 69)
                packet_.bytes[2] = event.reverse_value;
            pending_outputs_ = settings_.outputs; packet_pending_ = true;
            return;
        }
        if (progress < time_) return;
        if (!release_one(available, sink)) return;
        if (!post_waiting_) { post_start_ = elapsed_; post_waiting_ = true; }
        if (elapsed_ - post_start_ < uint64_t(settings_.post_delay) * 1000000) return;
        if (!settings_.loops || ++loop_ < settings_.loops) {
            position_ = 0; elapsed_ = 0; parser_.reset(); post_waiting_ = false;
        } else status_ = Status::complete;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_cancel)
#endif
    void cancel() { terminate_outputs_ |= sysex_outputs(); tracks_.clear(); notes_.clear(); std::memset(controls_, 0, sizeof(controls_)); parser_.reset(); status_ = Status::idle; prepared_ = false; }
private:
    struct Track { uint32_t cursor = 0, end = 0; uint64_t tick = 0; uint8_t running = 0; Event head; bool ready = false, ended = false; };
    struct Held { uint8_t channel, note, velocity; };
    struct Sounding { unsigned output; uint8_t channel, note; };
    CheckedList<Sounding> sounding_;
    struct Pedal { unsigned output; uint8_t channel, controller; };
    CheckedList<Pedal> pedals_;
    MidiFile *source_, *index_;
    PlayMidiFilePatch settings_;
    CheckedArray<Track> tracks_;
    CheckedList<Held> notes_;
    unsigned peak_notes_ = 0, pedal_count_ = 0;
    uint8_t pedal_seen_[16] = {};
    uint8_t controls_[16][6] = {};
    Status status_ = Status::idle;
    unsigned owner_ = 0;
    struct OwnerDelivery { unsigned owner; Bridge::Delivery delivery; };
    CheckedList<OwnerDelivery> deliveries_;
    bool remember_delivery() {
        auto previous = delivery(owner_);
        for (auto &entry : deliveries_) if (entry.owner == owner_) { entry.delivery = previous; return true; }
        return previous == Bridge::Delivery::idle || deliveries_.append({owner_, previous});
    }
    unsigned division_ = 0, denominator_ = 1;
    uint64_t numerator_ = 500000, remainder_ = 0, tick_ = 0, time_ = 0, elapsed_ = 0;
    uint32_t events_ = 0, position_ = 0, loop_ = 0, previous_ = 0, stream_offset_ = 0;
    unsigned prepared_positions_[max_routes] = {};
    uint64_t prepared_tokens_[max_routes] = {}, prepared_serial_ = 0;
    unsigned pending_outputs_ = 0, terminate_outputs_ = 0;
    uint64_t post_start_ = 0;
    bool post_waiting_ = false;
    uint32_t group_last_ = 0;
    bool seeking_group_ = false, has_clock_ = false;
    bool clock_known_ = false, packet_pending_ = false, event_pending_ = false, musical_ = false, prepared_ = false;
    Packet packet_;
    Parser parser_;
    Event stream_;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_remember)
#endif
    void remember_packet(unsigned route) {
        unsigned kind = packet_.bytes[0] >> 4;
        uint8_t channel = packet_.bytes[0] & 15;
        if (kind == 9 && packet_.bytes[2]) (void)sounding_.append({route, channel, packet_.bytes[1]});
        if (kind == 8 || (kind == 9 && !packet_.bytes[2])) {
            auto n = std::find_if(sounding_.begin(), sounding_.end(), [&](const auto &v) { return v.output == route && v.channel == channel && v.note == packet_.bytes[1]; });
            if (n != sounding_.end()) sounding_.erase(n);
        }
        if (kind == 11 && packet_.bytes[1] >= 64 && packet_.bytes[1] <= 69) {
            auto p = std::find_if(pedals_.begin(), pedals_.end(), [&](const auto &v) { return v.output == route && v.channel == channel && v.controller == packet_.bytes[1]; });
            if (packet_.bytes[2] >= 64) {
                if (p == pedals_.end()) (void)pedals_.append({route, channel, packet_.bytes[1]});
            } else if (p != pedals_.end()) pedals_.erase(p);
        }
    }
    bool prepare_packet(unsigned route) {
        const unsigned kind = packet_.bytes[0] >> 4;
        if (kind == 9 && packet_.bytes[2]) return sounding_.reserve(sounding_.size() + 1);
        if (kind == 11 && packet_.bytes[1] >= 64 && packet_.bytes[1] <= 69 && packet_.bytes[2] >= 64) {
            for (const auto &entry : pedals_)
                if (entry.output == route && entry.channel == (packet_.bytes[0] & 15) && entry.controller == packet_.bytes[1]) return true;
            return pedals_.reserve(pedals_.size() + 1);
        }
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_release)
#endif
    bool release_one(unsigned available, Sink &sink) {
        // Ownership for cleanup is separate from the sink's complete MIDI state.
        if (!pedals_.empty()) {
            auto p = pedals_.back();
            if (!(available & (1u << p.output)) || sink.submit(p.output, {11, {uint8_t(0xb0 | p.channel), p.controller, 0}, uint16_t(0xe000 + owner_)})) pedals_.pop_back();
            return false;
        }
        if (!sounding_.empty()) {
            auto n = sounding_.back();
            if (!(available & (1u << n.output)) || sink.submit(n.output, {8, {uint8_t(0x80 | n.channel), n.note, 0}, uint16_t(0xe000 + owner_)})) sounding_.pop_back();
            return false;
        }
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_big)
#endif
    static uint32_t big(const uint8_t *p, unsigned n) { uint32_t v = 0; while (n--) v = (v << 8) | *p++; return v; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_fail)
#endif
    bool fail() { terminate_outputs_ |= sysex_outputs(); parser_.reset(); status_ = Status::failed; return false; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_byte)
#endif
    bool byte(Track &track, uint8_t &v) { return track.cursor < track.end && source_->read(track.cursor++, &v, 1); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_variable)
#endif
    bool variable(Track &track, uint32_t &value) {
        value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            uint8_t b;
            if (!byte(track, b)) return false;
            value = (value << 7) | (b & 127);
            if (!(b & 128)) return true;
        }
        return false;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_next)
#endif
    bool next(Track &track) {
        if (track.ended) return true;
        uint32_t delta; uint8_t status;
        if (!variable(track, delta) || !byte(track, status)) return false;
        track.tick += delta;
        Event event;
        if (status < 0x80) { if (!track.running) return false; --track.cursor; status = track.running; }
        event.status = status;
        if (status < 0xf0) {
            track.running = status;
            if (!byte(track, event.first) || event.first > 127) return false;
            if (status >> 4 != 12 && status >> 4 != 13 && (!byte(track, event.second) || event.second > 127)) return false;
        } else {
            track.running = 0; event.meta = status == 0xff;
            if (status == 0xff && !byte(track, event.first)) return false;
            if (status != 0xff && status != 0xf0 && status != 0xf7) return false;
            if (!variable(track, event.length) || event.length > track.end - track.cursor) return false;
            event.offset = track.cursor; track.cursor += event.length;
            if (status == 0xff && event.first == 0x2f) {
                if (event.length) return false;
                track.ended = true;
            }
            if ((status == 0xf0 || status == 0xf7) && event.length) {
                uint8_t last;
                if (!source_->read(event.offset + event.length - 1, &last, 1)) return false;
                event.complete_sysex = last == 0xf7;
            }
            if (status == 0xf7 && event.length && event.length <= 3) {
                uint8_t raw[3] = {};
                if (!source_->read(event.offset, raw, event.length)) return false;
                if (raw[0] != 0xf0 && raw[0] != 0xf7 && message_valid(raw, event.length)) {
                    event.status = raw[0]; event.first = raw[1]; event.second = raw[2];
                }
            }
        }
        track.head = event; track.ready = true;
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_index)
#endif
    void index_one() {
        Track *chosen = nullptr;
        for (auto &track : tracks_) {
            if (!track.ready && !track.ended && !next(track)) { fail(); return; }
            if (track.ready && (!chosen || track.tick < chosen->tick)) chosen = &track;
        }
        if (!chosen) {
            if (group_last_) { fail(); return; }
            if (!notes_.empty()) {
                auto note = notes_.back(); notes_.pop_back();
                Event release; release.time = time_; release.status = uint8_t(0x80 | note.channel);
                release.first = note.note; release.reverse_value = note.velocity;
                append_event(release); return;
            }
            if (settings_.reverse) for (unsigned channel = 0; channel < 16; ++channel)
                for (unsigned controller = 0; controller < 6; ++controller) if (controls_[channel][controller]) {
                    Event release; release.time = time_; release.status = uint8_t(0xb0 | channel);
                    release.first = uint8_t(64 + controller); release.reverse_value = controls_[channel][controller];
                    controls_[channel][controller] = 0;
                    append_event(release); return;
                }
            if (!index_->sync()) { fail(); return; }
            unsigned outputs = 0;
            for (unsigned mask = settings_.outputs; mask; mask >>= 1) outputs += mask & 1;
            if (peak_notes_ > UINT32_MAX / outputs || pedal_count_ > UINT32_MAX / outputs
                || !sounding_.reserve(peak_notes_ * outputs) || !pedals_.reserve(pedal_count_ * outputs)) { fail(); return; }
            notes_.clear(); tracks_.release(); status_ = Status::playing; return;
        }
        uint64_t delta = chosen->tick - tick_;
        if (delta > (UINT64_MAX - remainder_) / numerator_) { fail(); return; }
        uint64_t duration = delta * numerator_ + remainder_;
        if (duration / denominator_ > UINT64_MAX - time_) { fail(); return; }
        time_ += duration / denominator_; remainder_ = duration % denominator_; tick_ = chosen->tick;
        Event &event = chosen->head; chosen->ready = false;
        if (event.meta) {
            if (event.first == 0x51) {
                uint8_t tempo[3];
                if (event.length != 3 || !source_->read(event.offset, tempo, 3) || !big(tempo, 3)) { fail(); return; }
                if (!musical_ && !(division_ & 0x8000)) numerator_ = big(tempo, 3);
            }
            return;
        }
        event.time = time_;
        if (event.status == 0xf8) has_clock_ = true;
        unsigned kind = event.status >> 4;
        if (kind == 9 && event.second) {
            if (!notes_.append({uint8_t(event.status & 15), event.first, event.second})) { fail(); return; }
            peak_notes_ = std::max(peak_notes_, notes_.size());
        }
        if (kind == 8 || (kind == 9 && !event.second)) {
            auto note = notes_.end();
            for (auto i = notes_.begin(); i != notes_.end(); ++i)
                if (i->channel == (event.status & 15) && i->note == event.first) note = i;
            if (note != notes_.end()) { event.reverse_value = note->velocity; notes_.erase(note); }
        }
        if (kind == 11 && event.first >= 64 && event.first <= 69) {
            const unsigned channel = event.status & 15, bit = 1u << (event.first - 64);
            if (!(pedal_seen_[channel] & bit)) { pedal_seen_[channel] |= uint8_t(bit); ++pedal_count_; }
            auto &value = controls_[event.status & 15][event.first - 64];
            event.reverse_value = value; value = event.second;
        }
        if (event.status == 0xf0) {
            if (group_last_) { fail(); return; }
            if (!event.complete_sysex) group_last_ = events_ + 1;
        } else if (event.status == 0xf7 && group_last_) {
            event.previous = group_last_;
            uint32_t next = events_ + 1;
            uint8_t link[] = {uint8_t(next), uint8_t(next >> 8), uint8_t(next >> 16), uint8_t(next >> 24)};
            if (!index_->write((group_last_ - 1) * 32 + 28, link, 4)) { fail(); return; }
            group_last_ = event.complete_sysex ? 0 : next;
        }
        append_event(event);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_append)
#endif
    void append_event(const Event &event) {
        if (events_ == UINT32_MAX / 32 || !write_event(*index_, events_, event)) { fail(); return; }
        ++events_;
    }
public:
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_write_event)
#endif
    static bool write_event(MidiFile &index, uint32_t position, const Event &event) {
        uint8_t record[32] = {};
        for (unsigned i = 0; i < 8; ++i) record[i] = uint8_t(event.time >> (i * 8));
        for (unsigned i = 0; i < 4; ++i) { record[8 + i] = uint8_t(event.offset >> (i * 8)); record[12 + i] = uint8_t(event.length >> (i * 8)); }
        record[16] = event.status; record[17] = event.first; record[18] = event.second; record[19] = event.reverse_value; record[20] = event.complete_sysex;
        for (unsigned i = 0; i < 4; ++i) record[24 + i] = uint8_t(event.previous >> (i * 8));
        for (unsigned i = 0; i < 4; ++i) record[28 + i] = uint8_t(event.next >> (i * 8));
        return position < UINT32_MAX / 32 && index.write(position * 32, record, 32);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_read_event)
#endif
    static bool read_event(MidiFile &index, uint32_t position, Event &event) {
        event = {};
        uint8_t record[32];
        if (position >= index.size() / 32 || !index.read(position * 32, record, 32)) return false;
        for (unsigned i = 0; i < 8; ++i) event.time |= uint64_t(record[i]) << (i * 8);
        for (unsigned i = 0; i < 4; ++i) { event.offset |= uint32_t(record[8 + i]) << (i * 8); event.length |= uint32_t(record[12 + i]) << (i * 8); }
        for (unsigned i = 0; i < 4; ++i) { event.previous |= uint32_t(record[24 + i]) << (i * 8); event.next |= uint32_t(record[28 + i]) << (i * 8); }
        event.status = record[16]; event.first = record[17]; event.second = record[18]; event.reverse_value = record[19]; event.complete_sysex = record[20];
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_reverse_event)
#endif
    static bool reverse_event(MidiFile &index, unsigned count, uint64_t duration, unsigned position, Event &event) {
        if (position >= count || !read_event(index,count - position - 1,event)) return false;
        if (event.previous) {
            unsigned first = event.previous - 1, end = event.next - 1;
            if (end > count || first >= end || count - position - 1 < first || count - position - 1 >= end) return false;
            unsigned part = position - (count - end);
            if (!read_event(index,first + part,event)) return false;
            event.previous = count - end + 1; event.next = count - first + 1;
        }
        if (event.time > duration) return false;
        event.time = duration - event.time;
        Packet packet{uint8_t(event.status >> 4),{event.status,event.first,event.second}};
        if (!reverse_note(packet,event.reverse_value)) return false;
        event.status = packet.bytes[0]; event.second = packet.bytes[2];
        if (packet.cin == 11 && event.first >= 64 && event.first <= 69) std::swap(event.second,event.reverse_value);
        bool release = packet.cin == 8 || (packet.cin == 11 && event.first >= 64 && event.first <= 69 && event.second < 64);
        event.offset = release && event.time == duration;
        return true;
    }
private:
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_prepared_event)
#endif
    bool prepared_event(unsigned position, Event &event) {
        return settings_.reverse ? reverse_event(*index_,events_,time_,position,event) : read_event(*index_,position,event);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_stream)
#endif
    void stream_event() {
        if (seeking_group_) {
            Event first;
            if (!read_event(*index_, stream_.previous - 1, first)) { fail(); return; }
            stream_ = first;
            seeking_group_ = stream_.previous != 0;
            if (!seeking_group_) parser_.receive(0xf0, packet_);
            return;
        }
        for (unsigned work = 0; work < 32 && stream_offset_ < stream_.length; ++work) {
            uint8_t b;
            if (!source_->read(stream_.offset + stream_offset_++, &b, 1)) { fail(); return; }
            if (parser_.receive(b, packet_)) { pending_outputs_ = settings_.outputs; packet_pending_ = true; return; }
        }
        if (stream_offset_ == stream_.length) {
            if (settings_.reverse && stream_.next) {
                Event next;
                if (!read_event(*index_, stream_.next - 1, next)) { fail(); return; }
                stream_ = next; stream_offset_ = 0;
            } else event_pending_ = false;
        }
    }
};

class MidiFileRecorder {
public:
    explicit MidiFileRecorder(MidiFile &file) : file_(&file) {}
    bool bind(MidiFile &file) { if (active_) return false; file_ = &file; return true; }
    bool active() const { return active_; }
    bool failed() const { return failed_; }
    bool finishing() const { return active_ && finishing_; }
    bool complete() const { return complete_; }
    void cancel() { active_ = finishing_ = complete_ = false; queue_.clear(); }
    bool release_storage() {
        if (active_ || finishing_) return false;
        queue_.release(); queue_limit_ = 0; return true;
    }
    bool reserve_prepared(unsigned capacity) { return !active_ && queue_.reserve(capacity); }
    bool start(RecordMidiFilePatch filters, uint32_t now) {
        return begin(filters, now, 1000, 1000000, nullptr);
    }
    bool start_prepared(RecordMidiFilePatch filters, MidiFile &index, unsigned ppqn, unsigned tempo, unsigned capacity) {
        if (!capacity || !ppqn || ppqn > 32767 || !tempo || tempo > 0xffffff) return false;
        return begin(filters, 0, ppqn, tempo, &index, capacity);
    }
    unsigned prepared_count() const { return prepared_count_; }
    bool queued() const { return !queue_.empty(); }
    void receive_tick(unsigned input, const Packet &packet, uint64_t tick) {
        if (!index_ || !active_ || finishing_ || packet.bytes[0] < 0x80 || packet.bytes[0] >= 0xf0) return;
        elapsed_ = tick;
        enqueue(input, packet, tick);
    }
    bool request_finish_tick(uint64_t tick) {
        if (!index_ || !active_) return false;
        if (!finishing_) { elapsed_ = tick; finishing_ = true; }
        return true;
    }
private:
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_record_begin)
#endif
    bool begin(RecordMidiFilePatch filters, uint32_t now, unsigned ppqn, unsigned tempo, MidiFile *index, unsigned capacity = 0) {
        if (active_) return false;
        if (!queue_.reserve(capacity)) return false;
        queue_.clear();
        queue_limit_ = capacity;
        index_ = index; ppqn_ = ppqn; prepared_count_ = 0;
        filters_ = filters; previous_ = now; elapsed_ = last_ = 0; length_ = 29;
        failed_ = sysex_ = finishing_ = complete_ = false; finish_stage_ = 0;
        // Type 0, 1000 PPQN at 60 BPM: one tick per millisecond.
        const uint8_t header[] = {'M','T','h','d',0,0,0,6,0,0,0,1,uint8_t(ppqn >> 8),uint8_t(ppqn),'M','T','r','k',0,0,0,0,0,0xff,0x51,3,uint8_t(tempo >> 16),uint8_t(tempo >> 8),uint8_t(tempo)};
        active_ = file_->write(0, header, sizeof(header));
        failed_ = !active_; return active_;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_record_enqueue)
#endif
    void enqueue(unsigned input, const Packet &packet, uint64_t time) {
        if (!active_ || finishing_ || !(filters_.inputs & (1u << input))) return;
        if (packet.bytes[0] >= 0x80 && packet.bytes[0] < 0xf0 && !(filters_.channels & (1u << (packet.bytes[0] & 15)))) return;
        if ((queue_limit_ && queue_.size() == queue_limit_) || !queue_.append({time, packet, input})) { failed_ = true; active_ = false; return; }
    }
    uint64_t record_time() const { return index_ ? elapsed_ : elapsed_ / 1000; }
public:
    void receive(unsigned input, const Packet &packet, uint32_t now) {
        if (index_) return;
        advance(now);
        enqueue(input, packet, elapsed_ / 1000);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_record_poll)
#endif
    void poll(uint32_t now) {
        advance(now);
        if (!active_) return;
        if (finishing_ && queue_.empty()) { finish_one(); return; }
        for (unsigned work = 0; work < 16 && !queue_.empty(); ++work) {
            auto chosen = queue_.begin();
            if (sysex_) {
                chosen = std::find_if(queue_.begin(), queue_.end(), [&](const auto &v) { return v.input == sysex_input_ || v.packet.bytes[0] >= 0xf8; });
                if (chosen == queue_.end()) {
                    if (finishing_ && !close_sysex(record_time())) return;
                    if (sysex_) return;
                    chosen = queue_.begin();
                }
            }
            Pending event = *chosen; queue_.erase(chosen);
            event.time = std::max(event.time, last_);
            bool sysex_packet = event.packet.cin >= 4 && event.packet.cin <= 7 && event.packet.bytes[0] != 0xf6;
            if (sysex_ && event.packet.bytes[0] >= 0x80 && event.packet.bytes[0] < 0xf8
                && (!sysex_packet || event.packet.bytes[0] == 0xf0) && !close_sysex(event.time)) return;
            uint8_t data[16]; unsigned size = variable(data, event.time - last_);
            if (!size) { failed_ = true; active_ = false; return; }
            if (sysex_packet) {
                unsigned begin = event.packet.bytes[0] == 0xf0 ? 1 : 0;
                if (!begin && !sysex_) continue;
                if (begin) { sysex_ = true; sysex_input_ = event.input; }
                if (event.packet.bytes[event.packet.size() - 1] == 0xf7) sysex_ = false;
                data[size++] = begin ? 0xf0 : 0xf7;
                data[size++] = uint8_t(event.packet.size() - begin);
                for (unsigned i = begin; i < event.packet.size(); ++i) data[size++] = event.packet.bytes[i];
            } else {
                if (event.packet.bytes[0] >= 0xf0) { data[size++] = 0xf7; data[size++] = uint8_t(event.packet.size()); }
                for (unsigned i = 0; i < event.packet.size(); ++i) data[size++] = event.packet.bytes[i];
            }
            if (size > UINT32_MAX - length_ || !file_->write(length_, data, size)) { failed_ = true; active_ = false; return; }
            if (index_) {
                MidiFilePlayer::Event indexed;
                indexed.time = (event.time / ppqn_) * (uint64_t(1) << 32) + (event.time % ppqn_) * (uint64_t(1) << 32) / ppqn_;
                indexed.status = event.packet.bytes[0]; indexed.first = event.packet.bytes[1]; indexed.second = event.packet.bytes[2];
                if (!MidiFilePlayer::write_event(*index_, prepared_count_, indexed)) { failed_ = true; active_ = false; return; }
                ++prepared_count_;
            }
            length_ += size; last_ = event.time;
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_record_finish)
#endif
    bool finish(uint32_t now) {
        if (!request_finish(now)) return false;
        while (active_) poll(now);
        return complete_;
    }
    bool request_finish(uint32_t now) {
        if (!active_) return false;
        if (!finishing_) { advance(now); finishing_ = true; }
        return true;
    }
private:
    struct Pending { uint64_t time; Packet packet; unsigned input; };
#if UINTPTR_MAX == UINT32_MAX
    static_assert(CheckedList<Pending>::node_size() == 56);
#endif
    MidiFile *file_, *index_ = nullptr;
    unsigned ppqn_ = 1000, prepared_count_ = 0;
    RecordMidiFilePatch filters_;
    CheckedList<Pending> queue_;
    unsigned queue_limit_ = 0;
    uint32_t previous_ = 0, length_ = 0;
    uint64_t elapsed_ = 0, last_ = 0;
    bool active_ = false, failed_ = false, sysex_ = false, finishing_ = false, complete_ = false;
    unsigned finish_stage_ = 0;
    unsigned sysex_input_ = 0;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_record_finalize)
#endif
    void finish_one() {
        bool ok = true;
        if (finish_stage_ == 0) { if (sysex_) ok = close_sysex(record_time()); }
        else if (finish_stage_ == 1) {
            uint8_t end[7]; unsigned n = variable(end, record_time() - last_);
            if (!n || n + 3 > UINT32_MAX - length_) ok = false;
            else {
                end[n++] = 0xff; end[n++] = 0x2f; end[n++] = 0;
                ok = file_->write(length_, end, n);
                if (ok) length_ += n;
            }
        } else if (finish_stage_ == 2) {
            uint32_t size = length_ - 22;
            uint8_t length[] = {uint8_t(size >> 24), uint8_t(size >> 16), uint8_t(size >> 8), uint8_t(size)};
            ok = file_->write(18, length, 4);
        } else {
            if (index_ && finish_stage_ == 3) ok = index_->sync();
            else { ok = file_->sync(); active_ = false; complete_ = ok; }
        }
        ++finish_stage_;
        if (!ok) { failed_ = true; active_ = false; complete_ = false; }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_record_close_sysex)
#endif
    bool close_sysex(uint64_t time) {
        uint8_t close[7]; unsigned n = variable(close, time - last_);
        if (!n) { active_ = false; failed_ = true; return false; }
        close[n++] = 0xf7; close[n++] = 1; close[n++] = 0xf7;
        if (n > UINT32_MAX - length_ || !file_->write(length_, close, n)) { active_ = false; failed_ = true; return false; }
        length_ += n; last_ = time; sysex_ = false; return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_record_advance)
#endif
    void advance(uint32_t now) { if (active_ && !finishing_ && !index_) elapsed_ += uint32_t(now - previous_); previous_ = now; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_record_variable)
#endif
    static unsigned variable(uint8_t *data, uint64_t value) {
        if (value > 0x0fffffff) return 0;
        uint8_t reversed[4]; unsigned n = 0;
        do { reversed[n++] = value & 127; value >>= 7; } while (value);
        for (unsigned i = 0; i < n; ++i) data[i] = reversed[n - i - 1] | (i + 1 < n ? 128 : 0);
        return n;
    }
};
} // namespace cntrl::midi
