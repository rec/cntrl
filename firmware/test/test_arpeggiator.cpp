#include <cntrl/midi_arpeggiator.h>
#include <cntrl/tempo.h>
#include <cassert>
#include <vector>

constexpr uint64_t beat = uint64_t(1) << 32;
struct Sink : cntrl::midi::Sink {
    std::vector<cntrl::midi::Packet> sent;
    bool blocked = false;
    unsigned blocked_outputs = 0;
    unsigned capture_contexts(unsigned inputs, unsigned channels) const override {
        return cntrl::midi::Sink::capture_contexts(inputs & 1,channels) / 16;
    }
    bool write_packet(unsigned output,const cntrl::midi::Packet &p) override { if (blocked || (blocked_outputs & (1u << output))) return false; sent.push_back(p); return true; }
};
std::array<uint8_t,50> settings(bool mono = false,unsigned kind = 0) {
    std::array<uint8_t,50> p{};
    std::memcpy(p.data(),"CARP",4); p[4] = 1; p[5] = mono; p[6] = uint8_t(kind);
    p[8] = p[9] = 255; p[10] = p[12] = 1; p[14] = 128; p[16] = 2;
    cntrl::put32(p.data() + 18,unsigned(beat / 4)); cntrl::put32(p.data() + 26,unsigned(beat / 5));
    cntrl::put32(p.data() + 34,1); cntrl::put32(p.data() + 38,1); cntrl::put32(p.data() + 42,2); cntrl::put32(p.data() + 46,42);
    return p;
}
void selectors_visit_the_requested_ranks() {
    using cntrl::midi::ArpeggiatorOrder;
    using cntrl::midi::ArpeggiatorSelection;
    ArpeggiatorOrder order;
    assert(order.reserve(9));
    auto trace = [&](unsigned n,unsigned count) { std::vector<unsigned> result; for (unsigned i = 0; i < count; ++i) result.push_back(order.next(n)); return result; };
    order.reset({6,0,2}); assert((trace(6,6) == std::vector<unsigned>{0,2,4,1,3,5}));
    order.reset({6,0,3}); assert((trace(9,9) == std::vector<unsigned>{0,3,6,1,4,7,2,5,8}));
    order.reset({3,0}); assert((trace(3,6) == std::vector<unsigned>{0,1,2,1,0,1}));
    order.reset({3,3}); assert((trace(3,8) == std::vector<unsigned>{0,1,2,2,1,0,0,1}));
    order.reset({3,1}); assert((trace(3,7) == std::vector<unsigned>{0,1,2,1,0,0,1}));
    order.reset({3,2}); assert((trace(3,7) == std::vector<unsigned>{0,1,2,2,1,0,1}));
    order.reset({1}); assert((trace(3,4) == std::vector<unsigned>{2,1,0,2}));
    order.reset({5,0,2,0,1,1,42}); assert((trace(3,4) == std::vector<unsigned>{0,1,0,1}));
    order.reset({5,4,2,0,1,1,42}); assert((trace(3,4) == std::vector<unsigned>{0,2,1,0}));
    order.reset({5,0,2,0,0,1,42}); assert((trace(3,4) == std::vector<unsigned>{0,0,0,0}));
    order.reset({8}); assert((trace(5,5) == std::vector<unsigned>{0,4,1,3,2}));
    for (unsigned kind = 0; kind < 9; ++kind) { order.reset({kind}); assert((trace(1,3) == std::vector<unsigned>{0,0,0})); assert(order.next(0) == 0); }
}
void held_notes_are_replaced_and_restored() {
    cntrl::memory_budget = 1024 * 1024;
    Sink sink; cntrl::midi::MidiArpeggiator arp;
    auto p = settings(); arp.configure(p.data()); arp.poll({0,1,true},1,0,sink); sink.note_routing = &arp;
    cntrl::midi::Packet note{9,{0x90,60,90},0};
    arp.receive(0,0,note,{0,1,true}); assert(sink.submit(0,note));
    arp.toggle({0,1,true}); arp.poll({beat / 4,1,true},1,0,sink);
    assert(sink.sent.size() == 3 && sink.sent[1].bytes[0] == 0x80 && sink.sent[2].bytes[0] == 0x90);
    sink.blocked = true; arp.stop(); arp.poll({beat / 3,1,true},1,0,sink); assert(sink.sent.size() == 3);
    sink.blocked = false; arp.poll({beat / 3,1,true},1,0,sink);
    assert(sink.sent.size() == 5 && sink.sent[3].bytes[0] == 0x80 && sink.sent[4].bytes[0] == 0x90);
    note.cin = 8; note.bytes[0] = 0x80; arp.receive(0,0,note,{beat / 2,1,true}); assert(sink.submit(0,note));
    assert(arp.size() == 0 && sink.sent.back().bytes[0] == 0x80);
}
void mono_finish_closes_the_final_note_and_plays_it() {
    Sink sink; cntrl::midi::MidiArpeggiator arp;
    auto p = settings(true,2); arp.configure(p.data()); arp.poll({0,1,true},1,0,sink);
    assert(arp.record({0,1,true})); arp.poll({beat,1,true},1,0,sink);
    arp.receive(0,0,{9,{0x90,62,99}},{beat,1,true});
    assert(arp.record({beat + beat / 2,1,true})); arp.poll({2 * beat,1,true},1,0,sink);
    assert(arp.size() == 1); arp.poll({2 * beat + beat / 4,1,true},1,0,sink);
    assert(sink.sent.size() == 1 && sink.sent[0].bytes[1] == 62);
    arp.poll({2 * beat + beat / 4 + beat / 5,1,true},1,0,sink);
    assert(sink.sent.size() == 2 && sink.sent[1].bytes[0] == 0x80 && sink.sent[1].bytes[1] == 62);
}
void mono_reorders_complete_gestures_and_pairs_repeated_pitches() {
    Sink sink; cntrl::midi::MidiArpeggiator arp;
    auto p = settings(true,2); p[7] = 8;
    arp.configure(p.data()); arp.poll({0,1,true},1,0,sink);
    arp.receive(0,0,{14,{0xe0,0,64}},{0,1,true});
    assert(arp.record({0,1,true})); arp.poll({beat,1,true},1,0,sink);
    arp.receive(0,0,{9,{0x90,60,90}},{beat,1,true});
    arp.receive(0,0,{14,{0xe0,0,96}},{beat + beat / 2,1,true});
    arp.receive(0,0,{9,{0x90,60,77}},{beat + 3 * beat / 4,1,true});
    arp.receive(0,0,{8,{0x80,60,33}},{beat + 7 * beat / 8,1,true});
    assert(arp.record({beat + 7 * beat / 8,1,true}));
    arp.poll({2 * beat,1,true},1,0,sink); assert(arp.size() == 2);
    arp.poll({2 * beat + beat / 4,1,true},1,0,sink);
    assert(sink.sent.size() == 2 && sink.sent[0].bytes[0] == 0xe0 && sink.sent[0].bytes[2] == 96 && sink.sent[1].bytes[2] == 77);
    arp.poll({2 * beat + beat / 4 + beat / 5,1,true},1,0,sink);
    arp.poll({2 * beat + beat / 2,1,true},1,0,sink);
    assert(sink.sent.size() == 5 && sink.sent[3].bytes[2] == 64 && sink.sent[4].bytes[2] == 90);
    arp.poll({2 * beat + beat / 2 + 2 * (beat / 5) / 3 + 1,1,true},1,0,sink);
    assert(sink.sent.size() == 6 && sink.sent.back().bytes[0] == 0xe0 && sink.sent.back().bytes[2] == 96);
}
void tempo_changes_preserve_the_grid_and_external_start_can_be_armed() {
    Sink sink; cntrl::midi::MidiArpeggiator arp; cntrl::ClockGenerator clock;
    auto p = settings(); arp.configure(p.data());
    clock.start(0,0,false); arp.poll(clock.beat_position(0),1,0,sink);
    arp.receive(0,0,{9,{0x90,60,90}},clock.beat_position(0)); arp.toggle(clock.beat_position(0));
    arp.poll(clock.beat_position(62500),1,0,sink); assert(sink.sent.empty());
    clock.set_tempo(1000000);
    arp.poll(clock.beat_position(187500),1,0,sink); assert(sink.sent.size() == 1);
    arp.stop(); arp.poll(clock.beat_position(187500),1,0,sink);
    sink.sent.clear(); arp.toggle({0,3,false}); arp.poll({0,3,false},1,0,sink);
    arp.poll({0,4,true},1,0,sink); assert(sink.sent.empty());
    arp.poll({beat / 4,4,true},1,0,sink); assert(sink.sent.size() == 1);
}
void one_blocked_output_does_not_stall_the_other_output() {
    Sink sink; cntrl::midi::MidiArpeggiator arp;
    auto p = settings(); p[12] = 3;
    arp.configure(p.data()); arp.poll({0,1,true},3,0,sink);
    arp.receive(0,0,{9,{0x90,60,90}},{0,1,true}); arp.toggle({0,1,true});
    arp.poll({beat / 4,1,true},3,0,sink); assert(sink.sent.size() == 2);
    sink.blocked_outputs = 2;
    arp.poll({beat / 2,1,true},3,0,sink);
    assert(sink.sent.size() == 4 && sink.sent[2].bytes[0] == 0x80 && sink.sent[3].bytes[0] == 0x90);
    sink.blocked_outputs = 0;
    arp.stop(); arp.poll({3 * beat / 4,1,true},3,0,sink);
    assert(sink.sent.size() == 6);
}
void capture_and_first_playback_use_only_admitted_storage() {
    Sink sink; cntrl::midi::MidiArpeggiator arp;
    auto p = settings(true,2); p[8] = 1; p[9] = 0; p[14] = 32;
    arp.configure(p.data()); arp.poll({0,1,true},1,0,sink);
    assert(arp.record({0,1,true}));
    const unsigned allocations = cntrl::memory_allocations;
    cntrl::memory_fail_after = 0;
    arp.poll({beat,1,true},1,0,sink);
    arp.receive(0,0,{9,{0x90,60,90}},{beat,1,true});
    arp.receive(0,0,{14,{0xe0,0,64}},{beat + beat / 4,1,true});
    arp.receive(0,0,{8,{0x80,60,33}},{beat + beat / 2,1,true});
    assert(arp.record({beat + beat / 2,1,true}));
    arp.poll({beat * 2,1,true},1,0,sink);
    arp.poll({beat * 2 + beat / 4,1,true},1,0,sink);
    assert(arp.size() == 1 && !sink.sent.empty());
    assert(arp.error() == cntrl::midi::MidiArpeggiator::Error::none);
    assert(cntrl::memory_allocations == allocations);
    cntrl::memory_fail_after = -1;
    const auto state = arp.state();
    cntrl::memory_fail_after = 0;
    assert(!arp.record({beat * 3,1,true}));
    assert(arp.size() == 1 && arp.state() == state);
    cntrl::memory_fail_after = -1;
}
void failed_configuration_keeps_the_playing_bank() {
    Sink sink; cntrl::midi::MidiArpeggiator arp;
    auto p = settings(); arp.configure(p.data()); arp.poll({0,1,true},1,0,sink);
    arp.receive(0,0,{9,{0x90,60,90}},{0,1,true}); arp.toggle({0,1,true});
    arp.poll({beat / 4,1,true},1,0,sink);
    auto larger = p; larger[14] = larger[15] = 255;
    cntrl::memory_fail_after = 0;
    arp.configure(larger.data()); arp.poll({beat / 3,1,true},1,0,sink);
    assert(arp.configured() && arp.size() == 1 && arp.state() == cntrl::midi::MidiArpeggiator::State::playing);
    assert(arp.error() == cntrl::midi::MidiArpeggiator::Error::capacity);
    cntrl::memory_fail_after = -1;
}
void configuration_is_admitted_before_publication() {
    Sink sink; cntrl::midi::MidiArpeggiator arp;
    auto p = settings();
    assert(arp.prepare_configuration(p.data(),sink));
    arp.poll({0,1,true},1,0,sink);
    assert(!arp.configured());
    arp.finish_configuration();
    arp.configure(p.data()); // PageHost republishes the same admitted settings.
    cntrl::memory_fail_after = 0;
    arp.poll({0,1,true},1,0,sink);
    assert(arp.configured());
    cntrl::memory_fail_after = -1;
    assert(arp.prepare_configuration(nullptr,sink));
    arp.discard_prepared_configuration();
    arp.poll({1,1,true},1,0,sink);
    assert(arp.configured());
}
void live_batches_are_refused_before_their_first_note_exceeds_routing_capacity() {
    Sink sink; cntrl::midi::MidiArpeggiator arp;
    auto p = settings(); p[14] = 2;
    arp.configure(p.data()); arp.poll({0,1,true},1,0,sink);
    sink.note_routing = &arp;
    const uint8_t notes[] = {0x90,60,100,0x90,64,100,0x90,67,100};
    assert(!sink.reserve_owner_stream(&arp,1,0,notes,sizeof(notes),0));
    assert(sink.sent.empty());
    // Generated patch output and an unselected live input do not enter the
    // held-note routing collection and must not consume its admission limit.
    assert(arp.prepare_batch(0,notes,sizeof(notes)));
    assert(arp.prepare_batch(0,notes,sizeof(notes),16));
    assert(arp.prepare_batch(0,notes,6,0));
}
void pending_batches_keep_their_routing_slots_until_delivery_finishes() {
    Sink sink; cntrl::midi::MidiArpeggiator arp;
    auto p = settings(); p[14] = 3;
    arp.configure(p.data()); arp.poll({0,1,true},1,0,sink);
    sink.note_routing = &arp;
    const uint8_t notes[] = {0x90,60,100,0x90,64,100};
    int identity = 0;
    assert(sink.retain_owner_state(&identity,17));
    assert(sink.reserve_owner_stream(&identity,17,0,notes,sizeof(notes),0));
    assert(sink.prepare_owner_cycle(&identity)); sink.activate_owner_state(&identity,17);
    assert(sink.prepare_stream(17,0,notes,sizeof(notes),0));
    assert(sink.submit(0,{9,{0x90,67,100},0}));
    assert(!sink.submit(0,{9,{0x90,69,100},0}));
    assert(!arp.begin_batch(18,0,notes,3,0));
    assert(sink.sent.size() == 1);
    auto replacement = settings(false,1); replacement[14] = 4;
    assert(arp.prepare_configuration(replacement.data(),sink)); arp.finish_configuration();
    arp.poll({1,1,true},1,0,sink);
    assert(arp.selection() == 0);
    cntrl::memory_fail_after = 0;
    assert(sink.submit_owned(17,0,{9,{0x90,60,100},0}));
    assert(sink.submit_owned(17,0,{9,{0x90,64,100},0}));
    assert(sink.sent.size() == 3);
    sink.finish_stream(17);
    arp.poll({2,1,true},1,0,sink);
    assert(arp.selection() == 1);
    cntrl::memory_fail_after = -1;
    sink.release_owner_state(&identity);
}
std::vector<uint8_t> choice_bank_trace(uint32_t seed) {
    Sink sink; cntrl::midi::MidiArpeggiator arp;
    auto p = settings(false,4); cntrl::put32(p.data() + 46,seed);
    arp.configure(p.data()); arp.poll({0,1,true},1,0,sink);
    for (unsigned pitch : {60u,62u,65u,67u,71u}) arp.receive(0,0,{9,{0x90,uint8_t(pitch),100}},{0,1,true});
    arp.toggle({0,1,true});
    for (unsigned i = 1; i <= 24; ++i) arp.poll({i * beat / 4,1,true},1,0,sink);
    std::vector<uint8_t> result;
    for (const auto &packet : sink.sent) if (packet.cin == 9 && packet.bytes[2]) result.push_back(packet.bytes[1]);
    return result;
}
void random_choice_repeats_a_seed_across_a_nontrivial_bank() {
    const auto first = choice_bank_trace(42), repeated = choice_bank_trace(42), other = choice_bank_trace(43);
    assert(first.size() == 24 && first == repeated && first != other);
    auto pitches = first; std::sort(pitches.begin(),pitches.end());
    assert(std::unique(pitches.begin(),pitches.end()) - pitches.begin() >= 3);
    for (uint8_t pitch : first) assert(pitch == 60 || pitch == 62 || pitch == 65 || pitch == 67 || pitch == 71);
}
void seeded_walk_preserves_stays_and_direction_probabilities() {
    cntrl::midi::ArpeggiatorOrder order;
    auto trace = [&]() { std::vector<unsigned> result; for (unsigned i = 0; i < 32; ++i) result.push_back(order.next(7)); return result; };
    order.reset({5,4,2,1,1,4,42}); auto walk = trace();
    order.reset({5,4,2,1,1,4,42}); assert(trace() == walk);
    order.reset({5,4,2,1,1,4,43}); assert(trace() != walk);
    bool stayed = false, went_up = false, went_down = false;
    for (unsigned i = 1; i < walk.size(); ++i) {
        assert(walk[i] < 7);
        if (walk[i] == walk[i - 1]) stayed = true;
        else if (walk[i] == (walk[i - 1] + 1) % 7) went_up = true;
        else { assert(walk[i] == (walk[i - 1] + 6) % 7); went_down = true; }
    }
    assert(stayed && went_up && went_down);
}
void shuffle_reshuffles_complete_cycles_and_mod_visits_uneven_groups() {
    cntrl::midi::ArpeggiatorOrder order; assert(order.reserve(7));
    auto trace = [&](unsigned size,unsigned count) { std::vector<unsigned> result; for (unsigned i = 0; i < count; ++i) result.push_back(order.next(size)); return result; };
    order.reset({7,0,2,1,1,2,42}); auto shuffled = trace(7,21);
    for (unsigned i = 0; i < 3; ++i) {
        std::vector<unsigned> cycle(shuffled.begin() + i * 7,shuffled.begin() + (i + 1) * 7);
        std::sort(cycle.begin(),cycle.end()); assert((cycle == std::vector<unsigned>{0,1,2,3,4,5,6}));
    }
    assert(!std::equal(shuffled.begin(),shuffled.begin() + 7,shuffled.begin() + 7));
    order.reset({7,0,2,1,1,2,42}); assert(trace(7,21) == shuffled);
    order.reset({7,0,2,1,1,2,43}); assert(trace(7,21) != shuffled);
    order.reset({6,0,2}); assert((trace(5,6) == std::vector<unsigned>{0,2,4,1,3,0}));
    order.reset({6,0,3}); assert((trace(8,9) == std::vector<unsigned>{0,3,6,1,4,7,2,5,0}));
    order.reset({6,0,7}); assert((trace(3,6) == std::vector<unsigned>{0,1,2,0,1,2}));
}
void poly_triad_updates_follow_the_current_held_bank() {
    Sink sink; cntrl::midi::MidiArpeggiator arp;
    auto p = settings(); arp.configure(p.data()); arp.poll({0,1,true},1,0,sink); sink.note_routing = &arp;
    auto input = [&](uint8_t pitch,bool down,uint64_t time) {
        cntrl::midi::Packet packet{uint8_t(down ? 9 : 8),{uint8_t(down ? 0x90 : 0x80),pitch,uint8_t(down ? 100 : 0)},0};
        arp.receive(0,0,packet,{time,1,true}); assert(sink.submit(0,packet));
    };
    input(60,true,0); input(64,true,0); input(67,true,0); assert(arp.size() == 3);
    sink.sent.clear(); arp.toggle({0,1,true}); arp.poll({beat / 4,1,true},1,0,sink);
    input(67,false,beat / 4 + 1); input(72,true,beat / 4 + 2); assert(arp.size() == 3);
    for (unsigned i = 2; i <= 4; ++i) arp.poll({i * beat / 4,1,true},1,0,sink);
    input(60,false,beat + 1); input(55,true,beat + 2); assert(arp.size() == 3);
    for (unsigned i = 5; i <= 7; ++i) arp.poll({i * beat / 4,1,true},1,0,sink);
    std::vector<uint8_t> attacks;
    for (const auto &packet : sink.sent) if (packet.cin == 9 && packet.bytes[2]) attacks.push_back(packet.bytes[1]);
    assert((attacks == std::vector<uint8_t>{60,64,72,60,55,64,72}));
    unsigned before = unsigned(sink.sent.size()); arp.stop(); arp.poll({2 * beat,1,true},1,0,sink);
    std::vector<uint8_t> restored;
    for (unsigned i = before; i < sink.sent.size(); ++i) if (sink.sent[i].cin == 9 && sink.sent[i].bytes[2]) restored.push_back(sink.sent[i].bytes[1]);
    std::sort(restored.begin(),restored.end()); assert((restored == std::vector<uint8_t>{55,64,72}));
}
void external_stop_restores_the_chord_but_clock_dropout_keeps_arpeggiating() {
    cntrl::ClockTempo clock; clock.configure(24,1,100000); assert(clock.transport(0,0xfa,0));
    for (unsigned i = 0; i <= 24; ++i) assert(clock.transport(0,0xf8,i * 20000));
    const auto position = clock.holdover_position(480000); assert(position.running);
    Sink sink; cntrl::midi::MidiArpeggiator arp; auto p = settings();
    arp.configure(p.data()); arp.poll(position,1,0,sink); sink.note_routing = &arp;
    for (unsigned pitch : {60u,64u,67u}) {
        cntrl::midi::Packet note{9,{0x90,uint8_t(pitch),100},0}; arp.receive(0,0,note,position); assert(sink.submit(0,note));
    }
    sink.sent.clear(); arp.toggle(position);
    for (unsigned i = 1; i <= 18; ++i) {
        uint32_t now = 480000 + i * 20000; clock.poll(now); arp.poll(clock.holdover_position(now),1,0,sink);
    }
    assert(!clock.active() && arp.state() == cntrl::midi::MidiArpeggiator::State::playing);
    std::vector<uint8_t> attacks;
    for (const auto &packet : sink.sent) if (packet.cin == 9 && packet.bytes[2]) attacks.push_back(packet.bytes[1]);
    assert((attacks == std::vector<uint8_t>{60,64,67}));
    unsigned before = unsigned(sink.sent.size()); assert(clock.transport(0,0xfc,840001));
    auto stopped = clock.holdover_position(840001); assert(!stopped.running);
    arp.poll(stopped,1,0,sink); arp.poll(stopped,1,0,sink);
    assert(arp.state() == cntrl::midi::MidiArpeggiator::State::stopped && arp.size() == 3);
    std::vector<uint8_t> restored;
    for (unsigned i = before; i < sink.sent.size(); ++i) if (sink.sent[i].cin == 9 && sink.sent[i].bytes[2]) restored.push_back(sink.sent[i].bytes[1]);
    std::sort(restored.begin(),restored.end()); assert((restored == std::vector<uint8_t>{60,64,67}));
    before = unsigned(sink.sent.size()); clock.poll(2000000); arp.poll(clock.holdover_position(2000000),1,0,sink); assert(sink.sent.size() == before);
}
int main() {
    cntrl::memory_budget = 524288;
    random_choice_repeats_a_seed_across_a_nontrivial_bank();
    seeded_walk_preserves_stays_and_direction_probabilities();
    shuffle_reshuffles_complete_cycles_and_mod_visits_uneven_groups();
    poly_triad_updates_follow_the_current_held_bank();
    external_stop_restores_the_chord_but_clock_dropout_keeps_arpeggiating();
    selectors_visit_the_requested_ranks();
    capture_and_first_playback_use_only_admitted_storage();
    failed_configuration_keeps_the_playing_bank();
    configuration_is_admitted_before_publication();
    live_batches_are_refused_before_their_first_note_exceeds_routing_capacity();
    pending_batches_keep_their_routing_slots_until_delivery_finishes();
    held_notes_are_replaced_and_restored();
    mono_finish_closes_the_final_note_and_plays_it();
    mono_reorders_complete_gestures_and_pairs_repeated_pitches();
    tempo_changes_preserve_the_grid_and_external_start_can_be_armed();
    one_blocked_output_does_not_stall_the_other_output();
}
