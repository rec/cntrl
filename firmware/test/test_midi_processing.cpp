#include <cntrl/midi_processing.h>
#include <cassert>
#include <iostream>
#include <vector>
using namespace cntrl::midi;
namespace {
struct Output : Sink {
    std::vector<Packet> packets[2];
    bool blocked[2] = {};
    bool write_packet(unsigned output, const Packet &packet) override {
        if (blocked[output]) return false;
        packets[output].push_back(packet); return true;
    }
};
const OutputConnection endpoints[] = {{"din", "DIN", 0, OutputTransport::physical, true, true}, {"trs", "TRS", 1, OutputTransport::physical, true, true}};
Outputs outputs{endpoints, 2};
void integer(std::vector<uint8_t> &data, uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) data.push_back(uint8_t(value >> (i * 8)));
}
struct Route {
    unsigned scope = 65535, input = 0, output = 2;
    std::vector<uint8_t> steps;
    unsigned count = 0;
};
std::vector<uint8_t> compiled(std::initializer_list<Route> routes) {
    std::vector<uint8_t> bytes{'C','S','T','R',0,0,0,0};
    integer(bytes, routes.size(), 2); integer(bytes, 1, 2);
    unsigned index = 0;
    for (const auto &route : routes) {
        integer(bytes, route.scope, 2); integer(bytes, 1, 2); integer(bytes, route.output, 2); integer(bytes, route.count, 2);
        bytes.push_back(uint8_t('a' + index++)); bytes.insert(bytes.end(), 63, 0);
        bytes.push_back(0); integer(bytes, route.input, 2);
        bytes.insert(bytes.end(), route.steps.begin(), route.steps.end());
    }
    uint32_t size = uint32_t(bytes.size());
    for (unsigned i = 0; i < 4; ++i) bytes[4+i] = uint8_t(size >> (i*8));
    return bytes;
}
Route transpose(unsigned scope = 65535) {
    Route route; route.scope = scope; route.count = 1; route.steps = {2, 0, 4, 0, 12, 0, 0, 0}; return route;
}
void poll(Streams &streams, Output &output, uint64_t now) { for (unsigned i = 0; i < 20; ++i) streams.service(output, now); }
void note(Streams &streams, uint8_t status, uint8_t pitch, uint8_t velocity, uint64_t now = 0) { streams.receive(0, {uint8_t(status >> 4), {status, pitch, velocity}}, now); }
void transforms_and_release() {
    auto data = compiled({transpose()}); Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    note(streams, 0x90, 60, 100, 100); poll(streams, output, 100);
    note(streams, 0x80, 60, 23, 200); poll(streams, output, 200);
    assert(output.packets[1].size() == 2);
    assert(output.packets[1][0].bytes[1] == 72 && output.packets[1][0].bytes[2] == 100);
    assert(output.packets[1][1].bytes[1] == 72 && output.packets[1][1].bytes[2] == 23);
    assert(!streams.voices());
}
void patch_scope_and_cleanup() {
    auto data = compiled({transpose(0)}); Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    note(streams, 0x90, 60, 100); poll(streams, output, 0); assert(output.packets[1].empty());
    streams.select(0); note(streams, 0x90, 61, 100); poll(streams, output, 0);
    assert(output.packets[1].size() == 1);
    streams.select(0); poll(streams, output, 0); assert(output.packets[1].size() == 1);
    streams.select(65535); poll(streams, output, 0);
    assert(output.packets[1].size() == 2 && output.packets[1][1].bytes[0] == 0x80);
}
void reconnect_and_independent_outputs() {
    Route both; both.output = 3; auto data = compiled({both}); Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    streams.output_connection(1, true, 5);
    for (unsigned i = 0; i < 300; ++i) {
        note(streams, 0x90, 60, 100); poll(streams, output, 0);
        streams.output_connection(1, false, 5);
        streams.connection(0, false, 1); streams.connection(0, true, 1);
        streams.output_connection(1, true, 5); poll(streams, output, 0);
        assert(streams.voices() == 0 && streams.pending() == 0 && streams.recovery() == 0);
    }
    assert(output.packets[0].size() == 600 && output.packets[1].size() == 600);
}
void delay_duration_and_no_stale_onsets() {
    Route route; route.count = 1; route.steps = {6, 0, 14, 0};
    integer(route.steps, 250000, 8); integer(route.steps, 1, 2);
    float feedback = 1; uint32_t bits; std::memcpy(&bits, &feedback, 4); integer(route.steps, bits, 4);
    auto data = compiled({route}); Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    note(streams, 0x90, 60, 100, 1000); note(streams, 0x80, 60, 0, 101000);
    poll(streams, output, 250999); assert(output.packets[1].empty());
    poll(streams, output, 251000); assert(output.packets[1].size() == 1);
    poll(streams, output, 351000); assert(output.packets[1].size() == 2);
    note(streams, 0x90, 61, 100, 400000); streams.connection(0, false, 1); poll(streams, output, 1000000);
    assert(output.packets[1].size() == 2);
}
void malformed_sections_are_rejected() {
    auto data = compiled({transpose()});
    for (unsigned i = 0; i < data.size(); ++i) assert(!Streams::valid(data.data(), i, outputs, 1));
    assert(Streams::valid(data.data(), unsigned(data.size()), outputs, 1));
    data.back() = 255;
    assert(!Streams::valid(data.data(), unsigned(data.size()), outputs, 1));
    data[8] = 65; assert(!Streams::valid(data.data(), unsigned(data.size()), outputs, 1));
}
void sustain_cleanup_and_shared_inputs() {
    Route a, b; b.input = 1; auto data = compiled({a, b}); Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    streams.receive(0, {11, {0xb0, 64, 127}}, 0);
    streams.receive(1, {11, {0xb0, 64, 127}}, 0); poll(streams, output, 0);
    streams.connection(0, false, 0); poll(streams, output, 0);
    assert(output.packets[1].size() == 2);
    streams.connection(1, false, 0); poll(streams, output, 0);
    assert(output.packets[1].size() == 3 && output.packets[1].back().bytes[2] == 0);
    streams.connection(0, true, 0); streams.connection(1, true, 0);
    note(streams, 0x90, 60, 100); streams.receive(1, {9, {0x90, 60, 80}}, 0); poll(streams, output, 0);
    assert(output.packets[1].size() == 4);
    streams.connection(0, false, 0); poll(streams, output, 0); assert(output.packets[1].size() == 4);
    streams.connection(1, false, 0); poll(streams, output, 0); assert(output.packets[1].size() == 5);
}
void thru_preserves_repeated_note_articulation() {
    auto data = compiled({Route{}}); Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    note(streams, 0x90, 60, 90); note(streams, 0x90, 60, 110); poll(streams, output, 0);
    note(streams, 0x80, 60, 3); note(streams, 0x80, 60, 4); poll(streams, output, 0);
    assert(output.packets[1].size() == 4);
    assert(output.packets[1][0].bytes[2] == 90 && output.packets[1][1].bytes[2] == 110);
    assert(output.packets[1][2].bytes[2] == 3 && output.packets[1][3].bytes[2] == 4);
}
struct Velocity : Streams::Processor {
    bool push(const Packet &packet, unsigned, Streams::Emit &output) override {
        Packet louder = packet; if (louder.bytes[0] >> 4 == 9) louder.bytes[2] = 120;
        return output.emit(louder);
    }
};
std::unique_ptr<Streams::Processor> velocity(const uint8_t *, unsigned size) {
    if (size) return {};
    return std::make_unique<Velocity>();
}
void registered_processor_uses_shared_delivery_and_cleanup() {
    assert(Streams::register_processor(128, 1, velocity));
    Route route; route.count = 1; route.steps = {128, 1, 0, 0}; auto data = compiled({route});
    Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    note(streams, 0x90, 60, 20); poll(streams, output, 0);
    assert(output.packets[1].size() == 1 && output.packets[1][0].bytes[2] == 120);
    streams.stop_all(); poll(streams, output, 0);
    assert(output.packets[1].back().bytes[0] == 0x80);
}
void external_patch_scope_restores_local_routes() {
    auto data = compiled({transpose(0)}); auto external = compiled({Route{}}); Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    streams.select(0); note(streams, 0x90, 60, 90); poll(streams, output, 0);
    assert(output.packets[1].back().bytes[1] == 72);
    assert(streams.select_external(external.data(), unsigned(external.size()), outputs, 1, 0));
    note(streams, 0x90, 61, 90); poll(streams, output, 0);
    assert(output.packets[1].back().bytes[0] == 0x80); // foreign scope has no routes
    streams.select(0); note(streams, 0x90, 62, 90); poll(streams, output, 0);
    assert(output.packets[1].back().bytes[1] == 74);
    assert(streams.load(nullptr, 0, outputs, 1));
    assert(!streams.configured());
}
void system_message_conversion_has_correct_payload_width() {
    Route tune; tune.count = 1;
    tune.steps = {5,0,8,0, 0,1, 0,255,0xf6,0,0,0};
    auto data = compiled({tune}); Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    streams.receive(0, {15,{0xf8,0,0}}, 0); poll(streams, output, 0);
    assert(output.packets[1].size() == 1 && output.packets[1][0].cin == 5 && output.packets[1][0].size() == 1);
    Route controller; controller.count = 1;
    std::vector<uint8_t> payload{0,4, 0,255,11,0,0,0, 1,255,1,0,0,0, 4,255,7,0,0,0, 5,5,0,0,0,0};
    controller.steps = {5,0,uint8_t(payload.size()),0}; controller.steps.insert(controller.steps.end(), payload.begin(), payload.end());
    data = compiled({controller}); assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    streams.receive(0, {3,{0xf2,123,0}}, 0); poll(streams, output, 0);
    assert(output.packets[1].back().cin == 11 && output.packets[1].back().bytes[1] == 7 && output.packets[1].back().bytes[2] == 123);
}
void merged_clocks_have_one_owner_until_disconnect() {
    Route a, b; b.input = 1; auto data = compiled({a,b}); Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    streams.receive(0, {15,{0xf8,0,0}}, 0);
    streams.receive(1, {15,{0xf8,0,0}}, 0); poll(streams, output, 0);
    assert(output.packets[1].size() == 1);
    streams.receive(1, {15,{0xf8,0,0}}, 20000); poll(streams, output, 20000);
    assert(output.packets[1].size() == 1);
    streams.connection(0, false, 0);
    streams.receive(1, {15,{0xf8,0,0}}, 40000); poll(streams, output, 40000);
    assert(output.packets[1].size() == 2);
}
}
int main() {
    transforms_and_release(); patch_scope_and_cleanup(); reconnect_and_independent_outputs();
    delay_duration_and_no_stale_onsets(); malformed_sections_are_rejected();
    sustain_cleanup_and_shared_inputs(); thru_preserves_repeated_note_articulation();
    registered_processor_uses_shared_delivery_and_cleanup(); external_patch_scope_restores_local_routes();
    system_message_conversion_has_correct_payload_width();
    merged_clocks_have_one_owner_until_disconnect();
    std::cout << "MIDI stream processing passed\n";
}
