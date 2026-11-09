#pragma once
#include "midi_bridge.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

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
    double speed = 1;
    bool reverse = false;
    int8_t transpose = 0;
};
struct RecordMidiFilePatch { uint16_t channels = 0xffff, inputs = 0xffff; };

// SMF tracks are merged into fixed-size records on disk. RAM holds track heads
// and currently held notes, independently of the length of the sequence.
class MidiFilePlayer {
public:
    enum class Status { idle, indexing, playing, complete, failed };
    MidiFilePlayer(MidiFile &source, MidiFile &index) : source_(source), index_(index) {}
    Status status() const { return status_; }
    bool active() const { return status_ == Status::indexing || status_ == Status::playing; }
    bool cleaning() const { return !active() && (!sounding_.empty() || !pedals_.empty() || terminate_outputs_); }
    unsigned owner() const { return owner_; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_delivery)
#endif
    Bridge::Delivery delivery(unsigned owner) const {
        if (owner >= Bridge::owners) return Bridge::Delivery::cancelled;
        if (owner != owner_) return deliveries_[owner];
        if (active() || cleaning()) return Bridge::Delivery::pending;
        return status_ == Status::complete ? Bridge::Delivery::complete : Bridge::Delivery::cancelled;
    }
    unsigned clock_outputs() const { return active() && has_clock_ ? settings_.outputs : 0; }
    unsigned sysex_outputs() const {
        return terminate_outputs_ | (active() && (parser_.sysex() || event_pending_ || (packet_pending_ && packet_.cin >= 4 && packet_.cin <= 7)) ? settings_.outputs : 0);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_start)
#endif
    bool start(const PlayMidiFilePatch &settings, unsigned owner = 0) {
        if (owner >= Bridge::owners || active() || cleaning()) return false;
        deliveries_[owner_] = delivery(owner_); owner_ = owner;
        cancel(); settings_ = settings; has_clock_ = false;
        uint8_t header[14];
        if (!source_.read(0, header, sizeof(header)) || std::memcmp(header, "MThd", 4)
            || big(header + 4, 4) < 6 || big(header + 8, 2) > 1 || !big(header + 10, 2)
            || (big(header + 8, 2) == 0 && big(header + 10, 2) != 1)
            || !settings.outputs || !std::isfinite(settings.speed) || settings.speed <= 0) return fail();
        division_ = big(header + 12, 2);
        if (!division_) return fail();
        if (division_ & 0x8000) {
            int fps = -int(int8_t(division_ >> 8));
            if (!(division_ & 255) || (fps != 24 && fps != 25 && fps != 29 && fps != 30)) return fail();
            numerator_ = fps == 29 ? 1001000000ull : 1000000ull;
            denominator_ = (fps == 29 ? 30000u : unsigned(fps)) * (division_ & 255);
        } else { numerator_ = 500000; denominator_ = division_; }
        if (big(header + 4, 4) > source_.size() - 8) return fail();
        uint32_t offset = 8 + big(header + 4, 4);
        for (unsigned i = 0; i < big(header + 10, 2); ++i) {
            uint8_t chunk[8];
            if (!source_.read(offset, chunk, 8) || std::memcmp(chunk, "MTrk", 4)) return fail();
            uint32_t length = big(chunk + 4, 4);
            if (offset > source_.size() - 8 || length > source_.size() - offset - 8) return fail();
            Track track; track.cursor = offset + 8; track.end = track.cursor + length;
            tracks_.push_back(track); offset = track.end;
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
        if (!active()) {
            if (terminate_outputs_) {
                for (unsigned route = 0; route < max_routes; ++route)
                    if ((terminate_outputs_ & (1u << route)) && (!(available & (1u << route)) || sink.submit(route, {5, {0xf7, 0, 0}}))) terminate_outputs_ &= ~(1u << route);
                return;
            }
            release_one(available, sink);
            return;
        }
        if ((available & settings_.outputs) != settings_.outputs) { fail(); return; }
        if (status_ == Status::indexing) {
            for (unsigned work = 0; work < 32 && status_ == Status::indexing; ++work) index_one();
            return;
        }
        if (!clock_known_) { previous_ = now; clock_known_ = true; }
        elapsed_ += uint32_t(now - previous_); previous_ = now;
        if (elapsed_ < uint64_t(settings_.pre_delay) * 1000000) return;
        double progress = double(elapsed_ - uint64_t(settings_.pre_delay) * 1000000) * settings_.speed;
        // One packet per poll, retried independently on each destination.
        if (packet_pending_) {
            for (unsigned route = 0; route < max_routes; ++route)
                if ((pending_outputs_ & (1u << route)) && sink.submit(route, packet_)) {
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
            if (!load_event(settings_.reverse ? events_ - position_ - 1 : position_, event)) { fail(); return; }
            uint64_t due = settings_.reverse ? time_ - event.time : event.time;
            if (progress < double(due)) return;
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
                    bool off = kind == 8 || !event.second;
                    if (off && !event.reverse_value) return; // Unmatched release has no onset to reverse.
                    packet_.cin = off ? 9 : 8;
                    packet_.bytes[0] = uint8_t((off ? 0x90 : 0x80) | (event.status & 15));
                    packet_.bytes[2] = off ? event.reverse_value : 0;
                }
            }
            if (settings_.reverse && kind == 11 && event.first >= 64 && event.first <= 69)
                packet_.bytes[2] = event.reverse_value;
            pending_outputs_ = settings_.outputs; packet_pending_ = true;
            return;
        }
        if (progress < double(time_)) return;
        if (!release_one(available, sink)) return;
        if (!post_waiting_) { post_start_ = elapsed_; post_waiting_ = true; }
        if (elapsed_ - post_start_ < uint64_t(settings_.post_delay) * 1000000) return;
        if (!settings_.loops || ++loop_ < settings_.loops) {
            position_ = 0; elapsed_ = 0; parser_.reset(); post_waiting_ = false;
        } else status_ = Status::complete;
    }
    void cancel() { terminate_outputs_ |= sysex_outputs(); tracks_.clear(); notes_.clear(); std::memset(controls_, 0, sizeof(controls_)); parser_.reset(); status_ = Status::idle; }
private:
    struct Event {
        uint64_t time = 0;
        uint32_t offset = 0, length = 0, previous = 0, next = 0;
        // Reverse playback uses the matched note velocity or previous CC value.
        uint8_t status = 0, first = 0, second = 0, reverse_value = 0;
        bool complete_sysex = false, meta = false;
    };
    struct Track { uint32_t cursor = 0, end = 0; uint64_t tick = 0; uint8_t running = 0; Event head; bool ready = false, ended = false; };
    struct Held { uint8_t channel, note, velocity; };
    struct Sounding { unsigned output; uint8_t channel, note; };
    std::vector<Sounding> sounding_;
    struct Pedal { unsigned output; uint8_t channel, controller; };
    std::vector<Pedal> pedals_;
    MidiFile &source_, &index_;
    PlayMidiFilePatch settings_;
    std::vector<Track> tracks_;
    std::vector<Held> notes_;
    uint8_t controls_[16][6] = {};
    Status status_ = Status::idle;
    unsigned owner_ = 0;
    std::array<Bridge::Delivery, Bridge::owners> deliveries_{};
    unsigned division_ = 0, denominator_ = 1;
    uint64_t numerator_ = 500000, remainder_ = 0, tick_ = 0, time_ = 0, elapsed_ = 0;
    uint32_t events_ = 0, position_ = 0, loop_ = 0, previous_ = 0, stream_offset_ = 0;
    unsigned pending_outputs_ = 0, terminate_outputs_ = 0;
    uint64_t post_start_ = 0;
    bool post_waiting_ = false;
    uint32_t group_last_ = 0;
    bool seeking_group_ = false, has_clock_ = false;
    bool clock_known_ = false, packet_pending_ = false, event_pending_ = false;
    Packet packet_;
    Parser parser_;
    Event stream_;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_remember)
#endif
    void remember_packet(unsigned route) {
        unsigned kind = packet_.bytes[0] >> 4;
        uint8_t channel = packet_.bytes[0] & 15;
        if (kind == 9 && packet_.bytes[2]) sounding_.push_back({route, channel, packet_.bytes[1]});
        if (kind == 8 || (kind == 9 && !packet_.bytes[2])) {
            auto n = std::find_if(sounding_.begin(), sounding_.end(), [&](const auto &v) { return v.output == route && v.channel == channel && v.note == packet_.bytes[1]; });
            if (n != sounding_.end()) sounding_.erase(n);
        }
        if (kind == 11 && packet_.bytes[1] >= 64 && packet_.bytes[1] <= 69) {
            auto p = std::find_if(pedals_.begin(), pedals_.end(), [&](const auto &v) { return v.output == route && v.channel == channel && v.controller == packet_.bytes[1]; });
            if (packet_.bytes[2] >= 64) {
                if (p == pedals_.end()) pedals_.push_back({route, channel, packet_.bytes[1]});
            } else if (p != pedals_.end()) pedals_.erase(p);
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_release)
#endif
    bool release_one(unsigned available, Sink &sink) {
        // Ownership for cleanup is separate from the sink's complete MIDI state.
        if (!pedals_.empty()) {
            auto p = pedals_.back();
            if (!(available & (1u << p.output)) || sink.submit(p.output, {11, {uint8_t(0xb0 | p.channel), p.controller, 0}})) pedals_.pop_back();
            return false;
        }
        if (!sounding_.empty()) {
            auto n = sounding_.back();
            if (!(available & (1u << n.output)) || sink.submit(n.output, {8, {uint8_t(0x80 | n.channel), n.note, 0}})) sounding_.pop_back();
            return false;
        }
        return true;
    }
    static uint32_t big(const uint8_t *p, unsigned n) { uint32_t v = 0; while (n--) v = (v << 8) | *p++; return v; }
    bool fail() { terminate_outputs_ |= sysex_outputs(); parser_.reset(); status_ = Status::failed; return false; }
    bool byte(Track &track, uint8_t &v) { return track.cursor < track.end && source_.read(track.cursor++, &v, 1); }
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
                if (!source_.read(event.offset + event.length - 1, &last, 1)) return false;
                event.complete_sysex = last == 0xf7;
            }
            if (status == 0xf7 && event.length && event.length <= 3) {
                uint8_t raw[3] = {};
                if (!source_.read(event.offset, raw, event.length)) return false;
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
            if (!index_.sync()) { fail(); return; }
            notes_.clear(); tracks_.clear(); status_ = Status::playing; return;
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
                if (event.length != 3 || !source_.read(event.offset, tempo, 3) || !big(tempo, 3)) { fail(); return; }
                if (!(division_ & 0x8000)) numerator_ = big(tempo, 3);
            }
            return;
        }
        event.time = time_;
        if (event.status == 0xf8) has_clock_ = true;
        unsigned kind = event.status >> 4;
        if (kind == 9 && event.second) notes_.push_back({uint8_t(event.status & 15), event.first, event.second});
        if (kind == 8 || (kind == 9 && !event.second)) {
            auto note = std::find_if(notes_.rbegin(), notes_.rend(), [&](const auto &n) { return n.channel == (event.status & 15) && n.note == event.first; });
            if (note != notes_.rend()) { event.reverse_value = note->velocity; notes_.erase(std::next(note).base()); }
        }
        if (kind == 11 && event.first >= 64 && event.first <= 69) {
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
            if (!index_.write((group_last_ - 1) * 32 + 28, link, 4)) { fail(); return; }
            group_last_ = event.complete_sysex ? 0 : next;
        }
        append_event(event);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_append)
#endif
    void append_event(const Event &event) {
        uint8_t record[32] = {};
        for (unsigned i = 0; i < 8; ++i) record[i] = uint8_t(event.time >> (i * 8));
        for (unsigned i = 0; i < 4; ++i) { record[8 + i] = uint8_t(event.offset >> (i * 8)); record[12 + i] = uint8_t(event.length >> (i * 8)); }
        record[16] = event.status; record[17] = event.first; record[18] = event.second; record[19] = event.reverse_value; record[20] = event.complete_sysex;
        for (unsigned i = 0; i < 4; ++i) record[24 + i] = uint8_t(event.previous >> (i * 8));
        if (events_ == UINT32_MAX / 32 || !index_.write(events_ * 32, record, 32)) { fail(); return; }
        ++events_;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_load_event)
#endif
    bool load_event(uint32_t position, Event &event) {
        uint8_t record[32];
        if (!index_.read(position * 32, record, 32)) return false;
        for (unsigned i = 0; i < 8; ++i) event.time |= uint64_t(record[i]) << (i * 8);
        for (unsigned i = 0; i < 4; ++i) { event.offset |= uint32_t(record[8 + i]) << (i * 8); event.length |= uint32_t(record[12 + i]) << (i * 8); }
        for (unsigned i = 0; i < 4; ++i) { event.previous |= uint32_t(record[24 + i]) << (i * 8); event.next |= uint32_t(record[28 + i]) << (i * 8); }
        event.status = record[16]; event.first = record[17]; event.second = record[18]; event.reverse_value = record[19]; event.complete_sysex = record[20];
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_stream)
#endif
    void stream_event() {
        if (seeking_group_) {
            Event first;
            if (!load_event(stream_.previous - 1, first)) { fail(); return; }
            stream_ = first;
            seeking_group_ = stream_.previous != 0;
            if (!seeking_group_) parser_.receive(0xf0, packet_);
            return;
        }
        for (unsigned work = 0; work < 32 && stream_offset_ < stream_.length; ++work) {
            uint8_t b;
            if (!source_.read(stream_.offset + stream_offset_++, &b, 1)) { fail(); return; }
            if (parser_.receive(b, packet_)) { pending_outputs_ = settings_.outputs; packet_pending_ = true; return; }
        }
        if (stream_offset_ == stream_.length) {
            if (settings_.reverse && stream_.next) {
                Event next;
                if (!load_event(stream_.next - 1, next)) { fail(); return; }
                stream_ = next; stream_offset_ = 0;
            } else event_pending_ = false;
        }
    }
};

class MidiFileRecorder {
public:
    explicit MidiFileRecorder(MidiFile &file) : file_(file) {}
    bool active() const { return active_; }
    bool failed() const { return failed_; }
    bool start(RecordMidiFilePatch filters, uint32_t now) {
        queue_.clear();
        filters_ = filters; previous_ = now; elapsed_ = last_ = 0; length_ = 29; failed_ = sysex_ = finishing_ = false;
        // Type 0, 1000 PPQN at 60 BPM: one tick per millisecond.
        const uint8_t header[] = {'M','T','h','d',0,0,0,6,0,0,0,1,3,0xe8,'M','T','r','k',0,0,0,0,0,0xff,0x51,3,0x0f,0x42,0x40};
        active_ = file_.write(0, header, sizeof(header));
        failed_ = !active_; return active_;
    }
    void receive(unsigned input, const Packet &packet, uint32_t now) {
        advance(now);
        if (!active_ || !(filters_.inputs & (1u << input))) return;
        if (packet.bytes[0] >= 0x80 && packet.bytes[0] < 0xf0 && !(filters_.channels & (1u << (packet.bytes[0] & 15)))) return;
        if (queue_.size() == 256) { failed_ = true; active_ = false; return; }
        queue_.push_back({elapsed_ / 1000, packet, input});
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_record_poll)
#endif
    void poll(uint32_t now) {
        advance(now);
        if (!active_) return;
        for (unsigned work = 0; work < 16 && !queue_.empty(); ++work) {
            auto chosen = queue_.begin();
            if (sysex_) {
                chosen = std::find_if(queue_.begin(), queue_.end(), [&](const auto &v) { return v.input == sysex_input_ || v.packet.bytes[0] >= 0xf8; });
                if (chosen == queue_.end()) {
                    if (finishing_ && !close_sysex(elapsed_ / 1000)) return;
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
            if (size > UINT32_MAX - length_ || !file_.write(length_, data, size)) { failed_ = true; active_ = false; return; }
            length_ += size; last_ = event.time;
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_record_finish)
#endif
    bool finish(uint32_t now) {
        finishing_ = true;
        while (active_ && !queue_.empty()) poll(now);
        advance(now);
        if (!active_) return false;
        if (sysex_ && !close_sysex(elapsed_ / 1000)) return false;
        uint8_t end[7]; unsigned n = variable(end, elapsed_ / 1000 - last_);
        if (!n) { active_ = false; failed_ = true; return false; }
        end[n++] = 0xff; end[n++] = 0x2f; end[n++] = 0;
        if (n > UINT32_MAX - length_) { active_ = false; failed_ = true; return false; }
        uint32_t size = length_ + n - 22;
        uint8_t length[] = {uint8_t(size >> 24), uint8_t(size >> 16), uint8_t(size >> 8), uint8_t(size)};
        active_ = false;
        failed_ = !file_.write(length_, end, n) || !file_.write(18, length, 4) || !file_.sync();
        return !failed_;
    }
private:
    struct Pending { uint64_t time; Packet packet; unsigned input; };
    MidiFile &file_;
    RecordMidiFilePatch filters_;
    std::vector<Pending> queue_;
    uint32_t previous_ = 0, length_ = 0;
    uint64_t elapsed_ = 0, last_ = 0;
    bool active_ = false, failed_ = false, sysex_ = false, finishing_ = false;
    unsigned sysex_input_ = 0;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_record_close_sysex)
#endif
    bool close_sysex(uint64_t time) {
        uint8_t close[7]; unsigned n = variable(close, time - last_);
        if (!n) { active_ = false; failed_ = true; return false; }
        close[n++] = 0xf7; close[n++] = 1; close[n++] = 0xf7;
        if (n > UINT32_MAX - length_ || !file_.write(length_, close, n)) { active_ = false; failed_ = true; return false; }
        length_ += n; last_ = time; sysex_ = false; return true;
    }
    void advance(uint32_t now) { if (active_) elapsed_ += uint32_t(now - previous_); previous_ = now; }
    static unsigned variable(uint8_t *data, uint64_t value) {
        if (value > 0x0fffffff) return 0;
        uint8_t reversed[4]; unsigned n = 0;
        do { reversed[n++] = value & 127; value >>= 7; } while (value);
        for (unsigned i = 0; i < n; ++i) data[i] = reversed[n - i - 1] | (i + 1 < n ? 128 : 0);
        return n;
    }
};
} // namespace cntrl::midi
