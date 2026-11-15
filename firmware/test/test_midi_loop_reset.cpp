#include <cntrl/midi_bridge.h>
#include <array>
#include <cassert>
#include <vector>

using namespace cntrl::midi;
namespace {
constexpr uint8_t settings[] = {'C','L','R','S',1,2,0,
    0,Parameter::cc7,0,10,0,64,0,
    0,Parameter::nrpn14,0,42,0,0,32};
struct Sender {
    unsigned blocked = 0;
    std::vector<std::array<unsigned,4>> sent;
    bool operator()(unsigned output,ParameterTransaction &transaction) {
        if (blocked & (1u << output)) return false;
        uint8_t bytes[3] = {};
        transaction.parameter.message(transaction.position,bytes);
        sent.push_back({output,bytes[0],bytes[1],bytes[2]});
        ++transaction.position;
        return true;
    }
};
struct Output : Sink {
    std::vector<Packet> sent;
    bool write_packet(unsigned,const Packet &packet) override { sent.push_back(packet); return true; }
};
void arpeggiator_groups_do_not_become_loop_reset_targets() {
    cntrl::memory_budget = 65536;
    const uint8_t policy[] = {'C','L','R','S',1,1,0,0,Parameter::cc14,0,1,0,0,0};
    Output output;
    output.configure_loop_controls(policy);
    assert(output.reserve_playback_state(2,1,1,1));
    Parameter parameter{Parameter::cc14,0,1,128};
    assert(output.submit_group(0,{11,{0xb0,1,1}},parameter,99,true,false,1));
    assert(output.submit_group(0,{11,{0xb0,33,0}},parameter,99,false,true,1));
    assert(output.parameter_value(0,parameter) == 128);
    output.begin_loop_control_reset();
    assert(!output.loop_control_reset_pending() && output.sent.size() == 2);
}
void accept(LoopControlReset &reset,unsigned output,Packet packet,const Parameter *logical = nullptr) {
    assert(reset.prepare(output,packet,logical));
    reset.accepted(output,packet,logical);
}
void untouched_and_disabled_controls_need_no_storage() {
    LoopControlReset reset;
    cntrl::memory_budget = 0;
    accept(reset,0,{14,{0xe0,0,0}});
    reset.begin(); assert(!reset.pending());
    reset.configure(settings);
    accept(reset,0,{11,{0xb0,7,100}});
    reset.begin(); assert(!reset.pending() && !cntrl::memory_used);
    assert(!reset.prepare(0,{14,{0xe0,0,0}}));
    reset.begin(); assert(!reset.pending());
}
void stop_resets_only_accepted_touched_controls() {
    cntrl::memory_budget = 4096;
    LoopControlReset reset; reset.configure(settings);
    // Preparation without acceptance must not mark a control as touched.
    assert(reset.prepare(0,{13,{0xd0,70,0}}));
    reset.begin(); assert(!reset.pending());
    accept(reset,0,{11,{0xb0,10,12}});
    accept(reset,0,{11,{0xb0,10,15}});
    accept(reset,0,{14,{0xe0,1,2}});
    accept(reset,0,{10,{0xa0,60,70}});
    Parameter parameter{Parameter::nrpn14,0,42,1234};
    accept(reset,0,{11,{0xb0,99,0}},&parameter);
    accept(reset,0,{11,{0xb0,98,42}},&parameter);
    accept(reset,0,{11,{0xb0,6,9}},&parameter);
    reset.begin(); Sender sender;
    for (unsigned i = 0; i < 32 && reset.pending(); ++i)
        reset.poll(1,[&](unsigned output,ParameterTransaction &t) { return sender(output,t); });
    assert(!reset.pending() && sender.sent.size() == 9);
    assert((sender.sent[0] == std::array<unsigned,4>{0,0xb0,10,64}));
    assert((sender.sent[1] == std::array<unsigned,4>{0,0xe0,0,64}));
    assert((sender.sent[2] == std::array<unsigned,4>{0,0xa0,60,0}));
    assert((sender.sent[3] == std::array<unsigned,4>{0,0xb0,99,0}));
    assert((sender.sent[5] == std::array<unsigned,4>{0,0xb0,6,64}));
    reset.begin(); assert(!reset.pending());
}
void blocked_and_retired_outputs_do_not_reset_a_replacement() {
    cntrl::memory_budget = 4096;
    LoopControlReset reset; reset.configure(settings);
    accept(reset,0,{14,{0xe0,1,2}});
    accept(reset,1,{13,{0xd0,70,0}});
    reset.begin(); Sender sender; sender.blocked = 1;
    assert(!reset.poll(3,[&](unsigned output,ParameterTransaction &t) { return sender(output,t); }));
    assert(sender.sent.size() == 1 && sender.sent[0][0] == 1 && reset.pending());
    reset.forget(0); assert(!reset.pending());
    sender.blocked = 0;
    reset.poll(3,[&](unsigned output,ParameterTransaction &t) { return sender(output,t); });
    assert(sender.sent.size() == 1);
}
void neutral_values_match_resolution_without_touching_unsent_halves() {
    constexpr uint8_t halves[] = {'C','L','R','S',1,2,0,
        0,Parameter::cc7,0,1,0,64,0,
        0,Parameter::cc7,0,33,0,7,0};
    cntrl::memory_budget = 4096;
    LoopControlReset reset; reset.configure(halves);
    Parameter fine{Parameter::cc14,0,1,1234};
    accept(reset,0,{11,{0xb0,1,9}},&fine);
    reset.begin(); Sender sender;
    assert(reset.poll(1,[&](unsigned output,ParameterTransaction &t) { return sender(output,t); }));
    assert(sender.sent.size() == 1 && sender.sent[0][2] == 1);
    accept(reset,0,{11,{0xb0,1,9}},&fine);
    accept(reset,0,{11,{0xb0,33,82}},&fine);
    reset.begin();
    for (unsigned i = 0; i < 2; ++i)
        reset.poll(1,[&](unsigned output,ParameterTransaction &t) { return sender(output,t); });
    assert(sender.sent.size() == 3 && sender.sent[2][2] == 33 && sender.sent[2][3] == 7);
    reset.configure(settings);
    Parameter coarse{Parameter::nrpn7,0,42,9};
    accept(reset,0,{11,{0xb0,6,9}},&coarse);
    reset.begin();
    for (unsigned i = 0; i < 6; ++i)
        reset.poll(1,[&](unsigned output,ParameterTransaction &t) { return sender(output,t); });
    assert(sender.sent.size() == 9 && sender.sent[5][2] == 6 && sender.sent[5][3] == 64);
}
}
int main() {
    untouched_and_disabled_controls_need_no_storage();
    stop_resets_only_accepted_touched_controls();
    blocked_and_retired_outputs_do_not_reset_a_replacement();
    neutral_values_match_resolution_without_touching_unsent_halves();
    arpeggiator_groups_do_not_become_loop_reset_targets();
    assert(cntrl::memory_used == 0);
}
