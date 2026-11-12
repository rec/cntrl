#pragma once
#include "arpeggiator_selection.h"
#include "document.h"
#include "clock_generator.h"
#include "midi_loop_capture.h"
#include "checked_list.h"
#include "numeric.h"
#include <array>

namespace cntrl::midi {
class MidiArpeggiator : public NoteRouting {
public:
    enum class State { stopped, waiting, playing, armed, recording, finishing };
    enum class Error { none, capacity, clock, output };
    State state() const { return state_; }
    Error error() const { return error_; }
    bool mono() const { return mono_; }
    unsigned size() const { return unsigned(mono_ ? bank_.size() : held_.size()); }
    unsigned selection() const { return selection_.kind; }
    bool configured() const { return configured_; }
    bool input_selected(unsigned input) const { return configured_ && input < 16 && (inputs_ & (1u << input)); }
    void input_memory_ready(bool ready) { input_memory_ready_ = ready; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(arp_configure)
#endif
    void configure(const uint8_t *p) {
        if (prepared_ && enable_pending_ == (p != nullptr)
            && (!p || std::equal(p,p + pending_.size(),pending_.begin()))) return;
        prepared_ = false; reconfigure_ = true; enable_pending_ = p != nullptr;
        if (p) std::copy(p,p + pending_.size(),pending_.begin());
    }
    bool prepare_configuration(const uint8_t *p, Sink &sink) {
        const auto previous = pending_;
        const bool enabled = enable_pending_;
        enable_pending_ = p != nullptr;
        if (p) std::copy(p,p + pending_.size(),pending_.begin());
        if (!stage_configuration(sink)) {
            pending_ = previous; enable_pending_ = enabled;
            error_ = Error::capacity; return false;
        }
        prepared_ = true;
        return true;
    }
    void finish_configuration() {
        if (!prepared_) return;
        reconfigure_ = true;
        stop();
    }
    void discard_prepared_configuration() {
        prepared_resources_ = Prepared{};
        prepared_ = false;
        reconfigure_ = false;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(arp_toggle)
#endif
    void toggle(BeatPosition clock) {
        if (!configured_) return;
        if (state_ != State::stopped) { stop(); return; }
        if (mono_ && bank_.empty()) return;
        restart_ = clock.restart; next_ = boundary(clock.value,step_); state_ = State::waiting;
        waiting_clock_ = !clock.running;
        order_.reset(selection_); selected_ = absent; error_ = Error::none;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(arp_record)
#endif
    bool record(BeatPosition clock) {
        if (!configured_ || !mono_ || !clock.running) { error_ = Error::clock; return false; }
        if (!input_memory_ready_) { error_ = Error::capacity; return false; }
        if (state_ == State::recording) { finish_ = boundary(clock.value,beat); state_ = State::finishing; return true; }
        if (state_ == State::armed || state_ == State::finishing) return true;
        unsigned outputs = 0;
        for (unsigned mask = outputs_; mask; mask &= mask - 1) ++outputs;
        const unsigned segments = candidate_.capacity(), groups = candidate_groups_.capacity();
        const unsigned retired = retired_.capacity(), snapshots = snapshot_.capacity();
        if (!candidate_.reserve(capacity_ / 2) || !candidate_groups_.reserve(capacity_)
            || !retired_.reserve(retired_.size() + capacity_ / 2)
            || !snapshot_.reserve(snapshot_.size() + capacity_)
            || !capture_.reserve(sink_->capture_contexts(inputs_,channels_))
            || !order_.reserve(capacity_ / 2)
            || !sink_->reserve_playback_state(capacity_ * outputs,outputs_,channels_,1)
            || !sink_->reserve_arpeggiator_ownership(outputs)) {
            candidate_.trim(candidate_.capacity() - segments); candidate_groups_.trim(candidate_groups_.capacity() - groups);
            retired_.trim(retired_.capacity() - retired); snapshot_.trim(snapshot_.capacity() - snapshots);
            error_ = Error::capacity; return false;
        }
        stop(); candidate_.clear(); candidate_groups_.clear(); candidate_events_ = 0;
        begin_ = boundary(clock.value,beat); restart_ = clock.restart;
        state_ = State::armed; error_ = Error::none; return true;
    }
    void stop() { state_ = State::stopped; waiting_clock_ = false; selected_ = absent; candidate_.clear(); candidate_groups_.clear(); finish_ = 0; }
    void suspend() { stop(); held_.clear(); routed_.clear(); }
    void disconnect(unsigned route) {
        capture_.forget_input(route);
        for (auto i = held_.begin(); i != held_.end();) {
            if (i->source / 16 == route) {
                if (i->identity == selected_) for (auto &voice : voices_) voice.end = 0;
                i = held_.erase(i);
            } else ++i;
        }
        for (auto i = routed_.begin(); i != routed_.end();) {
            if (i->output == route || unsigned(i->packet.source) / 16 == route) i = routed_.erase(i); else ++i;
        }
        for (auto &voice : voices_) if (voice.output == route) voice.active = false;
        for (auto i = snapshot_.begin(); i != snapshot_.end();) {
            if (i->source / 16 == route) i = snapshot_.erase(i); else ++i;
        }
        if (mono_ && !candidate_.empty() && !candidate_.back().end && candidate_.back().source / 16 == route) { error_ = Error::output; stop(); }
        retain_selection();
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(arp_input)
#endif
    void receive(unsigned input, unsigned cable, const Packet &packet, BeatPosition clock) {
        if (!configured_ || input >= 16 || !(inputs_ & (1u << input)) || packet.bytes[0] < 0x80 || packet.bytes[0] >= 0xf0
            || !(channels_ & (1u << (packet.bytes[0] & 15)))) return;
        unsigned source = input * 16 + cable, kind = packet.bytes[0] >> 4;
        Packet p = packet; p.source = uint16_t(source);
        if (kind == 11 && (p.bytes[1] == 120 || p.bytes[1] >= 123)) {
            if (mono_ && !candidate_.empty() && candidate_.back().source == source) close_note(clock.value,0,true);
            held_.erase_if([&](const Held &h) { return h.source == source && (h.packet.bytes[0] & 15) == (p.bytes[0] & 15); });
            retain_selection();
            for (auto &voice : voices_) if ((voice.attack.bytes[0] & 15) == (p.bytes[0] & 15)) voice.end = clock.value;
            return;
        }
        bool on = kind == 9 && p.bytes[2], off = kind == 8 || (kind == 9 && !p.bytes[2]);
        if (!mono_) {
            if (on) {
                if (held_.size() == capacity_) { error_ = Error::capacity; return; }
                Held held{source,next_identity_++,p};
                auto position = std::upper_bound(held_.begin(),held_.end(),held,[](const Held &a,const Held &b) {
                    if (a.packet.bytes[1] != b.packet.bytes[1]) return a.packet.bytes[1] < b.packet.bytes[1];
                    if (a.source != b.source) return a.source < b.source;
                    return a.packet.bytes[0] < b.packet.bytes[0];
                });
                if (!held_.insert(position,held)) { error_ = Error::capacity; return; }
            } else if (off) {
                auto i = std::find_if(held_.begin(),held_.end(),[&](const Held &h) { return h.source == source && (h.packet.bytes[0] & 15) == (p.bytes[0] & 15) && h.packet.bytes[1] == p.bytes[1]; });
                if (i != held_.end()) {
                    if (i->identity == selected_) for (auto &voice : voices_) if (voice.active) voice.end = clock.value;
                    held_.erase(i);
                }
            }
            if (on || off) retain_selection();
            return;
        }
        bool capturing = (state_ == State::recording || state_ == State::finishing) && clock.running && clock.restart == restart_ && (!finish_ || clock.value < finish_);
        CaptureContext context{*this,capturing};
        if (!capture_.flush(clock.value,on || off,{&context,emit_capture})) return;
        if (off) {
            auto retired = std::find_if(retired_.begin(),retired_.end(),[&](const Packet &old) { return old.source == source && (old.bytes[0] & 15) == (p.bytes[0] & 15) && old.bytes[1] == p.bytes[1]; });
            if (retired != retired_.end()) { retired_.erase(retired); return; }
        }
        if (capturing && on) {
            close_note(clock.value,0,true);
            if (candidate_events_ + 2 > capacity_) { abort(); return; }
            Segment segment; segment.source = source; segment.on = p; segment.begin = clock.value;
            for (const auto &s : snapshot_) if (s.source == source && (s.packets[0].bytes[0] & 15) == (p.bytes[0] & 15)) {
                if (s.packets[0].bytes[0] >> 4 == 10 && s.packets[0].bytes[1] != p.bytes[1]) continue;
                Group copy = s; copy.time = 0;
                if (candidate_events_ + copy.count + 2 > capacity_) { abort(); return; }
                auto *group = candidate_groups_.append(copy);
                if (!group) { abort(); return; }
                append_expression(segment,group); candidate_events_ += copy.count;
            }
            if (!candidate_.append(std::move(segment))) { abort(); return; }
            candidate_events_ += 2;
        } else if (capturing && off && !candidate_.empty()) {
            auto &s = candidate_.back();
            if (!s.end && s.source == source && (s.on.bytes[0] & 15) == (p.bytes[0] & 15) && s.on.bytes[1] == p.bytes[1]) close_note(clock.value,p.bytes[2]);
        }
        if (kind == 10 || kind == 11 || kind == 13 || kind == 14) {
            if (!capture_.receive(source,p,clock.value,{&context,emit_capture})) abort();
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(arp_route_filter)
#endif
    bool suppress(unsigned output, const Packet &packet) override {
        if (!routable(output,packet) || !muted()) return false;
        bool on = packet.bytes[0] >> 4 == 9 && packet.bytes[2];
        if (on) {
            if (!prepare(output,packet)) return false;
            track(output,packet,false); consume_batch(); return true;
        }
        auto i = routed_note(output,packet);
        if (i == routed_.end()) return true;
        if (i->sounding) return false;
        routed_.erase(i); return true;
    }
    bool prepare(unsigned output, const Packet &packet) override {
        if (!routable(output,packet) || packet.bytes[0] >> 4 != 9 || !packet.bytes[2]) return true;
        if ((current_batch_ && !current_batch_->remaining)
            || (!current_batch_ && reserved_routes_ >= routed_limit_ - routed_.size())
            || routed_.size() == routed_limit_) { error_ = Error::capacity; return false; }
        return routed_.reserve(routed_.size() + 1);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(arp_prepare_batch)
#endif
    bool prepare_batch(unsigned output,const uint8_t *bytes,unsigned size,unsigned source = 65535) override {
        const unsigned onsets = batch_onsets(output,bytes,size,source);
        if (!onsets) return true;
        if (reserved_routes_ > routed_limit_ - routed_.size()
            || onsets > routed_limit_ - routed_.size() - reserved_routes_) { error_ = Error::capacity; return false; }
        return routed_.reserve(routed_.size() + onsets);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(arp_begin_batch)
#endif
    bool begin_batch(unsigned owner,unsigned output,const uint8_t *bytes,unsigned size,unsigned source = 65535) override {
        const unsigned onsets = batch_onsets(output,bytes,size,source);
        if (!onsets) return true;
        for (const auto &batch : batches_) if (batch.owner == owner) return false;
        if (!prepare_batch(output,bytes,size,source) || !batches_.append({owner,onsets})) return false;
        reserved_routes_ += onsets;
        return true;
    }
    void enter_batch(unsigned owner) override {
        current_batch_ = nullptr;
        for (auto &batch : batches_) if (batch.owner == owner) { current_batch_ = &batch; break; }
    }
    void leave_batch() override { current_batch_ = nullptr; }
    void finish_batch(unsigned owner) override {
        for (auto i = batches_.begin(); i != batches_.end(); ++i) if (i->owner == owner) {
            reserved_routes_ -= i->remaining;
            if (current_batch_ == &*i) current_batch_ = nullptr;
            batches_.erase(i); break;
        }
    }
    void accepted(unsigned output, const Packet &packet) override {
        if (!routable(output,packet)) return;
        if (packet.bytes[0] >> 4 == 9 && packet.bytes[2]) { track(output,packet,true); consume_batch(); }
        else { auto i = routed_note(output,packet); if (i != routed_.end()) routed_.erase(i); }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(arp_poll)
#endif
    void poll(BeatPosition clock, unsigned available, unsigned busy, Sink &sink, unsigned quarter_us = 500000) {
        sink.reclaim_memory();
        batches_.trim(1);
        if (reconfigure_ && !prepared_) {
            if (!stage_configuration(sink)) { reconfigure_ = false; error_ = Error::capacity; }
            else { stop(); prepared_ = true; }
        }
        if (state_ == State::waiting && waiting_clock_ && clock.running) {
            restart_ = clock.restart; next_ = boundary(clock.value,step_); waiting_clock_ = false;
        }
        capture_.pairing(unsigned(std::min(uint64_t(UINT32_MAX),beat * 2000 / std::max(1u,quarter_us))));
        CaptureContext context{*this,state_ == State::recording || state_ == State::finishing};
        capture_.flush(clock.value,false,{&context,emit_capture});
        for (auto &voice : voices_) if (voice.active && (state_ == State::stopped || !clock.running || clock.restart != restart_ || clock.value >= voice.end)) {
            if (voice.token) { sink.abandon_loop_group(voice.output,voice.token); voice.token = 0; }
            if (!(available & (1u << voice.output))) voice.active = false;
            else if (!voice.attacked || (!(busy & (1u << voice.output)) && sink.submit_arpeggiator(voice.output,voice.off))) voice.active = false;
        }
        if (reconfigure_ && !sounding() && batches_.empty()) {
            bool restored = true;
            for (auto &r : routed_) if (!r.sounding && (available & (1u << r.output))) {
                NoteRouting *routing = sink.note_routing; sink.note_routing = nullptr;
                if (!(busy & (1u << r.output)) && sink.submit(r.output,r.packet)) r.sounding = true;
                else restored = false;
                sink.note_routing = routing;
            }
            if (restored) apply(sink);
        }
        if (!configured_ || reconfigure_) return;
        if (!sounding() && state_ != State::armed && state_ != State::recording && state_ != State::finishing
            && (state_ == State::stopped || clock.value < next_)) {
            candidate_.trim(1); candidate_groups_.trim(1); bank_.trim(1); bank_groups_.trim(1);
            snapshot_.trim(1); retired_.trim(1);
            const unsigned keys = mono_ ? (bank_groups_.size() + unsigned(!bank_.empty())) * voices_.size() : voices_.size();
            sink.shrink_playback_state(keys,1,4);
        }
        if (state_ != State::stopped && (!clock.running || clock.restart != restart_)) {
            if (!waiting_clock_) stop();
            return;
        }
        for (auto &r : routed_) {
            bool should_sound = !muted();
            if (r.sounding == should_sound || !(available & (1u << r.output)) || (busy & (1u << r.output))) continue;
            Packet packet = r.packet;
            if (!should_sound) { packet.cin = 8; packet.bytes[0] = uint8_t(0x80 | (packet.bytes[0] & 15)); packet.bytes[2] = 0; }
            NoteRouting *routing = sink.note_routing; sink.note_routing = nullptr;
            bool accepted = sink.submit(r.output,packet); sink.note_routing = routing;
            if (accepted) r.sounding = should_sound;
        }
        if (state_ == State::armed && clock.value >= begin_) { state_ = State::recording; }
        if (state_ == State::finishing && clock.value >= finish_ && !sounding()) {
            context.capturing = true;
            capture_.flush(finish_,true,{&context,emit_capture});
            close_note(finish_,0,true); bank_.swap(candidate_); bank_groups_.swap(candidate_groups_);
            candidate_.clear(); candidate_groups_.clear(); finish_ = 0;
            order_.reset(selection_); next_ = boundary(clock.value,step_); state_ = State::waiting;
        }
        expression_playback(clock,available,busy,sink);
        if ((state_ != State::playing && state_ != State::waiting) || !clock.running || clock.value < next_) return;
        uint64_t at = next_; next_ += (1 + (clock.value - next_) / step_) * step_;
        state_ = State::playing;
        if (clock.value - at >= gate_ || !size()) return;
        unsigned handoff = 0;
        for (const auto &r : routed_) if (muted() && r.sounding) handoff |= 1u << r.output;
        unsigned rank = order_.next(size()); Packet attack, release; const Segment *segment = nullptr;
        if (mono_) { segment = &bank_[rank]; attack = segment->on; release = segment->off; }
        else {
            const auto &h = held_[rank]; attack = h.packet; selected_ = h.identity;
            release = {8,{uint8_t(0x80 | (attack.bytes[0] & 15)),attack.bytes[1],0}};
        }
        attack.source = release.source = 65535;
        for (unsigned output = 0; output < 16; ++output) if ((outputs_ & available & ~busy & ~handoff) & (1u << output)) {
            auto *prepared = output_voice(output);
            if (!prepared) { error_ = Error::capacity; continue; }
            auto &voice = *prepared;
            if (voice.active) continue;
            voice = {}; voice.active = true; voice.output = output; voice.begin = at;
            voice.end = at + gate_; voice.off = release; voice.attack = attack; voice.segment = segment;
            voice.group = segment ? segment->expression : nullptr;
        }
        expression_playback(clock,available,busy,sink);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(arp_expression_playback)
#endif
    void expression_playback(BeatPosition clock,unsigned available,unsigned busy,Sink &sink) {
        for (auto &voice : voices_) if (voice.active && ((available & ~busy) & (1u << voice.output)) && clock.value < voice.end) {
            for (unsigned work = 0; work < 16; ++work) {
                bool complete = !voice.group;
                if (!voice.attacked && (complete || voice.group->time > 0)) {
                    if (!sink.submit_arpeggiator(voice.output,voice.attack)) { error_ = Error::output; break; }
                    voice.attacked = true;
                }
                if (complete) break;
                const auto &segment = *voice.segment;
                const auto &group = *voice.group;
                uint64_t duration = std::max(uint64_t(1),segment.end - segment.begin);
                uint64_t due = voice.begin + multiply_divide(group.time,gate_,duration);
                if (clock.value < due || clock.value >= voice.end) break;
                Packet packet = group.packets[voice.packet]; packet.source = 65535;
                bool accepted;
                if (group.count > 1) {
                    uint8_t bytes[18] = {};
                    for (unsigned i = 0; i < group.count; ++i) std::copy(group.packets[i].bytes,group.packets[i].bytes + 3,bytes + i * 3);
                    Parameter parameter;
                    if (!parameter.decode_messages(bytes,group.count * 3)) { voice.group = voice.group->next; voice.packet = 0; continue; }
                    if (!voice.token) voice.token = (uint64_t(1) << 63) | ++next_token_;
                    accepted = sink.submit_loop_group(voice.output,packet,parameter,voice.token,voice.packet == 0,voice.packet + 1 == group.count);
                } else accepted = sink.submit_arpeggiator(voice.output,packet);
                if (!accepted) { error_ = Error::output; break; }
                if (++voice.packet == group.count) { voice.packet = 0; voice.token = 0; voice.group = voice.group->next; }
            }
        }
    }
private:
    struct CaptureContext { MidiArpeggiator &arp; bool capturing; };
    static bool emit_capture(void *context,const Packet *packets,unsigned count,uint64_t time) {
        auto &capture = *static_cast<CaptureContext *>(context);
        return capture.arp.expression(packets[0].source,packets,count,time,capture.capturing);
    }
    static constexpr uint64_t beat = uint64_t(1) << 32;
    static constexpr unsigned absent = ~0u;
    struct Held { unsigned source, identity; Packet packet; };
    struct Group { unsigned source = 0, key = 0, count = 0; uint64_t time = 0; std::array<Packet,6> packets{}; Group *next = nullptr; };
    struct Segment { unsigned source = 0; Packet on{}, off{}; uint64_t begin = 0, end = 0; Group *expression = nullptr, *last_expression = nullptr; };
    struct Voice { bool active = false, attacked = false; unsigned output = 0; uint64_t begin = 0, end = 0, token = 0; Packet off{}, attack{}; const Segment *segment = nullptr; const Group *group = nullptr; unsigned packet = 0; };
    struct Routed { unsigned output; Packet packet; bool sounding; };
    struct Batch { unsigned owner, remaining; };
#if UINTPTR_MAX == UINT32_MAX
    static_assert(CheckedList<Held>::node_size() == 44 && CheckedList<Routed>::node_size() == 40);
    static_assert(CheckedList<Group>::node_size() == 96 && CheckedList<Segment>::node_size() == 72);
    static_assert(CheckedList<Voice>::node_size() == 88);
#endif
    State state_ = State::stopped;
    Error error_ = Error::none;
    bool mono_ = false, configured_ = false, waiting_clock_ = false, input_memory_ready_ = true;
    unsigned inputs_ = 0, outputs_ = 0, channels_ = 0, capacity_ = 0;
    unsigned routed_limit_ = 0;
    unsigned reserved_routes_ = 0;
    CheckedList<Batch> batches_;
    Batch *current_batch_ = nullptr;
    unsigned next_identity_ = 0, selected_ = absent, candidate_events_ = 0;
    uint64_t step_ = beat / 4, gate_ = beat / 5, next_ = 0, begin_ = 0, finish_ = 0;
    uint32_t restart_ = 0;
    uint64_t next_token_ = 0;
    std::array<uint8_t,50> pending_{};
    bool reconfigure_ = false, enable_pending_ = false, prepared_ = false;
    ArpeggiatorSelection selection_;
    ArpeggiatorOrder order_;
    LoopCapture capture_;
    CheckedList<Held> held_;
    CheckedList<Segment> bank_, candidate_;
    CheckedList<Group> snapshot_, bank_groups_, candidate_groups_;
    CheckedList<Packet> retired_;
    CheckedList<Routed> routed_;
    CheckedList<Voice> voices_;
    Sink *sink_ = nullptr;
    struct Prepared {
        CheckedList<Held> held;
        CheckedList<Routed> routed;
        CheckedList<Voice> voices;
        ArpeggiatorOrder order;
    } prepared_resources_;
    Voice *output_voice(unsigned output) {
        for (auto &voice : voices_) if (voice.output == output) return &voice;
        return nullptr;
    }
    static void append_expression(Segment &segment, Group *group) {
        group->next = nullptr;
        if (segment.last_expression) segment.last_expression->next = group; else segment.expression = group;
        segment.last_expression = group;
    }
    static void insert_expression(Segment &segment, Group *group) {
        if (!segment.last_expression || segment.last_expression->time <= group->time) { append_expression(segment,group); return; }
        auto **position = &segment.expression;
        while (*position && (*position)->time <= group->time) position = &(*position)->next;
        group->next = *position; *position = group;
    }
    static uint64_t boundary(uint64_t value,uint64_t step) { return (value / step + 1) * step; }
    bool sounding() const { for (const auto &v : voices_) if (v.active) return true; return false; }
    bool muted() const { return configured_ && !mono_ && (state_ != State::stopped || sounding()); }
    bool routable(unsigned output,const Packet &p) const {
        return configured_ && !mono_ && p.source < 256 && (inputs_ & (1u << (p.source / 16))) && (outputs_ & (1u << output))
            && (channels_ & (1u << (p.bytes[0] & 15))) && (p.bytes[0] >> 4 == 8 || p.bytes[0] >> 4 == 9);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(arp_batch_onsets)
#endif
    unsigned batch_onsets(unsigned output,const uint8_t *bytes,unsigned size,unsigned source) const {
        if (!configured_ || mono_ || source >= 256 || !(inputs_ & (1u << (source / 16)))
            || !(outputs_ & (1u << output))) return 0;
        Parser parser; Packet packet; unsigned onsets = 0;
        for (unsigned i = 0; i < size; ++i) if (parser.receive(bytes[i],packet)
            && packet.bytes[0] >> 4 == 9 && packet.bytes[2] && (channels_ & (1u << (packet.bytes[0] & 15)))) ++onsets;
        return onsets;
    }
    void consume_batch() {
        if (current_batch_ && current_batch_->remaining) { --current_batch_->remaining; --reserved_routes_; }
    }
    CheckedList<Routed>::iterator routed_note(unsigned output,const Packet &p) {
        return std::find_if(routed_.begin(),routed_.end(),[&](const Routed &r) { return r.output == output && r.packet.source == p.source && (r.packet.bytes[0] & 15) == (p.bytes[0] & 15) && r.packet.bytes[1] == p.bytes[1]; });
    }
    void track(unsigned output,const Packet &p,bool sounding) {
        if (routed_.size() >= routed_limit_) { error_ = Error::capacity; return; }
        if (!routed_.append({output,p,sounding})) error_ = Error::capacity;
    }
    void retain_selection() {
        if (selected_ == absent) return;
        for (unsigned i = 0; i < held_.size(); ++i) if (held_[i].identity == selected_) { order_.retain(i); return; }
        order_.reset(selection_); selected_ = absent;
    }
    void abort() { candidate_.clear(); candidate_groups_.clear(); candidate_events_ = 0; finish_ = 0; state_ = State::stopped; error_ = Error::capacity; }
    void close_note(uint64_t at,uint8_t velocity,bool synthetic = false) {
        if (candidate_.empty() || candidate_.back().end) return;
        auto &s = candidate_.back(); s.end = std::max(at,s.begin + 1);
        if (synthetic && retired_.size() < capacity_ && !retired_.append(s.on)) { error_ = Error::capacity; }
        s.off = {8,{uint8_t(0x80 | (s.on.bytes[0] & 15)),s.on.bytes[1],velocity}};
    }
    bool expression(unsigned source,const Packet *packets,unsigned count,uint64_t time,bool capturing) {
        if (!count || count > 6) return true;
        unsigned kind = packets[0].bytes[0] >> 4, address = packets[0].bytes[1];
        if (kind == 11 && (address >= 120 || (address >= 64 && address <= 69))) return true;
        Group group; group.source = source; group.count = count; group.time = time;
        group.key = (unsigned(packets[0].bytes[0]) << 24) | ((kind == 11 || kind == 10) ? address << 16 : 0);
        if (count > 2) group.key |= packets[0].bytes[2] * 128 + packets[1].bytes[2];
        std::copy(packets,packets + count,group.packets.begin());
        auto i = std::find_if(snapshot_.begin(),snapshot_.end(),[&](const Group &s) { return s.source == source && s.key == group.key; });
        if (i != snapshot_.end()) *i = group;
        else if (!snapshot_.append(group)) { error_ = Error::capacity; return false; }
        if (capturing && !candidate_.empty()) {
            auto &s = candidate_.back();
            if (!s.end && time >= s.begin && s.source == source && (s.on.bytes[0] & 15) == (packets[0].bytes[0] & 15)) {
                if (kind == 10 && address != s.on.bytes[1]) return true;
                if (candidate_events_ + count > capacity_) { abort(); return false; }
                group.time = time > s.begin ? time - s.begin : 0;
                auto *copy = candidate_groups_.append(group);
                if (!copy) { abort(); return false; }
                insert_expression(s,copy); candidate_events_ += count;
            }
        }
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(arp_apply)
#endif
    void apply(Sink &sink) {
        sink_ = &sink;
        held_ = std::move(prepared_resources_.held); routed_ = std::move(prepared_resources_.routed);
        voices_ = std::move(prepared_resources_.voices); order_ = std::move(prepared_resources_.order);
        bank_.release(); candidate_.release(); bank_groups_.release(); candidate_groups_.release();
        snapshot_.release(); retired_.release();
        prepared_ = false;
        reconfigure_ = false; configured_ = enable_pending_;
        if (!configured_) { capture_.release(); sink.reserve_arpeggiator_ownership(0); sink.reserve_playback_state(0,0,0,1); return; }
        const auto *p = pending_.data(); mono_ = p[5] != 0;
        if (mono_) { sink.reserve_arpeggiator_ownership(0); sink.reserve_playback_state(0,0,0,1); }
        selection_ = {p[6],p[7],u16(p + 16),u32(p + 34),u32(p + 38),u32(p + 42),u32(p + 46)};
        channels_ = u16(p + 8); inputs_ = u16(p + 10); outputs_ = u16(p + 12); capacity_ = u16(p + 14);
        routed_limit_ = routed_.capacity();
        step_ = uint64_t(u32(p + 18)) | uint64_t(u32(p + 22)) << 32;
        gate_ = uint64_t(u32(p + 26)) | uint64_t(u32(p + 30)) << 32;
        order_.reset(selection_);
        capture_.release();
        capture_.reset(1);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(arp_stage_configuration)
#endif
    bool stage_configuration(Sink &sink) {
        Prepared staged;
        if (!enable_pending_) { prepared_resources_ = std::move(staged); return true; }
        const auto *p = pending_.data();
        const unsigned count = u16(p + 14), outputs = u16(p + 12);
        unsigned selected = 0;
        for (unsigned mask = outputs; mask; mask &= mask - 1) ++selected;
        if (!p[5] && (!staged.held.reserve(count) || !staged.routed.reserve(count * selected)
            || !staged.order.reserve(count))) return false;
        if (!staged.voices.reserve(selected)) return false;
        for (unsigned output = 0; output < 16; ++output) if (outputs & (1u << output)) {
            Voice voice; voice.output = output;
            if (!staged.voices.append(voice)) return false;
        }
        if (!p[5] && (!sink.reserve_playback_state(selected,outputs,u16(p + 8),1)
            || !sink.reserve_arpeggiator_ownership(selected))) return false;
        prepared_resources_ = std::move(staged); return true;
    }
};
} // namespace cntrl::midi
