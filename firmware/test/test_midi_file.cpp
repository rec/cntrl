#include <cntrl/midi_file.h>
#include <cassert>
#include <vector>

using Bytes = std::vector<uint8_t>;
struct File : cntrl::midi::MidiFile {
    Bytes data;
    bool writable = true;
    uint32_t size() const override { return unsigned(data.size()); }
    bool read(uint32_t offset, uint8_t *out, unsigned length) override {
        if (offset > data.size() || length > data.size() - offset) return false;
        std::memcpy(out, data.data() + offset, length); return true;
    }
    bool write(uint32_t offset, const uint8_t *in, unsigned length) override {
        if (!writable) return false;
        if (offset + length > data.size()) data.resize(offset + length);
        std::memcpy(data.data() + offset, in, length); return true;
    }
    bool sync() override { return writable; }
};
struct Sink : cntrl::midi::Sink {
    Bytes sent[9];
    bool blocked[9] = {};
    bool write_packet(unsigned route, const cntrl::midi::Packet &p) override {
        if (blocked[route]) return false;
        sent[route].insert(sent[route].end(), p.bytes, p.bytes + p.size()); return true;
    }
};
void append32(Bytes &bytes, unsigned n) { for (int i = 3; i >= 0; --i) bytes.push_back(uint8_t(n >> (8 * i))); }
File smf(std::initializer_list<Bytes> tracks, unsigned division = 480) {
    File file;
    file.data = {'M','T','h','d',0,0,0,6,0,uint8_t(tracks.size() > 1),0,uint8_t(tracks.size()),uint8_t(division >> 8),uint8_t(division)};
    for (const auto &track : tracks) {
        file.data.insert(file.data.end(), {'M','T','r','k'}); append32(file.data, unsigned(track.size()));
        file.data.insert(file.data.end(), track.begin(), track.end());
    }
    return file;
}
void prepare(cntrl::midi::MidiFilePlayer &player, Sink &sink, const cntrl::midi::PlayMidiFilePatch &settings = {}) {
    assert(player.start(settings));
    for (unsigned i = 0; player.status() == cntrl::midi::MidiFilePlayer::Status::indexing && i < 1000; ++i) player.poll(0, 0x17f, sink);
    assert(player.status() == cntrl::midi::MidiFilePlayer::Status::playing);
}
void pump(cntrl::midi::MidiFilePlayer &player, Sink &sink, uint32_t now) { for (unsigned i = 0; i < 20; ++i) player.poll(now, 0x17f, sink); }

void test_tempo_changes_multitrack_and_speed() {
    auto source = smf({{0,0xff,0x51,3,7,0xa1,0x20, 0x83,0x60,0xff,0x51,3,15,0x42,0x40, 0x83,0x60,0xff,0x2f,0},
                       {0,0x90,60,100, 0x83,0x60,0x80,60,0, 0x83,0x60,0xc0,12, 0,0xff,0x2f,0}});
    File index; Sink sink; cntrl::midi::MidiFilePlayer player(source,index);
    cntrl::midi::PlayMidiFilePatch settings; settings.speed = 2; settings.outputs = 3;
    prepare(player,sink,settings); pump(player,sink,0);
    assert((sink.sent[0] == Bytes{0x90,60,100}));
    pump(player,sink,249999); assert(sink.sent[0].size() == 3);
    pump(player,sink,250000); assert(sink.sent[0].size() == 6);
    pump(player,sink,749999); assert(sink.sent[0].size() == 6);
    pump(player,sink,750000);
    assert((sink.sent[0] == Bytes{0x90,60,100,0x80,60,0,0xc0,12}));
    assert(sink.sent[0] == sink.sent[1]);
    assert(player.status() == cntrl::midi::MidiFilePlayer::Status::complete);
}
void test_reverse_preserves_velocity_and_transposes() {
    auto source = smf({{0,0x90,60,103, 0x83,0x60,0x90,60,0, 0,0xff,0x2f,0}});
    File index; Sink sink; cntrl::midi::MidiFilePlayer player(source,index);
    cntrl::midi::PlayMidiFilePatch settings; settings.reverse = true; settings.transpose = 12;
    prepare(player,sink,settings); pump(player,sink,0);
    assert((sink.sent[0] == Bytes{0x90,72,103}));
    pump(player,sink,500000);
    assert((sink.sent[0] == Bytes{0x90,72,103,0x80,72,0}));
}
void test_loop_delays_rollover_and_independent_outputs() {
    auto source = smf({{0,0xc0,12, 0,0xff,0x2f,0}});
    File index; Sink sink; cntrl::midi::MidiFilePlayer player(source,index);
    cntrl::midi::PlayMidiFilePatch settings; settings.pre_delay = 1; settings.post_delay = 2; settings.loops = 2; settings.outputs = 3;
    prepare(player,sink,settings); uint32_t start = 0xffff0000;
    pump(player,sink,start); assert(sink.sent[0].empty());
    sink.blocked[1] = true;
    pump(player,sink,start + 1000000); assert(sink.sent[0].size() == 2 && sink.sent[1].empty());
    pump(player,sink,start + 1500000); assert(sink.sent[0].size() == 2);
    sink.blocked[1] = false; pump(player,sink,start + 2000000); assert(sink.sent[0] == sink.sent[1]);
    pump(player,sink,start + 4000000); pump(player,sink,start + 5000000); assert(sink.sent[0].size() == 4);
    pump(player,sink,start + 7000000); assert(player.status() == cntrl::midi::MidiFilePlayer::Status::complete);
    settings.pre_delay = settings.post_delay = settings.loops = 0;
    prepare(player,sink,settings); pump(player,sink,0); assert(player.active()); player.cancel();
}
void test_recording_filters_and_playback_round_trip() {
    File recorded; cntrl::midi::MidiFileRecorder recorder(recorded);
    assert(recorder.start({2,1},0xffffff00));
    recorder.receive(1,{9,{0x91,60,100}},0xffffff00);
    recorder.receive(0,{9,{0x90,60,100}},0xffffff00);
    recorder.receive(0,{9,{0x91,60,109}},0xffffff00);
    recorder.poll(0xffffff00);
    recorder.receive(0,{8,{0x81,60,0}},uint32_t(0xffffff00 + 500000u));
    assert(recorder.finish(uint32_t(0xffffff00 + 500000u)));
    File index; Sink sink; cntrl::midi::MidiFilePlayer player(recorded,index);
    prepare(player,sink); pump(player,sink,0); pump(player,sink,500000);
    assert((sink.sent[0] == Bytes{0x91,60,109,0x81,60,0}));
}
void test_cancel_releases_notes_and_bad_files_fail() {
    auto source = smf({{0,0x90,60,100, 0x83,0x60,0x80,60,0, 0,0xff,0x2f,0}});
    File index; Sink sink; cntrl::midi::MidiFilePlayer player(source,index);
    prepare(player,sink); pump(player,sink,0); player.cancel(); pump(player,sink,0);
    assert((sink.sent[0] == Bytes{0x90,60,100,0x80,60,0}));
    source.data.pop_back(); assert(!player.start({}));
    File failed; failed.writable = false; cntrl::midi::MidiFileRecorder recorder(failed);
    assert(!recorder.start({},0) && recorder.failed());
}
void test_recorded_sysex_plays_in_both_directions() {
    File source; cntrl::midi::MidiFileRecorder recorder(source);
    assert(recorder.start({1,1},0));
    recorder.receive(0,{4,{0xf0,1,2}},0);
    recorder.receive(0,{4,{3,4,5}},50000);
    recorder.receive(0,{6,{6,0xf7,0}},100000);
    assert(recorder.finish(100000));
    for (bool reverse : {false,true}) {
        File index; Sink sink; cntrl::midi::MidiFilePlayer player(source,index);
        cntrl::midi::PlayMidiFilePatch settings; settings.reverse = reverse;
        prepare(player,sink,settings); pump(player,sink,0); pump(player,sink,50000); pump(player,sink,100000);
        assert((sink.sent[0] == Bytes{0xf0,1,2,3,4,5,6,0xf7}));
    }
}

void test_reverse_releases_a_note_held_at_recording_stop() {
    File source; cntrl::midi::MidiFileRecorder recorder(source);
    assert(recorder.start({},0)); recorder.receive(0,{9,{0x90,60,117}},0);
    assert(recorder.finish(500000));
    File index; Sink sink; cntrl::midi::MidiFilePlayer player(source,index);
    cntrl::midi::PlayMidiFilePatch settings; settings.reverse = true;
    prepare(player,sink,settings); pump(player,sink,0);
    assert((sink.sent[0] == Bytes{0x90,60,117}));
    pump(player,sink,500000);
    assert((sink.sent[0] == Bytes{0x90,60,117,0x80,60,0}));
}

void test_playback_releases_held_sustain() {
    auto source = smf({{0,0xb0,64,127, 0,0x90,60,100, 0x83,0x60,0x80,60,0, 0,0xff,0x2f,0}});
    File index; Sink sink; cntrl::midi::MidiFilePlayer player(source,index);
    prepare(player,sink); pump(player,sink,0); pump(player,sink,500000);
    assert((sink.sent[0] == Bytes{0xb0,64,127,0x90,60,100,0x80,60,0,0xb0,64,0}));
}

void test_recorded_system_messages_and_clock_are_preserved() {
    File source; cntrl::midi::MidiFileRecorder recorder(source);
    assert(recorder.start({},0));
    recorder.receive(0,{15,{0xfa,0,0}},0);
    recorder.receive(0,{15,{0xf8,0,0}},20000);
    recorder.receive(0,{2,{0xf1,0x12,0}},30000);
    recorder.receive(0,{3,{0xf2,1,2}},40000);
    recorder.receive(0,{5,{0xf6,0,0}},50000);
    recorder.receive(0,{15,{0xff,0,0}},60000);
    recorder.receive(0,{15,{0xfc,0,0}},70000);
    assert(recorder.finish(70000));
    File index; Sink sink; cntrl::midi::MidiFilePlayer player(source,index);
    prepare(player,sink); assert(player.clock_outputs() == 1);
    pump(player,sink,0); for (unsigned t = 10000; t <= 70000; t += 10000) pump(player,sink,t);
    assert((sink.sent[0] == Bytes{0xfa,0xf8,0xf1,0x12,0xf2,1,2,0xf6,0xff,0xfc}));
}

void test_recording_serializes_independent_input_sysex_streams() {
    File source; cntrl::midi::MidiFileRecorder recorder(source);
    assert(recorder.start({},0));
    recorder.receive(0,{4,{0xf0,1,2}},0);
    recorder.receive(1,{9,{0x90,72,90}},1000);
    recorder.receive(1,{4,{0xf0,3,4}},2000);
    recorder.receive(0,{5,{0xf7,0,0}},3000);
    recorder.receive(1,{5,{0xf7,0,0}},4000);
    assert(recorder.finish(5000));
    File index; Sink sink; cntrl::midi::MidiFilePlayer player(source,index);
    prepare(player,sink); for (unsigned t = 0; t <= 5000; t += 1000) pump(player,sink,t);
    assert((sink.sent[0] == Bytes{0xf0,1,2,0xf7,0x90,72,90,0xf0,3,4,0xf7,0x80,72,0}));
    // Stopping during a message closes it before deferred input is drained.
    File partial; cntrl::midi::MidiFileRecorder incomplete(partial);
    assert(incomplete.start({},0));
    incomplete.receive(0,{4,{0xf0,1,2}},0); incomplete.receive(1,{12,{0xc0,12,0}},1000);
    assert(incomplete.finish(2000));
    File partial_index; Sink partial_sink; cntrl::midi::MidiFilePlayer partial_player(partial,partial_index);
    prepare(partial_player,partial_sink); pump(partial_player,partial_sink,0); pump(partial_player,partial_sink,2000);
    assert((partial_sink.sent[0] == Bytes{0xf0,1,2,0xf7,0xc0,12}));
}

void test_a_new_invocation_keeps_the_previous_delivery_result() {
    auto source = smf({{0,0xc0,12,0,0xff,0x2f,0}});
    File index; Sink sink; cntrl::midi::MidiFilePlayer player(source,index);
    assert(player.start({},5));
    for (unsigned i = 0; i < 50; ++i) player.poll(0,1,sink);
    assert(player.delivery(5) == cntrl::midi::Bridge::Delivery::complete);
    assert(player.start({},0));
    assert(player.delivery(5) == cntrl::midi::Bridge::Delivery::complete);
    assert(player.delivery(0) == cntrl::midi::Bridge::Delivery::pending);
    player.cancel(); assert(player.start({},1));
    assert(player.delivery(0) == cntrl::midi::Bridge::Delivery::cancelled);
    assert(player.delivery(5) == cntrl::midi::Bridge::Delivery::complete);
}

int main() {
    test_a_new_invocation_keeps_the_previous_delivery_result();
    test_recording_serializes_independent_input_sysex_streams();
    test_recorded_system_messages_and_clock_are_preserved();
    test_playback_releases_held_sustain();
    test_reverse_releases_a_note_held_at_recording_stop();
    test_recorded_sysex_plays_in_both_directions();
    test_tempo_changes_multitrack_and_speed();
    test_reverse_preserves_velocity_and_transposes();
    test_loop_delays_rollover_and_independent_outputs();
    test_recording_filters_and_playback_round_trip();
    test_cancel_releases_notes_and_bad_files_fail();
}
