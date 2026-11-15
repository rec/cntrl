#pragma once
#include "clock_generator.h"
#include "midi_loop_file.h"
#include "midi_loop_capture.h"
#include "midi_compact_loop.h"
#include <array>
#include <cstdio>
#include <charconv>

namespace cntrl::midi {
struct LooperSettings {
    uint16_t inputs = 0xffff, channels = 0xffff, outputs = 1;
    unsigned events = 512; // User-configurable arm admission limit, not idle storage.
};

class MidiLooper {
public:
    enum class State { empty, armed, recording, closing, playing, overdub_armed, overdubbing, preparing, stopped, launching, undo_pending, loading };
    enum class Error { none, busy, clock, capacity, memory, late, output, file, input };
    static constexpr uint64_t beat = uint64_t(1) << 32;
    // The live looper contract supports sixteen four-beat bars per take.
    static constexpr unsigned ppqn = 3840, maximum_beats = 64;
    MidiLooper(MidiFilePlayer &player, MidiFileRecorder &recorder) : player_(player), recorder_(recorder) {}
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_configure)
#endif
    bool configure(const LooperSettings &settings, Sink &sink) {
        if (cleanup_pending_ && !sink.loop_cleaning() && !sink.loop_control_reset_pending()) { playback_.release(sink); cleanup_pending_ = false; }
        if (owned() || saving_ || !settings.events || settings.events > 65535 || !settings.inputs || !settings.channels || !settings.outputs) return false;
        configured_ = false;
        release_buffers(sink);
        settings_ = settings;
        sink_ = &sink;
        peak_events_ = peak_held_ = late_ = failed_outputs_ = maximum_late_us_ = capture_.discarded = 0;
        configured_ = true; shutdown_ = false; error_ = Error::none; return true;
    }
    State state() const { return state_; }
    Error error() const { return error_; }
    bool configured() const { return configured_; }
    void disable() { if (clear()) shutdown_ = true; }
    uint64_t length() const { return active_ < 3 ? generations_[active_].length : length_; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_position)
#endif
    uint64_t position(BeatPosition clock) const {
        if (clock.value < origin_) return 0;
        if (state_ == State::recording) return clock.value - capture_origin_;
        if (!playing() || !length()) return 0;
        uint64_t phase = (clock.value - origin_) % length();
        return backward_ ? length() - phase : phase;
    }
    unsigned events() const { return active_ < 3 ? generations_[active_].count : candidate_ < 3 ? generations_[candidate_].count : 0; }
    unsigned capacity() const { return settings_.events; }
    unsigned payload_bytes() const {
        unsigned bytes = 0; for (const auto &g : generations_) bytes += g.data.payload_bytes(); return bytes;
    }
    unsigned storage_bytes() const {
        unsigned bytes = scratch_.storage_bytes();
        for (const auto &g : generations_) bytes += g.data.storage_bytes();
        return bytes;
    }
    unsigned discarded_groups() const { return capture_.discarded; }
    unsigned failed_outputs() const { return failed_outputs_; }
    unsigned late_boundaries() const { return late_; }
    unsigned maximum_boundary_late_us() const { return maximum_late_us_; }
    bool undo_available() const { return previous_ < 3 && candidate_ == absent; }
    bool owned() const { return active_ < 3 || candidate_ < 3 || state_ != State::empty || cleanup_pending_ || (sink_ && sink_->loop_control_reset_pending()); }
    bool playing() const { return state_ == State::playing || state_ == State::overdub_armed || state_ == State::overdubbing || (state_ == State::preparing && !edit_stopped_) || state_ == State::undo_pending || (state_ == State::closing && active_ < 3); }
    bool unsaved() const { return dirty_; }
    bool backward() const { return backward_; }
    bool direction_pending() const { return direction_pending_; }
    template<class Replies>
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_status)
#endif
    void status(Replies &replies) const {
        char text[288];
        char duration[21];
        *std::to_chars(duration,duration + sizeof(duration) - 1,length()).ptr = 0;
        std::snprintf(text,sizeof(text),"CNTRL MIDI LOOPER configured=%u state=%u error=%u events=%u capacity=%u peak_events=%u peak_held=%u length=%s late=%u max_late_us=%u failed=%x discarded=%u backward=%u direction_pending=%u",
            unsigned(configured_),unsigned(state_),unsigned(error_),events(),capacity(),peak_events_,peak_held_,
            duration,late_,maximum_late_us_,failed_outputs_,capture_.discarded,unsigned(backward_),unsigned(direction_pending_));
        replies.reply(text);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_record)
#endif
    bool record(BeatPosition clock, unsigned tempo) {
        if (saving_ || backward_ || direction_pending_) return fail(Error::busy);
        if (!tempo) return fail(Error::clock);
        if (!ready(clock)) return false;
        if (state_ == State::armed || state_ == State::overdub_armed || state_ == State::closing) return true;
        if (state_ == State::recording) { close_ = std::max(origin_ + beat, next_beat(clock.value)); return true; }
        if (state_ == State::overdubbing) { close_ = next_boundary(clock.value); return true; }
        if (state_ != State::empty && state_ != State::playing && state_ != State::stopped) return fail(Error::busy);
        if (!prepare(tempo)) return false;
        restart_ = clock.restart; transport_armed_ = true;
        if (active_ == absent) { boundary_ = next_beat(clock.value); state_ = State::armed; }
        else {
            boundary_ = next_boundary(clock.value);
            if (state_ == State::stopped) { launch_with_record_ = true; state_ = State::launching; }
            else state_ = State::overdub_armed;
        }
        error_ = Error::none; return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_play_stop)
#endif
    bool play_stop(BeatPosition clock) {
        if (state_ != State::stopped && state_ != State::empty) { stop(); return true; }
        if (active_ == absent) return false;
        if (!ready(clock)) return false;
        restart_ = clock.restart; transport_armed_ = true; launch_with_record_ = false;
        boundary_ = next_boundary(clock.value); state_ = State::launching; error_ = Error::none; return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_undo)
#endif
    bool undo(BeatPosition clock) {
        if (state_ == State::undo_pending) return true;
        if (saving_ || !undo_available() || (state_ != State::playing && state_ != State::stopped)) return fail(Error::busy);
        if (state_ == State::stopped) { std::swap(active_, previous_); previous_ = absent; dirty_ = true; return true; }
        if (!ready(clock)) return false;
        boundary_ = next_boundary(clock.value); state_ = State::undo_pending; return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_reverse)
#endif
    bool reverse(BeatPosition clock) {
        if (saving_ || direction_pending_ || active_ == absent || (state_ != State::playing && state_ != State::stopped)) return fail(Error::busy);
        if (state_ == State::playing && !ready(clock)) return false;
        bool stopped = state_ == State::stopped;
        if (!generations_[active_].data.prepare_reverse()) return fail(Error::memory);
        if (!prepare(500000)) return false;
        auto &g = generations_[candidate_]; g.length = length(); g.count = generations_[active_].count;
        merge_count_ = g.count; merge_cursor_ = 0;
        reversing_ = true; edit_stopped_ = stopped; state_ = State::preparing; error_ = Error::none;
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_direction)
#endif
    bool direction(BeatPosition clock) {
        if (active_ == absent || (state_ != State::playing && state_ != State::stopped)) return fail(Error::busy);
        if (!backward_ && !direction_pending_) {
            if (!generations_[active_].data.prepare_reverse()
                || (previous_ < 3 && !generations_[previous_].data.prepare_reverse())) return fail(Error::memory);
        }
        if (state_ == State::stopped) {
            backward_ = !backward_;
            if (!backward_) for (auto &generation : generations_) generation.data.release_reverse();
        }
        else {
            if (!ready(clock)) return false;
            direction_pending_ = !direction_pending_;
        }
        error_ = Error::none; return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_stop)
#endif
    void stop() {
        if (!owned()) return;
        recorder_.cancel();
        player_.cancel();
        if (sink_) { playback_.release(*sink_); sink_->begin_loop_control_reset(); }
        cleanup_pending_ = true;
        candidate_ = absent; held_count_ = 0; close_ = 0; first_pending_ = import_pending_ = false;
        transport_armed_ = launch_with_record_ = publishing_ = false;
        reversing_ = edit_stopped_ = direction_pending_ = false;
        state_ = active_ < 3 ? State::stopped : State::empty;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_clear)
#endif
    bool clear() {
        if (saving_) return fail(Error::busy);
        stop(); active_ = previous_ = absent; state_ = State::empty; dirty_ = backward_ = false; length_ = 0; error_ = Error::none; return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_save_begin)
#endif
    MidiFile *save_begin() {
        if (saving_ || active_ == absent || candidate_ < 3 || (state_ != State::playing && state_ != State::stopped)) { fail(Error::busy); return nullptr; }
        saving_ = true; auto &g = generations_[active_]; g.data.length = g.length;
        export_.bind(g.data); return &export_;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_save_complete)
#endif
    void save_complete(bool success) {
        saving_ = false;
        if (success) { dirty_ = false; if (error_ == Error::file || error_ == Error::busy) error_ = Error::none; }
        else error_ = Error::file;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_load_begin)
#endif
    bool load_begin(MidiFile &source) {
        if (!configured_ || saving_ || candidate_ < 3 || (state_ != State::empty && state_ != State::playing && state_ != State::stopped)) return fail(Error::busy);
        if (!prepare(500000)) return false;
        player_.cancel(); playback_.release(*sink_); sink_->begin_loop_control_reset(); cleanup_pending_ = true;
        import_source_ = &source; import_stage_ = import_cursor_ = 0;
        import_index_.reserve(0);
        state_ = State::loading; error_ = Error::none; return true;
    }
    void connection(unsigned output) {
        if (outputs_ & (1u << output)) { outputs_ &= ~(1u << output); failed_outputs_ |= 1u << output; error_ = Error::output; }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_input_connection)
#endif
    void input_connection(unsigned input) {
        if (!configured_) return;
        if (state_ == State::recording || state_ == State::overdubbing)
            for (unsigned i = 0; i < held_count_; ++i) if (held(i).source / 16u == input) { abort_candidate(Error::input); break; }
        capture_.forget_input(input);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_receive)
#endif
    void receive(unsigned input, unsigned cable, const Packet &packet, BeatPosition clock) {
        if ((state_ != State::recording && state_ != State::overdubbing) || !clock.running || clock.restart != restart_
            || clock.value < capture_origin_ || (close_ && clock.value >= close_) || input >= 16
            || !(settings_.inputs & (1u << input)) || packet.bytes[0] < 0x80 || packet.bytes[0] >= 0xf0
            || !(settings_.channels & (1u << (packet.bytes[0] & 15)))) return;
        uint64_t position = clock.value - capture_origin_;
        if (position > maximum_beats * beat) { abort_candidate(Error::capacity); return; }
        if (state_ == State::overdubbing && !capture_edges(position)) return;
        capture(packet,input * 16 + cable,position);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_looper_poll)
#endif
    void poll(BeatPosition clock, bool clock_available, unsigned available, Sink &sink, unsigned quarter_us = 500000) {
        sink.reclaim_memory();
        sink.poll_loop_control_reset(available);
        if (!configured_ && !shutdown_) return;
        bool first_played = false;
        if (quarter_us) quarter_us_ = quarter_us;
        if (state_ == State::empty && !cleanup_pending_ && !shutdown_ && !sink.loop_control_reset_pending()) { release_buffers(sink); last_restart_ = clock.restart; return; }
        if (candidate_ == absent && (state_ == State::playing || state_ == State::stopped)) {
            scratch_.clear(); order_.clear(); held_.clear(); capture_.release(); recorder_.release_storage(); import_index_.reserve(0);
            for (unsigned i = 0; i < generations_.size(); ++i) {
                auto &generation = generations_[i];
                if (i == active_ || i == previous_) generation.data.trim();
                else generation.data.clear();
            }
        }
        if (!playing()) playback_.release(sink);
        if (shutdown_) {
            cleanup(sink,0xffff,available);
            if (sink.loop_cleaning() || sink.loop_control_reset_pending()) return;
            release_buffers(sink);
            configured_ = shutdown_ = cleanup_pending_ = false; return;
        }
        if (!configured_) return;
        if (state_ == State::loading) {
            cleanup(sink,0xffff,available);
            if (!sink.loop_cleaning() && !sink.loop_control_reset_pending()) load_step(sink);
            return;
        }
        if ((playing() || state_ == State::armed || state_ == State::recording || state_ == State::launching) && (!clock_available || !clock.running)) {
            bool armed = transport_armed_ && clock_available && active_ < 3;
            stop(); transport_armed_ = armed;
            if (!clock_available) error_ = Error::clock;
        }
        if (clock.running && transport_armed_ && (clock.restart != restart_ || state_ == State::stopped)) {
            recorder_.cancel(); candidate_ = absent; held_count_ = 0;
            reversing_ = edit_stopped_ = direction_pending_ = false;
            player_.cancel(); playback_.release(sink); restart_ = clock.restart;
            if (active_ < 3) {
                if (clock.restart != last_restart_) origin_ = 0;
                boundary_ = next_boundary(clock.value, true); state_ = State::launching;
            } else { abort_candidate(Error::clock); return; }
        }
        last_restart_ = clock.restart;
        cleanup(sink,!playing() ? 0xffff : failed_outputs_,available);
        if (!sink.loop_cleaning() && !sink.loop_control_reset_pending()) cleanup_pending_ = false;
        if (edit_stopped_ && state_ == State::preparing) { prepare_step(0); return; }
        if (!clock.running) return;
        if (state_ == State::armed && clock.value >= boundary_) {
            if (late_us(clock.value - boundary_) > 1000 || sink.loop_cleaning() || sink.loop_control_reset_pending()) { abort_candidate(Error::late); return; }
            origin_ = capture_origin_ = boundary_; close_ = 0; state_ = State::recording;
        }
        if (state_ == State::launching && clock.value >= boundary_) {
            if (late_us(clock.value - boundary_) > 1000) { stop(); error_ = Error::late; return; }
            if (!launch(sink,available)) return;
            capture_origin_ = boundary_; cycle_ = (boundary_ - origin_) / length();
            state_ = launch_with_record_ ? State::overdubbing : State::playing; launch_with_record_ = false;
        }
        if ((state_ == State::recording || state_ == State::overdubbing) && clock.value - capture_origin_ >= maximum_beats * beat
            && (!close_ || close_ - capture_origin_ > maximum_beats * beat)) { abort_candidate(Error::capacity); return; }
        if ((state_ == State::recording || state_ == State::overdubbing) && close_ && clock.value >= close_) close_take();
        if (state_ == State::overdubbing && !capture_edges(clock.value - capture_origin_)) return;
        if ((state_ == State::recording || state_ == State::overdubbing)
            && !capture_.flush(clock.value - capture_origin_,false,{this,emit_capture})) {
            if (candidate_ < 3) abort_candidate(Error::capacity);
            return;
        }
        if (state_ == State::closing) {
            first_pending_ = active_ == absent;
            if (first_pending_ && late_us(clock.value - close_) > 1000) { abort_candidate(Error::late); return; }
            if (!begin_merge(!first_pending_)) return;
            if (first_pending_) {
                outputs_ = settings_.outputs & available; failed_outputs_ = settings_.outputs & ~available; cycle_ = 1;
                if (failed_outputs_) error_ = Error::output;
                if (!play_generation(candidate_)) { abort_candidate(Error::file); return; }
                playback_.poll((clock.value - origin_) % length(),available & outputs_,sink);
                first_played = true;
            }
        }
        if (state_ == State::preparing && !publishing_) prepare_step((clock.value - origin_) / length());
        if (!playing()) return;
        uint64_t current_cycle = (clock.value - origin_) / length();
        if (current_cycle != cycle_) {
            unsigned late = late_us((clock.value - origin_) % length());
            maximum_late_us_ = std::max(maximum_late_us_,late);
            if (late > 1000 || current_cycle > cycle_ + 1) ++late_;
            if (current_cycle > cycle_ + 1) { failed_outputs_ |= outputs_; outputs_ = 0; error_ = Error::late; playback_.release(sink); }
            unsigned unfinished = 0;
            for (unsigned output = 0; output < 16; ++output) if ((outputs_ & (1u << output)) && !playback_.done(output)
                && !playback_.finish(output,sink)) { playback_.release(sink,1u << output); unfinished |= 1u << output; }
            cleanup(sink,0xffff,available);
            for (unsigned output = 0; output < 16; ++output) if ((outputs_ & (1u << output))
                && (sink.loop_cleaning(1u << output) || ((unfinished & (1u << output)) && !playback_.finish(output,sink)))) {
                failed_outputs_ |= 1u << output; outputs_ &= ~(1u << output); error_ = Error::output;
            }
            cycle_ = current_cycle;
            if (direction_pending_) {
                backward_ = !backward_; direction_pending_ = false;
                if (!rebind()) { stop(); error_ = Error::memory; return; }
            }
            if (state_ == State::undo_pending) {
                active_ = previous_; previous_ = absent; dirty_ = true; state_ = State::playing;
                if (!rebind()) { stop(); error_ = Error::memory; return; }
            }
            if (publishing_ && current_cycle >= publication_cycle_) {
                previous_ = active_; active_ = candidate_; candidate_ = absent; publishing_ = false; dirty_ = true; state_ = State::playing;
                if (!rebind()) { stop(); error_ = Error::memory; return; }
            }
            playback_.rewind();
            if (state_ == State::overdub_armed && clock.value >= boundary_) { capture_origin_ = origin_ + cycle_ * length(); capture_cycle_ = 0; close_ = 0; state_ = State::overdubbing; }
        }
        if (!first_played) playback_.poll((clock.value - origin_) % length(), available & outputs_, sink);
    }
private:
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_capture)
#endif
    void capture(const Packet &packet, unsigned source, uint64_t tick) {
        unsigned kind = packet.bytes[0] >> 4, channel = packet.bytes[0] & 15;
        bool note = kind == 8 || kind == 9, hold = kind == 11 && packet.bytes[1] >= 64 && packet.bytes[1] <= 69;
        if (note || hold) {
            unsigned key = packet.bytes[1] + (hold ? 128 : 0), i = 0;
            while (i < held_count_ && (held(i).source != source || held(i).channel != channel || held(i).key != key)) ++i;
            bool on = note ? kind == 9 && packet.bytes[2] : packet.bytes[2] >= 64;
            if (on) {
                if (i == held_count_) {
                    if (held_count_ == held_capacity()) { abort_candidate(Error::capacity); return; }
                    if (!reserve_held(held_count_ + 1)) { abort_candidate(Error::memory); return; }
                    held(held_count_++) = {uint16_t(source),1,uint8_t(channel),uint8_t(key)};
                } else if (note) ++held(i).count;
            } else {
                if (i == held_count_) return;
                if (hold || !--held(i).count) held(i) = held(--held_count_);
            }
        }
        const unsigned failures = memory_failures;
        if (!capture_.receive(source,packet,tick,{this,emit_capture}) && candidate_ < 3)
            abort_candidate(memory_failures != failures ? Error::memory : Error::capacity);
    }
    static constexpr unsigned absent = 3;
    struct Generation { CompactLoop data; uint64_t length = 0, last_time = 0; unsigned count = 0; };
    struct Held { uint16_t source, count; uint8_t channel, key; };
    MidiFilePlayer &player_;
    MidiFileRecorder &recorder_;
    LooperSettings settings_;
    std::array<Generation,3> generations_;
    CompactLoop scratch_;
    LoopFile import_index_;
    CompactLoopPlayback playback_;
    CompactLoopSmf export_;
    LoopCapture capture_;
    MemoryBuffer order_;
    MemoryBuffer held_;
    Sink *sink_ = nullptr;
    unsigned held_count_ = 0, active_ = absent, previous_ = absent, candidate_ = absent;
    unsigned outputs_ = 0, failed_outputs_ = 0, late_ = 0, restart_ = 0, last_restart_ = 0;
    unsigned merge_count_ = 0, merge_cursor_ = 0, base_count_ = 0;
    MidiFile *import_source_ = nullptr;
    unsigned import_stage_ = 0, import_cursor_ = 0, import_count_ = 0;
    uint64_t import_length_ = 0;
    uint64_t origin_ = 0, capture_origin_ = 0, boundary_ = 0, close_ = 0, length_ = 0, cycle_ = 0, capture_cycle_ = 0, publication_cycle_ = 0;
    State state_ = State::empty;
    Error error_ = Error::none;
    bool configured_ = false, transport_armed_ = false, launch_with_record_ = false, publishing_ = false, dirty_ = false, saving_ = false, shutdown_ = false, first_pending_ = false, import_pending_ = false, merging_ = false, cleanup_pending_ = false;
    bool backward_ = false, direction_pending_ = false, reversing_ = false, edit_stopped_ = false;
    unsigned cleanup_cursor_ = 0;
    unsigned quarter_us_ = 500000, maximum_late_us_ = 0;
    unsigned peak_events_ = 0, peak_held_ = 0;
    unsigned pairing_ = 1;
    unsigned held_capacity() const { return settings_.events / 2; }
    Held &held(unsigned i) { return reinterpret_cast<Held *>(held_.data)[i]; }
    bool reserve_held(unsigned count) { return count * sizeof(Held) <= held_.size || held_.resize(((count + 7) / 8) * 8 * sizeof(Held)); }
    static bool emit_capture(void *context,const Packet *packets,unsigned count,uint64_t tick) {
        return static_cast<MidiLooper *>(context)->append_packets(packets,count,tick);
    }
    static uint64_t next_beat(uint64_t position) { return (position / beat + 1) * beat; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_late_us)
#endif
    unsigned late_us(uint64_t delta) const { return unsigned(std::min(uint64_t(UINT32_MAX),delta / beat * quarter_us_ + delta % beat * quarter_us_ / beat)); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_next_boundary)
#endif
    uint64_t next_boundary(uint64_t position, bool inclusive = false) const {
        if (position < origin_) return origin_;
        uint64_t delta = position - origin_, n = delta / length();
        return origin_ + (n + (!inclusive || delta % length() != 0)) * length();
    }
    bool fail(Error error) { error_ = error; return false; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_release_buffers)
#endif
    void release_buffers(Sink &sink) {
        for (auto &g : generations_) g.data.clear();
        scratch_.clear(); import_index_.reserve(0); order_.clear(); held_.clear(); capture_.release(); sink.reserve_loop_ownership(0);
        sink.release_loop_preparation();
        sink.reserve_playback_state(0,0,0); recorder_.release_storage(); player_.trim_prepared_storage();
    }
    bool ready(BeatPosition clock) { return configured_ && clock.running ? true : fail(Error::clock); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_cleanup)
#endif
    void cleanup(Sink &sink, unsigned outputs, unsigned available) {
        unsigned mask = outputs & available;
        for (unsigned work = 0; work < 64 && sink.loop_cleaning(mask); ++work) {
            for (unsigned i = 0; i < 16; ++i) {
                unsigned output = cleanup_cursor_++ % 16, bit = 1u << output;
                if ((mask & bit) && sink.loop_cleaning(bit)) { sink.release_loop(bit); break; }
            }
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_prepare)
#endif
    bool prepare(unsigned tempo) {
        if (candidate_ < 3 || recorder_.active() || (active_ == absent && (player_.active() || player_.cleaning()))) return fail(Error::busy);
        unsigned candidate = 0; while (candidate == active_ || candidate == previous_) ++candidate;
        auto &g = generations_[candidate];
        // These owners are unpinned while no candidate is being prepared.
        g.data.clear(); scratch_.clear();
        order_.clear(); held_.clear(); capture_.release();
        CompactLoop data;
        MemoryBuffer held;
        LoopCapture capture;
        capture.discarded = capture_.discarded;
        if (!data.reserve(std::min(32u,settings_.events),std::min(32u,settings_.events) * 3)
            || !held.resize(8 * sizeof(Held))) return fail(Error::memory);
        g.data.swap(data); held_.swap(held); capture_.swap(capture);
        reversing_ = edit_stopped_ = false;
        candidate_ = candidate; g.count = 0; g.length = g.last_time = 0;
        held_count_ = 0; capture_cycle_ = 0; close_ = 0;
        pairing_ = unsigned(std::min(uint64_t(UINT32_MAX),beat * 2000 / tempo));
        capture_.reset(pairing_);
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_append)
#endif
    bool append_packets(const Packet *packets, unsigned count, uint64_t time, bool = false) {
        unsigned reserved = 0; for (unsigned i = 0; i < held_count_; ++i) reserved += held(i).count;
        if (reserved > held_capacity()) { abort_candidate(Error::capacity); return false; }
        unsigned base = active_ < 3 && state_ != State::loading ? generations_[active_].count : 0;
        auto &g = generations_[candidate_];
        if (base + g.count + reserved + count > settings_.events) { abort_candidate(Error::capacity); return false; }
        peak_held_ = std::max(peak_held_,reserved);
        if (!g.data.append(packets,count,time,reserved)) { abort_candidate(Error::memory); return false; }
        g.count += count; g.last_time = std::max(g.last_time,time);
        peak_events_ = std::max(peak_events_,base + g.count);
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_abort_candidate)
#endif
    void abort_candidate(Error error) {
        bool was_playing = playing();
        if (state_ == State::loading || first_pending_) player_.cancel();
        recorder_.cancel(); candidate_ = absent; held_count_ = 0; publishing_ = false;
        state_ = active_ < 3 ? (was_playing ? State::playing : State::stopped) : State::empty; error_ = error;
        first_pending_ = import_pending_ = false;
        reversing_ = edit_stopped_ = false;
        if (active_ == absent) transport_armed_ = false;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_close_held)
#endif
    bool close_held(uint64_t duration) {
        while (held_count_) {
            auto &h = held(held_count_ - 1);
            Packet packet{uint8_t(h.key < 128 ? 8 : 11), {uint8_t((h.key < 128 ? 0x80 : 0xb0) | h.channel),uint8_t(h.key % 128),0}};
            if (!--h.count) --held_count_;
            if (!append_packets(&packet,1,duration,true)) return false;
        }
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_capture_edges)
#endif
    bool capture_edges(uint64_t position) {
        uint64_t cycle = position / length();
        if (cycle == capture_cycle_) return true;
        if (!capture_.flush((capture_cycle_ + 1) * length(),true,{this,emit_capture})) {
            if (candidate_ < 3) abort_candidate(Error::capacity);
            return false;
        }
        if (!close_held((capture_cycle_ + 1) * length())) return false;
        capture_cycle_ = cycle; return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_close_take)
#endif
    void close_take() {
        uint64_t duration = close_ - capture_origin_;
        if (!capture_.flush(duration,true,{this,emit_capture})) {
            if (candidate_ < 3) abort_candidate(Error::capacity);
            return;
        }
        if (!close_held(duration)) return;
        auto &g = generations_[candidate_]; g.length = active_ < 3 ? length() : duration;
        length_ = g.length;
        state_ = State::closing;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_launch)
#endif
    bool launch(Sink &sink, unsigned available) {
        if (sink.loop_control_reset_pending()) return false;
        outputs_ = settings_.outputs & available; failed_outputs_ = settings_.outputs & ~available;
        for (unsigned i = 0; i < 16; ++i) if ((outputs_ & (1u << i)) && sink.loop_cleaning(1u << i)) {
            outputs_ &= ~(1u << i); failed_outputs_ |= 1u << i;
        }
        if (failed_outputs_) error_ = Error::output;
        if (!outputs_) { stop(); return fail(Error::output); }
        if (!rebind()) { stop(); return fail(Error::file); }
        return true;
    }
    bool rebind() {
        return play_generation(active_);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_play_generation)
#endif
    bool play_generation(unsigned generation) {
        playback_.release(*sink_); auto &g = generations_[generation]; g.data.length = g.length;
        return playback_.bind(g.data,backward_);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_prepare_output)
#endif
    bool prepare_output(const CompactLoop &data) {
        if (!sink_->begin_loop_preparation()) return false;
        struct Key { unsigned id, count; };
        CheckedArray<Key> keys;
        unsigned peak = 0, outputs = 0;
        for (unsigned mask = settings_.outputs; mask; mask &= mask - 1) ++outputs;
        for (unsigned i = 0; i < data.count(); ++i) {
            MidiFilePlayer::Event event; if (!data.event(i,event)) return false;
            Packet packet{uint8_t(event.status >> 4),{event.status,event.first,event.second}};
            Parameter parameter; const Parameter *logical = nullptr;
            if (event.previous && i + 1 == event.previous) {
                MidiFilePlayer::Event second; if (!data.event(i + 1,second)) return false;
                unsigned count = event.next - event.previous;
                parameter.channel = event.status & 15;
                parameter.kind = count == 2 ? Parameter::cc14 : event.first == 99 ? (count == 5 ? Parameter::nrpn7 : Parameter::nrpn14) : Parameter::rpn14;
                parameter.address = uint16_t(count == 2 ? event.first : event.second * 128 + second.second);
                logical = &parameter;
            }
            for (unsigned output = 0; output < 16; ++output) if (settings_.outputs & (1u << output))
                if (!sink_->prepare_loop_packet(output,packet,logical)) return false;
            unsigned kind = event.status >> 4;
            bool note = kind == 8 || kind == 9;
            bool hold = kind == 11 && event.first >= 64 && event.first <= 69;
            if (!note && !hold) continue;
            unsigned id = (event.status & 15) * 256 + event.first + unsigned(hold) * 128, j = 0;
            while (j < keys.size() && keys[j].id != id) ++j;
            bool on = note ? kind == 9 && event.second : event.second >= 64;
            if (on) {
                if (j == keys.size()) {
                    if (keys.size() == keys.capacity() && !keys.reserve(keys.size() + 8)) return false;
                    if (!keys.append({id,1})) return false;
                    peak = std::max(peak,keys.size());
                } else if (note) ++keys[j].count;
            } else if (j < keys.size() && (hold || !--keys[j].count)) {
                keys[j] = keys.back(); keys.resize(keys.size() - 1);
            }
        }
        return sink_->finish_loop_preparation(peak * outputs);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_merge_event)
#endif
    MidiFilePlayer::Event merge_event(unsigned index) {
        MidiFilePlayer::Event event;
        if (index < base_count_) generations_[active_].data.event(index,event);
        else {
            generations_[candidate_].data.event(index - base_count_,event);
            if (merging_) {
                unsigned kind = event.status >> 4;
                bool release = kind == 8 || (kind == 9 && !event.second) || (kind == 11 && event.first >= 64 && event.first <= 69 && event.second < 64);
                if (release && event.time) event.time = (event.time - 1) % length() + 1;
                else event.time %= length();
            }
        }
        return event;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_begin_merge)
#endif
    bool begin_merge(bool merge) {
        merging_ = merge;
        base_count_ = merge ? generations_[active_].count : 0;
        merge_count_ = base_count_ + generations_[candidate_].count; merge_cursor_ = 0;
        if (!merge) return begin_file();
        if (!order_.resize(merge_count_ * sizeof(unsigned))) { abort_candidate(Error::memory); return false; }
        auto *order = reinterpret_cast<unsigned *>(order_.data);
        for (unsigned i = 0; i < merge_count_; ++i) new (order + i) unsigned(i);
        std::sort(order,order + merge_count_,[&](unsigned a, unsigned b) {
            auto left = merge_event(a), right = merge_event(b);
            return left.time == right.time ? a < b : left.time < right.time;
        });
        scratch_.clear();
        for (unsigned i = 0; i < merge_count_;) {
            auto event = merge_event(order[i]);
            unsigned count = event.previous ? event.next - event.previous : 1;
            Packet packets[6];
            if (count > 6 || count > merge_count_ - i) { abort_candidate(Error::file); return false; }
            for (unsigned j = 0; j < count; ++j) {
                auto part = merge_event(order[i + j]);
                packets[j] = {uint8_t(part.status >> 4),{part.status,part.first,part.second}};
            }
            if (!scratch_.append(packets,count,event.time)) { abort_candidate(Error::memory); return false; }
            i += count;
        }
        auto &g = generations_[candidate_]; g.data.swap(scratch_); g.count = merge_count_;
        scratch_.clear(); order_.clear();
        return begin_file();
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_begin_file)
#endif
    bool begin_file() {
        auto &g = generations_[candidate_]; g.data.length = g.length;
        merge_count_ = g.count; merge_cursor_ = 0;
        if (backward_ && !g.data.prepare_reverse()) { abort_candidate(Error::memory); return false; }
        if (!prepare_output(g.data)) { abort_candidate(Error::memory); return false; }
        if (!import_pending_) state_ = State::preparing;
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_prepare_step)
#endif
    void prepare_step(uint64_t current_cycle) {
        if (reversing_) {
            for (unsigned work = 0; work < 16 && merge_cursor_ < merge_count_;) {
                auto &g = generations_[active_];
                MidiFilePlayer::Event event;
                if (!g.data.reversed(merge_cursor_,event)) { abort_candidate(Error::file); return; }
                unsigned count = event.previous ? event.next - event.previous : 1;
                Packet packets[6];
                if (count > 6 || count > merge_count_ - merge_cursor_) { abort_candidate(Error::file); return; }
                for (unsigned j = 0; j < count; ++j) {
                    MidiFilePlayer::Event part; if (!g.data.reversed(merge_cursor_ + j,part)) { abort_candidate(Error::file); return; }
                    packets[j] = {uint8_t(part.status >> 4),{part.status,part.first,part.second}};
                }
                if (!generations_[candidate_].data.append(packets,count,event.time)) { abort_candidate(Error::memory); return; }
                merge_cursor_ += count; work += count;
            }
            if (merge_cursor_ == merge_count_) {
                if (!backward_) generations_[active_].data.release_reverse();
                reversing_ = false; merge_cursor_ = 0; begin_file();
            }
            return;
        }
        merge_step(current_cycle);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_merge_step)
#endif
    void merge_step(uint64_t current_cycle = 0) {
        held_count_ = 0;
        if (edit_stopped_) {
            previous_ = active_; active_ = candidate_; candidate_ = absent;
            edit_stopped_ = false; dirty_ = true; state_ = State::stopped;
        } else if (first_pending_ || import_pending_) {
            active_ = candidate_; candidate_ = absent; previous_ = absent;
            state_ = import_pending_ ? State::stopped : State::playing;
            dirty_ = !import_pending_; if (import_pending_) backward_ = false; first_pending_ = import_pending_ = false;
        } else { publishing_ = true; publication_cycle_ = current_cycle + 1; }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_load_step)
#endif
    void load_step(Sink &sink) {
        if (import_stage_ == 4) { merge_step(); return; }
        if (import_stage_ == 0) {
            if (!player_.bind(*import_source_,import_index_) || !player_.start({},0,true)) { abort_candidate(Error::file); return; }
            import_stage_ = 1;
        }
        if (import_stage_ == 1) {
            player_.poll(0,0,sink);
            if (player_.status() == MidiFilePlayer::Status::failed) { abort_candidate(Error::file); return; }
            if (player_.event_count() > settings_.events) { abort_candidate(Error::capacity); return; }
            if (player_.status() == MidiFilePlayer::Status::indexing) return;
            import_length_ = player_.duration(); import_count_ = player_.event_count(); player_.cancel();
            if (!import_length_ || import_length_ > maximum_beats * beat || import_count_ > settings_.events) { abort_candidate(Error::capacity); return; }
            import_length_ = (import_length_ + 128) & ~uint64_t(255);
            if (!import_length_) { abort_candidate(Error::capacity); return; }
            import_stage_ = 2;
        }
        if (import_stage_ == 2) {
            for (unsigned i = 0; i < 16 && import_cursor_ < import_count_; ++i) {
                MidiFilePlayer::Event event;
                if (!MidiFilePlayer::read_event(import_index_,import_cursor_++,event) || event.status < 0x80 || event.status >= 0xf0) { abort_candidate(Error::file); return; }
                capture({uint8_t(event.status >> 4),{event.status,event.first,event.second}},0,event.time);
                if (state_ != State::loading) return;
            }
            if (import_cursor_ != import_count_) return;
            if (!capture_.flush(import_length_,true,{this,emit_capture}) || !close_held(import_length_)) {
                if (candidate_ < 3) abort_candidate(Error::capacity);
                return;
            }
            generations_[candidate_].length = length_ = import_length_;
            import_pending_ = true; import_stage_ = 4;
            begin_merge(false); return;
        }
    }
};
} // namespace cntrl::midi
