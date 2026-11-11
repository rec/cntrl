#include <cntrl/midi_arpeggiator.h>
#include <cntrl/tempo.h>
#include <cassert>
#include <vector>

constexpr uint64_t beat = uint64_t(1) << 32;
struct Sink : cntrl::midi::Sink {
    std::vector<cntrl::midi::Packet> sent;
    bool blocked = false;
    unsigned blocked_outputs = 0;
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
    order.reset({7}); auto shuffle = trace(5,5); std::sort(shuffle.begin(),shuffle.end()); assert((shuffle == std::vector<unsigned>{0,1,2,3,4}));
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
void external_clock_dropout_continues_until_an_explicit_stop() {
    cntrl::ClockTempo clock;
    clock.configure(24,1,100000);
    clock.transport(0,0xfa,0);
    for (unsigned i = 0; i <= 24; ++i) clock.transport(0,0xf8,i * 20000);
    auto first = clock.holdover_position(480000);
    assert(first.running);
    clock.poll(980000);
    auto continuing = clock.holdover_position(980000);
    assert(continuing.running && continuing.value > first.value + beat);
    clock.transport(0,0xfc,980000);
    assert(!clock.holdover_position(1000000).running);
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
int main() {
    selectors_visit_the_requested_ranks();
    held_notes_are_replaced_and_restored();
    mono_finish_closes_the_final_note_and_plays_it();
    mono_reorders_complete_gestures_and_pairs_repeated_pitches();
    tempo_changes_preserve_the_grid_and_external_start_can_be_armed();
    external_clock_dropout_continues_until_an_explicit_stop();
    one_blocked_output_does_not_stall_the_other_output();
}
