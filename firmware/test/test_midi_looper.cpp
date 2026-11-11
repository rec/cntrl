#include <cntrl/midi_looper.h>
#include <cassert>
#include <cstdio>
#include <vector>

using namespace cntrl::midi;
struct RecordingSink : cntrl::midi::Sink {
    std::vector<Packet> sent[2];
    unsigned blocked = 0;
    unsigned allowance = UINT32_MAX;
    bool write_packet(unsigned output, const Packet &packet) override {
        if ((blocked & (1u << output)) || !allowance) return false;
        --allowance;
        sent[output].push_back(packet); return true;
    }
};
struct Session {
    LoopFile source, index;
    MidiFilePlayer player{source,index};
    MidiFileRecorder recorder{source};
    MidiLooper loop{player,recorder};
    RecordingSink sink;
    uint64_t position = 0;
    unsigned per_poll = UINT32_MAX;
    Session(unsigned events = 512, unsigned outputs = 1) {
        assert(source.reserve(128) && index.reserve(128));
        LooperSettings settings; settings.events = events; settings.outputs = uint16_t(outputs);
        assert(loop.configure(settings,sink));
    }
    BeatPosition clock() const { return {position,1,true}; }
    void poll(uint64_t value, unsigned count = 1) {
        position = value;
        for (unsigned i = 0; i < count; ++i) { sink.allowance = per_poll; loop.poll(clock(),true,3,sink); }
    }
    void note(unsigned key, bool on) { loop.receive(0,0,{uint8_t(on ? 9 : 8),{uint8_t(on ? 0x90 : 0x80),uint8_t(key),uint8_t(on ? 100 : 0)}},clock()); }
    void take() {
        assert(loop.record(clock(),500000)); poll(MidiLooper::beat); note(60,true);
        poll(MidiLooper::beat + MidiLooper::beat / 2); note(60,false);
        assert(loop.record(clock(),500000)); poll(MidiLooper::beat * 2);
        assert(loop.state() == MidiLooper::State::playing);
    }
};
void first_wrap_and_thousands_of_irregular_cycles_keep_absolute_phase() {
    Session s; s.take();
    assert(s.loop.length() == MidiLooper::beat && s.sink.sent[0].size() == 1);
    for (unsigned i = 2; i < 4002; ++i) {
        s.poll(MidiLooper::beat * i + MidiLooper::beat / 2);
        s.poll(MidiLooper::beat * (i + 1) + (i % 17) * MidiLooper::beat / 16384);
    }
    assert(s.loop.late_boundaries() == 0 && s.loop.failed_outputs() == 0);
    assert(s.loop.maximum_boundary_late_us() <= 489);
    assert(s.sink.sent[0].size() == 8001);
    s.loop.stop(); s.loop.poll(s.clock(),true,3,s.sink);
    assert(!s.sink.loop_cleaning());
}
void a_silent_take_has_a_positive_length_and_can_be_stopped() {
    Session s; assert(s.loop.record(s.clock(),500000)); s.poll(MidiLooper::beat);
    assert(s.loop.record(s.clock(),500000)); s.poll(MidiLooper::beat * 2);
    assert(s.loop.state() == MidiLooper::State::playing && s.loop.events() == 0);
    assert(s.sink.sent[0].empty()); s.loop.stop();
    assert(s.loop.state() == MidiLooper::State::stopped);
}
void overdub_is_published_at_a_boundary_and_undo_has_one_level() {
    Session s; s.take(); assert(s.loop.record(s.clock(),500000));
    s.poll(MidiLooper::beat * 2 + MidiLooper::beat / 2);
    s.poll(MidiLooper::beat * 3); assert(s.loop.state() == MidiLooper::State::overdubbing);
    s.note(64,true); s.poll(MidiLooper::beat * 3 + MidiLooper::beat / 2); s.note(64,false);
    assert(s.loop.record(s.clock(),500000)); s.poll(MidiLooper::beat * 4,16);
    assert(s.loop.state() == MidiLooper::State::preparing);
    s.poll(MidiLooper::beat * 4 + MidiLooper::beat / 2);
    s.poll(MidiLooper::beat * 5,8); assert(s.loop.events() == 4 && s.loop.undo_available());
    assert(s.loop.undo(s.clock())); s.poll(MidiLooper::beat * 5 + MidiLooper::beat / 2,8);
    s.poll(MidiLooper::beat * 6,8); assert(s.loop.events() == 2 && !s.loop.undo_available());
}
void stopping_an_unowned_session_does_not_cancel_an_ordinary_recording() {
    Session s; assert(s.recorder.start({},0)); s.loop.stop(); assert(s.recorder.active());
    assert(!s.loop.record(s.clock(),500000)); assert(s.recorder.active());
}
void a_blocked_destination_retires_without_stalling_a_healthy_destination() {
    Session s(512,3); s.take(); s.sink.blocked = 2;
    s.poll(MidiLooper::beat * 2 + MidiLooper::beat / 2);
    s.poll(MidiLooper::beat * 3);
    assert(s.loop.failed_outputs() == 2 && s.sink.sent[0].size() == 3);
    unsigned failed_count = unsigned(s.sink.sent[1].size());
    s.sink.blocked = 0; s.poll(MidiLooper::beat * 3 + MidiLooper::beat / 2);
    assert(s.sink.sent[1].size() == failed_count + 1 && !s.sink.loop_cleaning(2));
    s.poll(MidiLooper::beat * 4);
    assert(s.sink.sent[1].size() == failed_count + 1 && s.sink.sent[0].size() == 5);
}
void capacity_failure_discards_only_the_candidate() {
    Session s(4); s.take(); assert(s.loop.record(s.clock(),500000));
    s.poll(MidiLooper::beat * 2 + MidiLooper::beat / 2); s.poll(MidiLooper::beat * 3);
    s.note(64,true); s.note(67,true);
    assert(s.loop.error() == MidiLooper::Error::capacity && s.loop.state() == MidiLooper::State::playing);
    assert(s.loop.events() == 2 && !s.loop.undo_available());
}
void fine_controls_and_parameters_are_complete_atomic_transactions() {
    Session s; assert(s.loop.record(s.clock(),500000)); s.poll(MidiLooper::beat);
    auto cc = [&](unsigned input, unsigned cable, unsigned control, unsigned value) {
        s.loop.receive(input,cable,{11,{0xb0,uint8_t(control),uint8_t(value)}},s.clock());
    };
    cc(0,0,1,65); s.position += MidiLooper::beat / MidiLooper::ppqn * 4;
    s.loop.receive(1,3,{9,{0x90,62,101}},s.clock());
    s.position += MidiLooper::beat / MidiLooper::ppqn * 4; cc(0,0,33,17);
    cc(2,1,99,4); cc(2,1,98,5); cc(2,1,6,6); cc(2,1,38,7);
    cc(3,0,101,0); // Incomplete selector never reaches playback.
    assert(s.loop.record(s.clock(),500000)); s.poll(MidiLooper::beat * 2);
    assert(s.loop.discarded_groups() == 1);
    s.per_poll = 1;
    s.position = MidiLooper::beat * 2 + MidiLooper::beat / 4;
    unsigned locked = 0;
    for (unsigned i = 0; i < 16; ++i) {
        s.poll(s.position);
        if (s.sink.parameter_locked(0)) { ++locked; assert(!s.sink.submit(0,{11,{0xb0,7,99}})); }
    }
    assert(locked == 6);
    assert(s.sink.sent[0].size() == 9);
    const unsigned controls[] = {1,33,99,98,6,38,101,100};
    for (unsigned i = 0; i < 8; ++i) assert(s.sink.sent[0][i + 1].bytes[1] == controls[i]);
    s.loop.stop(); s.loop.poll(s.clock(),true,3,s.sink);
    assert(!s.sink.parameter_locked(0) && !s.sink.loop_cleaning());
}
void loading_preserves_fractional_beats_and_rejects_system_events() {
    Session s; s.take(); s.loop.stop(); s.loop.poll(s.clock(),true,3,s.sink);
    LoopFile source, index; assert(source.reserve(512) && index.reserve(512));
    MidiFileRecorder recorder(source); assert(recorder.start_prepared({},index,480,500000));
    recorder.receive_tick(0,{9,{0x90,67,99}},0); recorder.poll(0);
    recorder.receive_tick(0,{8,{0x80,67,0}},240); recorder.poll(0);
    assert(recorder.request_finish_tick(720)); for (unsigned i = 0; i < 8; ++i) recorder.poll(0);
    assert(recorder.complete());
    LoopFile validation_index; assert(validation_index.reserve(512));
    MidiFilePlayer validation(source,validation_index);
    assert(validation.start({},0,true));
    for (unsigned i = 0; i < 64 && validation.status() == MidiFilePlayer::Status::indexing; ++i) validation.poll(0,0,s.sink);
    if (validation.status() != MidiFilePlayer::Status::playing || validation.duration() != MidiLooper::beat * 3 / 2) {
        std::fprintf(stderr,"musical SMF status=%u duration=%llu bytes=%u\n",unsigned(validation.status()),(unsigned long long)validation.duration(),unsigned(source.size()));
        for (unsigned i = 0; i < source.size(); ++i) { uint8_t b; assert(source.read(i,&b,1)); std::fprintf(stderr,"%02x ",unsigned(b)); }
        std::fprintf(stderr,"\n");
    }
    assert(validation.status() == MidiFilePlayer::Status::playing && validation.duration() == MidiLooper::beat * 3 / 2);
    validation.cancel();
    assert(s.loop.load_begin(source));
    for (unsigned i = 0; i < 64 && s.loop.state() == MidiLooper::State::loading; ++i) s.loop.poll({0,1,false},true,3,s.sink);
    if (s.loop.state() != MidiLooper::State::stopped || s.loop.length() != MidiLooper::beat * 3 / 2 || s.loop.events() != 2)
        std::fprintf(stderr,"import state=%u error=%u length=%llu events=%u player=%u recorder_failed=%u recorded=%u\n",unsigned(s.loop.state()),unsigned(s.loop.error()),(unsigned long long)s.loop.length(),s.loop.events(),unsigned(s.player.status()),unsigned(s.recorder.failed()),s.recorder.prepared_count());
    assert(s.loop.state() == MidiLooper::State::stopped && s.loop.length() == MidiLooper::beat * 3 / 2 && s.loop.events() == 2);
    source.reset(); assert(recorder.start({},0)); recorder.receive(0,{15,{0xf8,0,0}},0); assert(recorder.finish(500000));
    assert(s.loop.load_begin(source));
    for (unsigned i = 0; i < 64 && s.loop.state() == MidiLooper::State::loading; ++i) s.loop.poll({0,1,false},true,3,s.sink);
    assert(s.loop.error() == MidiLooper::Error::file && s.loop.state() == MidiLooper::State::stopped);
    assert(s.loop.length() == MidiLooper::beat * 3 / 2 && s.loop.events() == 2);
}
void transport_pause_continue_and_clock_loss_have_distinct_restart_behavior() {
    Session s; s.take();
    s.loop.poll({s.position,1,false},true,3,s.sink);
    assert(s.loop.state() == MidiLooper::State::stopped && !s.sink.loop_cleaning());
    s.poll(MidiLooper::beat * 2 + MidiLooper::beat / 2);
    assert(s.loop.state() == MidiLooper::State::launching);
    s.poll(MidiLooper::beat * 3); assert(s.loop.state() == MidiLooper::State::playing);
    s.loop.poll(s.clock(),false,3,s.sink);
    assert(s.loop.state() == MidiLooper::State::stopped && s.loop.error() == MidiLooper::Error::clock);
    s.poll(MidiLooper::beat * 4);
    assert(s.loop.state() == MidiLooper::State::stopped);
}
void a_missed_first_wrap_is_rejected_and_hold_sources_are_unioned() {
    Session late; assert(late.loop.record(late.clock(),500000)); late.poll(MidiLooper::beat);
    late.note(60,true); assert(late.loop.record(late.clock(),500000)); late.poll(MidiLooper::beat * 2 + MidiLooper::beat / 10);
    assert(late.loop.error() == MidiLooper::Error::late && late.loop.state() == MidiLooper::State::empty && late.sink.sent[0].empty());
    Session s; assert(s.loop.record(s.clock(),500000)); s.poll(MidiLooper::beat);
    auto hold = [&](unsigned cable, unsigned value) { s.loop.receive(0,cable,{11,{0xb0,64,uint8_t(value)}},s.clock()); };
    hold(0,80); s.position += MidiLooper::beat / 8; hold(1,100);
    s.position += MidiLooper::beat / 8; hold(0,0);
    s.position += MidiLooper::beat / 8; hold(1,0);
    assert(s.loop.record(s.clock(),500000)); s.poll(MidiLooper::beat * 2);
    s.poll(MidiLooper::beat * 2 + MidiLooper::beat / 4); assert(s.sink.sent[0].size() == 2);
    assert(s.sink.sent[0][0].bytes[2] == 80 && s.sink.sent[0][1].bytes[2] == 100);
    s.poll(MidiLooper::beat * 2 + MidiLooper::beat / 2); assert(s.sink.sent[0].size() == 3 && s.sink.sent[0][2].bytes[2] == 0);
}
void a_full_take_and_multiple_overdub_passes_preserve_events() {
    Session full; assert(full.loop.record(full.clock(),500000)); full.poll(MidiLooper::beat);
    for (unsigned i = 0; i < 256; ++i) {
        full.position = MidiLooper::beat + i * MidiLooper::beat / 512;
        full.note(60,true); full.note(60,false);
    }
    assert(full.loop.record(full.clock(),500000)); full.poll(MidiLooper::beat * 2);
    assert(full.loop.error() == MidiLooper::Error::none && full.loop.events() == 512);
    for (unsigned i = 1; i <= 512; ++i) full.poll(MidiLooper::beat * 2 + i * MidiLooper::beat / 1024);
    assert(full.loop.state() == MidiLooper::State::playing && full.sink.sent[0].size() == 512);

    Session s; s.take(); assert(s.loop.record(s.clock(),500000));
    s.poll(MidiLooper::beat * 2 + MidiLooper::beat / 2); s.poll(MidiLooper::beat * 3);
    s.note(64,true); // The first pass closes this held note at its boundary.
    s.poll(MidiLooper::beat * 3 + MidiLooper::beat / 2);
    s.poll(MidiLooper::beat * 4); s.note(67,true);
    s.poll(MidiLooper::beat * 4 + MidiLooper::beat / 2); s.note(67,false);
    assert(s.loop.record(s.clock(),500000)); s.poll(MidiLooper::beat * 5,16);
    s.poll(MidiLooper::beat * 5 + MidiLooper::beat / 2); s.poll(MidiLooper::beat * 6);
    assert(s.loop.events() == 6 && s.loop.undo_available());
    s.poll(MidiLooper::beat * 6 + MidiLooper::beat / 2);
    s.loop.poll({MidiLooper::beat * 7,2,true},true,3,s.sink,250000);
    assert(s.loop.state() == MidiLooper::State::playing && !s.loop.failed_outputs());
    s.loop.stop(); s.loop.poll({MidiLooper::beat * 7,2,true},true,3,s.sink);
    assert(!s.sink.loop_cleaning());
}
void parameter_selection_survives_complete_overdub_passes() {
    Session s; s.take(); assert(s.loop.record(s.clock(),500000));
    s.poll(MidiLooper::beat * 2 + MidiLooper::beat / 2); s.poll(MidiLooper::beat * 3);
    auto cc = [&](unsigned control, unsigned value) {
        s.loop.receive(0,0,{11,{0xb0,uint8_t(control),uint8_t(value)}},s.clock());
    };
    s.position += MidiLooper::beat / 4; cc(99,4); cc(98,5); cc(6,6);
    s.poll(MidiLooper::beat * 3 + MidiLooper::beat / 2);
    s.poll(MidiLooper::beat * 4);
    s.position += MidiLooper::beat / 4; cc(6,7);
    s.poll(MidiLooper::beat * 4 + MidiLooper::beat / 2);
    assert(s.loop.record(s.clock(),500000)); s.poll(MidiLooper::beat * 5,16);
    s.poll(MidiLooper::beat * 5 + MidiLooper::beat / 2); s.poll(MidiLooper::beat * 6);
    assert(s.loop.events() == 12 && s.loop.discarded_groups() == 0);
    unsigned values = 0;
    s.poll(MidiLooper::beat * 6 + MidiLooper::beat / 2);
    for (const auto &p : s.sink.sent[0]) if (p.bytes[0] == 0xb0 && p.bytes[1] == 6) {
        assert(p.bytes[2] == 6 || p.bytes[2] == 7); ++values;
    }
    assert(values == 2);
}
int main() {
    cntrl::memory_budget = 524288;
    first_wrap_and_thousands_of_irregular_cycles_keep_absolute_phase();
    a_silent_take_has_a_positive_length_and_can_be_stopped();
    overdub_is_published_at_a_boundary_and_undo_has_one_level();
    stopping_an_unowned_session_does_not_cancel_an_ordinary_recording();
    a_blocked_destination_retires_without_stalling_a_healthy_destination();
    capacity_failure_discards_only_the_candidate();
    fine_controls_and_parameters_are_complete_atomic_transactions();
    loading_preserves_fractional_beats_and_rejects_system_events();
    transport_pause_continue_and_clock_loss_have_distinct_restart_behavior();
    a_missed_first_wrap_is_rejected_and_hold_sources_are_unioned();
    a_full_take_and_multiple_overdub_passes_preserve_events();
    parameter_selection_survives_complete_overdub_passes();
    assert(cntrl::memory_used == 0);
}
