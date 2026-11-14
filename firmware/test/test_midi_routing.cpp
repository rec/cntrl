// This translation unit deliberately needs only the basic routing API.
#include <cntrl/midi_routing.h>
#include <cassert>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<uint8_t>;
using cntrl::midi::BasicRouting;
using cntrl::midi::Packet;

struct Output : cntrl::midi::Sink {
    Output() { pickup(false); }
    Bytes bytes[4];
    bool blocked[4] = {};
    bool write_packet(unsigned output, const Packet &packet) override {
        if (blocked[output]) return false;
        bytes[output].insert(bytes[output].end(),packet.bytes,packet.bytes + packet.size());
        return true;
    }
};
constexpr cntrl::midi::OutputConnection connections[] = {
    {"keys", "Keys", 0, cntrl::midi::OutputTransport::physical, true, false},
    {"pads", "Pads", 1, cntrl::midi::OutputTransport::physical, true, false},
    {"left", "Left", 2, cntrl::midi::OutputTransport::physical, false, true},
    {"right", "Right", 3, cntrl::midi::OutputTransport::physical, false, true},
};
constexpr cntrl::midi::Outputs outputs{connections,4};

void integer(Bytes &bytes, uint64_t value, unsigned size) {
    for (unsigned i = 0; i < size; ++i) bytes.push_back(uint8_t(value >> (8 * i)));
}
struct Route {
    std::vector<unsigned> inputs{0,1};
    unsigned destinations = 12, step_count = 0;
    Bytes steps;
};
Bytes compiled(std::initializer_list<Route> routes) {
    Bytes bytes{'C','S','T','R',0,0,0,0};
    integer(bytes,routes.size(),2); integer(bytes,1,2);
    unsigned index = 0;
    for (const auto &route : routes) {
        integer(bytes,65535,2); integer(bytes,route.inputs.size(),2);
        integer(bytes,route.destinations,2); integer(bytes,route.step_count,2);
        std::string name = "route" + std::to_string(index++);
        bytes.insert(bytes.end(),name.begin(),name.end()); bytes.insert(bytes.end(),64 - name.size(),0);
        for (unsigned input : route.inputs) { bytes.push_back(0); integer(bytes,input,2); }
        bytes.insert(bytes.end(),route.steps.begin(),route.steps.end());
    }
    for (unsigned i = 0; i < 4; ++i) bytes[4 + i] = uint8_t(bytes.size() >> (8 * i));
    return bytes;
}
Route channel(unsigned destination, uint8_t channel) {
    Route route; route.inputs = {0}; route.destinations = 1u << destination; route.step_count = 3;
    // Drop channel 2, keep channel 1, then map to the configured constant.
    route.steps = {1,0,7,0, 1,1,1,2,0,0,0, 0,0,7,0, 1,1,1,1,0,0,0, 5,0,8,0, 0,1,1,255,channel,0,0,0};
    return route;
}
void load(BasicRouting &routing, const Bytes &bytes) {
    assert(routing.load(bytes.data(),unsigned(bytes.size()),outputs,1));
}
void poll(BasicRouting &routing, Output &output, uint64_t now = 0) {
    for (unsigned i = 0; i < 100; ++i) routing.service(output,now);
}

void plain_routes_preserve_multiple_sources_and_destinations() {
    BasicRouting routing; Output output; load(routing,compiled({Route{}}));
    routing.receive(0,{9,{0x90,60,100}},0);
    routing.receive(1,{11,{0xb1,74,23}},1);
    routing.receive(0,{8,{0x80,60,31}},2); poll(routing,output,2);
    const Bytes expected{0x90,60,100,0xb1,74,23,0x80,60,31};
    assert(output.bytes[2] == expected && output.bytes[3] == expected);
    assert(output.bytes[0].empty() && output.bytes[1].empty());
    assert(!routing.pending() && !routing.voices());
}

void channel_changes_release_the_original_destination_before_using_the_new_one() {
    BasicRouting routing; Output output; load(routing,compiled({channel(2,3)}));
    routing.receive(0,{9,{0x91,55,100}},0); poll(routing,output);
    routing.receive(0,{9,{0x92,56,100}},0); poll(routing,output);
    assert(output.bytes[2].empty()); // Channel 2 is dropped; channel 3 fails keep.
    routing.receive(0,{9,{0x90,60,100}},1); poll(routing,output,1);
    assert((output.bytes[2] == Bytes{0x92,60,100}));
    auto replacement = compiled({channel(3,4)});
    assert(routing.prepare_load(replacement.data(),unsigned(replacement.size()),outputs,1));
    routing.finish_load(true); poll(routing,output,2);
    assert((output.bytes[2] == Bytes{0x92,60,100,0x82,60,0}));
    routing.receive(0,{8,{0x80,60,23}},3); poll(routing,output,3);
    assert(output.bytes[3].empty()); // The old release must not acquire the new mapping.
    routing.receive(0,{9,{0x90,64,99}},4);
    routing.receive(0,{8,{0x80,64,17}},5); poll(routing,output,5);
    assert((output.bytes[3] == Bytes{0x93,64,99,0x83,64,17}));
    assert(!routing.voices());
}

void sysex_keeps_other_messages_behind_its_owner_but_allows_realtime() {
    BasicRouting routing; Output output; load(routing,compiled({Route{}}));
    routing.receive(0,{4,{0xf0,0x7d,1}},0); poll(routing,output);
    routing.receive(1,{12,{0xc1,9,0}},1);
    routing.receive(0,{15,{0xf8,0,0}},2);
    routing.receive(0,{6,{2,0xf7,0}},3); poll(routing,output,3);
    const Bytes expected{0xf0,0x7d,1,0xf8,2,0xf7,0xc1,9};
    assert(output.bytes[2] == expected && output.bytes[3] == expected);
    assert(!routing.pending());
}

void blocked_destinations_do_not_stall_healthy_fanout() {
    BasicRouting routing; Output output; output.blocked[2] = true;
    load(routing,compiled({Route{}}));
    routing.receive(0,{12,{0xc0,7,0}},0); poll(routing,output);
    routing.receive(1,{12,{0xc1,9,0}},1); poll(routing,output,1);
    const Bytes expected{0xc0,7,0xc1,9};
    assert(output.bytes[2].empty() && output.bytes[3] == expected);
    output.blocked[2] = false; poll(routing,output,2);
    assert(output.bytes[2] == expected && output.bytes[3] == expected);
    assert(!routing.pending());
}

void disconnect_cleanup_preserves_notes_from_the_other_input() {
    BasicRouting routing; Output output; load(routing,compiled({Route{}}));
    routing.receive(0,{9,{0x90,60,100}},0);
    routing.receive(1,{9,{0x90,64,100}},0);
    routing.receive(0,{11,{0xb0,64,127}},0); poll(routing,output);
    routing.connection(0,false,1); poll(routing,output,1);
    for (unsigned destination : {2u,3u}) {
        assert(output.state().value(destination,0,64) == 0);
        const Bytes &bytes = output.bytes[destination];
        unsigned released = 0;
        for (unsigned at = 0; at + 2 < bytes.size(); at += 3) {
            if (bytes[at] == 0x80 && bytes[at + 1] == 60) ++released;
            assert(bytes[at] != 0x80 || bytes[at + 1] != 64);
        }
        assert(released == 1);
    }
    routing.receive(1,{8,{0x80,64,7}},2); poll(routing,output,2);
    assert(!routing.voices() && !routing.recovery());
}

void unsupported_or_unaffordable_replacements_preserve_the_active_graph() {
    BasicRouting routing; Output output; load(routing,compiled({channel(2,3)}));
    routing.receive(0,{9,{0x90,60,100}},0); poll(routing,output);
    auto replacement = compiled({channel(3,4)});
    cntrl::memory_fail_after = 0;
    assert(!routing.prepare_load(replacement.data(),unsigned(replacement.size()),outputs,1));
    cntrl::memory_fail_after = -1;
    Route advanced; advanced.inputs = {0}; advanced.destinations = 8; advanced.step_count = 1;
    advanced.steps = {2,0,4,0,12,0,0,0}; // Transposition belongs to advanced processing.
    Route predicate; predicate.inputs = {0}; predicate.destinations = 8; predicate.step_count = 1;
    predicate.steps = {0,0,7,0, 1,0,1,9,0,0,0}; // Keep Note On, not a channel-only match.
    Route mapping; mapping.inputs = {0}; mapping.destinations = 8; mapping.step_count = 1;
    mapping.steps = {5,0,8,0, 0,1,3,255,64,0,0,0}; // Set velocity, not channel.
    for (const auto &rejected : {compiled({advanced}),compiled({predicate}),compiled({mapping})}) {
        assert(!BasicRouting::valid(rejected.data(),unsigned(rejected.size()),outputs,1));
        assert(!routing.prepare_load(rejected.data(),unsigned(rejected.size()),outputs,1));
    }
    routing.receive(0,{8,{0x80,60,12}},1); poll(routing,output,1);
    routing.receive(0,{11,{0xb0,74,55}},2); poll(routing,output,2);
    assert((output.bytes[2] == Bytes{0x92,60,100,0x82,60,12,0xb2,74,55}));
    assert(output.bytes[3].empty() && !routing.voices());
}
}

int main() {
    cntrl::memory_budget = 524288;
    plain_routes_preserve_multiple_sources_and_destinations();
    channel_changes_release_the_original_destination_before_using_the_new_one();
    sysex_keeps_other_messages_behind_its_owner_but_allows_realtime();
    blocked_destinations_do_not_stall_healthy_fanout();
    disconnect_cleanup_preserves_notes_from_the_other_input();
    unsupported_or_unaffordable_replacements_preserve_the_active_graph();
}
