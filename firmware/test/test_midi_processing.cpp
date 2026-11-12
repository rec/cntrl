#include <cntrl/midi_processing.h>
#include <cassert>
#include <iostream>
#include <vector>
using namespace cntrl::midi;
namespace {
struct Output : Sink {
    Output() { pickup(false); }
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
    unsigned input_kind = 0;
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
        bytes.push_back(uint8_t(route.input_kind)); integer(bytes, route.input, 2);
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
void replacement_graph_prepares_without_interrupting_active_notes() {
    auto original = compiled({Route{}}), replacement = compiled({transpose()});
    Streams streams; Output output;
    assert(streams.load(original.data(), unsigned(original.size()), outputs, 1));
    streams.output_connection(1,true,42);
    note(streams, 0x90, 60, 100); poll(streams, output, 0);
    assert(streams.prepare_load(replacement.data(), unsigned(replacement.size()), outputs, 1));
    poll(streams, output, 0);
    assert(output.packets[1].size() == 1 && streams.voices() == 1);
    streams.finish_load(false);
    cntrl::memory_fail_after = 0;
    assert(!streams.prepare_load(replacement.data(), unsigned(replacement.size()), outputs, 1));
    note(streams, 0x80, 60, 0); poll(streams, output, 0);
    assert(output.packets[1].size() == 2 && !streams.voices());
    cntrl::memory_fail_after = -1;
    note(streams,0x90,61,100); poll(streams,output,0);
    assert(streams.prepare_load(replacement.data(), unsigned(replacement.size()), outputs, 1));
    cntrl::memory_fail_after = 0;
    streams.finish_load(true);
    streams.output_connection(1,true,42);
    poll(streams,output,0);
    assert(output.packets[1].back().bytes[0] == 0x80 && output.packets[1].back().bytes[1] == 61);
    cntrl::memory_fail_after = -1;
    note(streams, 0x90, 60, 100); poll(streams, output, 0);
    assert(output.packets[1].back().bytes[1] == 72);
    note(streams, 0x80, 60, 0); poll(streams, output, 0);
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
void echo_decay_rounds_half_velocities_up_and_releases_each_note() {
    Route route; route.count = 1; route.steps = {6,0,14,0};
    integer(route.steps,100000,8); integer(route.steps,4,2);
    float feedback = 0.5f; uint32_t bits; std::memcpy(&bits,&feedback,4); integer(route.steps,bits,4);
    auto data = compiled({route}); Streams streams; Output output;
    assert(streams.load(data.data(),unsigned(data.size()),outputs,1));
    note(streams,0x90,60,127); note(streams,0x80,60,0,50000);
    for (unsigned i = 1; i <= 4; ++i) {
        poll(streams,output,i * 100000);
        assert(output.packets[1].size() == i * 2 - 1);
        assert(output.packets[1].back().bytes[2] == (1u << (7 - i)));
        poll(streams,output,i * 100000 + 50000);
        assert(output.packets[1].size() == i * 2);
        assert(output.packets[1].back().bytes[0] == 0x80);
    }
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
struct Burst : Streams::Processor {
    bool push(const Packet &packet, unsigned, Streams::Emit &output) override {
        if (packet.bytes[0] >> 4 != 11) return output.emit(packet);
        bool accepted = output.emit({9,{0x90,62,100}}) && output.emit({9,{0x90,63,100}});
        cntrl::memory_fail_after = 0;
        return output.emit({8,{0x80,62,0}}) && output.emit({8,{0x80,63,0}}) && accepted;
    }
};
std::unique_ptr<Streams::Processor> burst(const uint8_t *, unsigned) { return std::make_unique<Burst>(); }
void generated_releases_keep_their_traversal_credit_under_exhaustion() {
    assert(Streams::register_processor(130,1,burst));
    Route route; route.count = 1; route.steps = {130,1,0,0}; auto data = compiled({route});
    Streams streams; Output output;
    assert(streams.load(data.data(),unsigned(data.size()),outputs,1));
    note(streams,0x90,62,100); note(streams,0x90,63,100); poll(streams,output,0);
    note(streams,0x80,62,0); note(streams,0x80,63,0); poll(streams,output,0);
    streams.receive(0,{11,{0xb0,7,1}},1); poll(streams,output,1);
    cntrl::memory_fail_after = -1;
    assert(!streams.voices() && !streams.pending() && !streams.diagnostics().failed);
    assert(output.packets[1].size() == 8 && output.packets[1].back().bytes[0] == 0x80);
}
struct TriggeredNote : Streams::Processor {
    unsigned controller;
    explicit TriggeredNote(unsigned control = 7) : controller(control) {}
    bool push(const Packet &packet, unsigned, Streams::Emit &output) override {
        if (packet.bytes[0] >> 4 == 11 && packet.bytes[1] == controller) {
            const bool on = packet.bytes[2] != 0;
            output.emit({uint8_t(on ? 9 : 8), {uint8_t(on ? 0x90 : 0x80), 60, uint8_t(on ? 100 : 0)}});
        }
        return true;
    }
};
std::unique_ptr<Streams::Processor> triggered_note(const uint8_t *bytes, unsigned size) {
    return size <= 1 ? std::make_unique<TriggeredNote>(size ? bytes[0] : 7) : nullptr;
}
void generated_refusals_keep_fifo_pairing_at_the_note_limit() {
    assert(Streams::register_processor(131, 1, triggered_note));
    Route route; route.count = 1; route.steps = {131, 1, 0, 0}; auto data = compiled({route});
    Streams::Limits limits; limits.notes = 1;
    Streams streams(limits); Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    auto trigger = [&](bool on) {
        streams.receive(0, {11, {0xb0, 7, uint8_t(on ? 127 : 0)}}, 0);
        poll(streams, output, 0);
    };
    trigger(true); // A is admitted.
    trigger(true); // B is refused at the note limit.
    assert(output.packets[1].size() == 1);
    trigger(false); // Off A releases its accepted voice first.
    assert(output.packets[1].size() == 2 && output.packets[1].back().bytes[0] == 0x80);
    trigger(true); // C remains refused until B's Off arrives.
    assert(output.packets[1].size() == 2);
    trigger(false); trigger(false); // Drain Off B and Off C.
    assert(output.packets[1].size() == 2);
    trigger(true); // D can now be admitted.
    assert(output.packets[1].size() == 3 && output.packets[1].back().bytes[0] == 0x90);
    trigger(false);
    assert(output.packets[1].size() == 4 && !streams.voices() && !streams.pending());
}
void generated_notes_pair_with_the_processor_that_emitted_them() {
    Route a, b; a.output = 1; a.count = b.count = 1;
    a.steps = {131, 1, 1, 0, 7}; b.steps = {131, 1, 1, 0, 8};
    auto data = compiled({a, b}); Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    auto trigger = [&](unsigned controller, bool on) {
        streams.receive(0, {11, {0xb0, uint8_t(controller), uint8_t(on ? 127 : 0)}}, 0);
        poll(streams, output, 0);
    };
    trigger(7, true); trigger(8, true);
    assert(output.packets[0].size() == 1 && output.packets[1].size() == 1);
    trigger(8, false);
    assert(output.packets[0].size() == 1);
    assert(output.packets[1].size() == 2 && output.packets[1].back().bytes[0] == 0x80);
    trigger(7, false);
    assert(output.packets[0].size() == 2 && !streams.voices());
}
void generated_refusals_preserve_the_original_input_cable() {
    Route route; route.count = 1; route.steps = {131, 1, 0, 0}; auto data = compiled({route});
    Streams::Limits limits; limits.notes = 1;
    Streams streams(limits); Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    auto trigger = [&](unsigned cable, bool on) {
        streams.receive(0, {11, {0xb0, 7, uint8_t(on ? 127 : 0)}, uint16_t(cable)}, 0);
        poll(streams, output, 0);
    };
    trigger(0, true); trigger(1, true); // Only cable 1's new onset is refused.
    trigger(0, false);
    assert(output.packets[1].size() == 2);
    trigger(0, true); // Cable 1's missing Off must not close cable 0.
    assert(output.packets[1].size() == 3 && output.packets[1].back().bytes[0] == 0x90);
    trigger(1, false); trigger(0, false);
    assert(output.packets[1].size() == 4 && !streams.voices());
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
void blocked_fanout_does_not_interrupt_healthy_notes_or_releases() {
    Route route; route.output = 3; auto data = compiled({route});
    Streams streams({24,12,32,8}); Output output; output.blocked[0] = true;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    note(streams, 0x90, 60, 100); poll(streams, output, 0);
    for (unsigned i = 0; i < 1000; ++i) {
        streams.receive(0, {11,{0xb0,7,uint8_t(i % 128)}}, i);
        note(streams, 0x90, 61, 90, i); note(streams, 0x80, 61, 0, i);
        poll(streams, output, i);
        assert(streams.pending() <= 12);
    }
    note(streams, 0x80, 60, 23, 1001); poll(streams, output, 1001);
    assert(output.packets[0].empty());
    assert(output.packets[1].size() == 3002);
    assert(output.packets[1].back().bytes[1] == 60 && output.packets[1].back().bytes[2] == 23);
    assert(streams.diagnostics().overloaded > 0 && !streams.voices());
}
void overloaded_derived_branch_keeps_shared_dry_route_running() {
    Route dry; dry.output = 1;
    Route echo; echo.input_kind = 1; echo.input = 0; echo.count = 2;
    echo.steps = {6,0,14,0}; integer(echo.steps, 100000, 8); integer(echo.steps, 8, 2);
    float gain = 1; uint32_t bits; std::memcpy(&bits, &gain, 4); integer(echo.steps, bits, 4);
    echo.steps.insert(echo.steps.end(), {7,0,8,0}); integer(echo.steps, 1000000, 8);
    auto data = compiled({dry, echo}); Streams streams({32,16,32,2}); Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    streams.receive(0, {11,{0xb0,7,100}}, 0); poll(streams, output, 0);
    assert(streams.diagnostics().overloaded == 1);
    streams.receive(0, {11,{0xb0,7,101}}, 1); poll(streams, output, 1);
    assert(output.packets[0].size() == 2 && output.packets[0].back().bytes[2] == 101);
    assert(output.packets[1].empty());
}
void paired_pressure_and_release_fields_follow_written_transforms() {
    Route route = transpose();
    auto scale = [&](unsigned field, unsigned match_field, unsigned match_value) {
        std::vector<uint8_t> payload{1,uint8_t(match_field),1}; integer(payload, match_value, 4);
        payload.push_back(uint8_t(field)); integer(payload, 0, 4); integer(payload, 127, 4);
        integer(payload, 0, 4); integer(payload, 63, 4);
        route.steps.insert(route.steps.end(), {4,0,uint8_t(payload.size()),0});
        route.steps.insert(route.steps.end(), payload.begin(), payload.end()); ++route.count;
    };
    scale(7,2,72); scale(3,0,8);
    route.steps.insert(route.steps.end(), {6,0,14,0}); integer(route.steps, 1000, 8); integer(route.steps, 1, 2);
    float gain = 1; uint32_t bits; std::memcpy(&bits, &gain, 4); integer(route.steps, bits, 4); ++route.count;
    auto data = compiled({route}); Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    note(streams, 0x90, 60, 90, 0); poll(streams, output, 1000);
    streams.receive(0, {10,{0xa0,60,127}}, 2000); poll(streams, output, 2999);
    assert(output.packets[1].size() == 1);
    poll(streams, output, 3000);
    assert(output.packets[1].size() == 2 && output.packets[1].back().bytes[1] == 72 && output.packets[1].back().bytes[2] == 63);
    note(streams, 0x80, 60, 100, 4000); poll(streams, output, 5000);
    assert(output.packets[1].size() == 3 && output.packets[1].back().bytes[1] == 72 && output.packets[1].back().bytes[2] == 50);
}
void controller_only_map_preserves_unrelated_notes() {
    Route route; route.count = 1; route.steps = {5,0,8,0, 0,1, 4,255,11,0,0,0};
    auto data = compiled({route}); Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    note(streams, 0x90, 60, 90); streams.receive(0, {11,{0xb0,7,100}}, 0); note(streams, 0x80, 60, 5); poll(streams, output, 0);
    assert(output.packets[1].size() == 3);
    assert(output.packets[1][0].bytes[0] == 0x90 && output.packets[1][1].bytes[1] == 11 && output.packets[1][2].bytes[0] == 0x80);
}
unsigned plugin_onsets = 0, plugin_releases = 0;
struct Stateful : Streams::Processor {
    bool push(const Packet &packet, unsigned, Streams::Emit &output) override {
        if (packet.bytes[0] >> 4 == 9) ++plugin_onsets;
        if (packet.bytes[0] >> 4 == 8) ++plugin_releases;
        return output.emit(packet);
    }
};
std::unique_ptr<Streams::Processor> stateful(const uint8_t *, unsigned size) {
    if (size) return {};
    return std::make_unique<Stateful>();
}
void stateful_processor_observes_each_input_release_once_across_fanout() {
    assert(Streams::register_processor(129, 1, stateful));
    Route route; route.output = 3; route.count = 1; route.steps = {129,1,0,0};
    auto data = compiled({route}); Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    note(streams, 0x90, 60, 90); poll(streams, output, 0);
    note(streams, 0x80, 60, 5); poll(streams, output, 0);
    assert(plugin_onsets == 1 && plugin_releases == 1);
    assert(output.packets[0].size() == 2 && output.packets[1].size() == 2);
    note(streams, 0x90, 61, 90); poll(streams, output, 0);
    streams.connection(0, false, 1); poll(streams, output, 0);
    assert(plugin_onsets == 2 && plugin_releases == 2);
    assert(output.packets[0].size() == 4 && output.packets[1].size() == 4);
}
void unused_ports_do_not_reduce_active_output_capacity() {
    auto data = compiled({Route{}}); Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    for (unsigned i = 0; i < 150; ++i) {
        note(streams, 0x90, 60, 100, i); poll(streams, output, i);
    }
    assert(streams.voices() == 150 && !streams.diagnostics().overloaded);
    for (unsigned i = 0; i < 150; ++i) {
        note(streams, 0x80, 60, 0, 150 + i); poll(streams, output, 150 + i);
    }
    assert(!streams.voices() && output.packets[1].size() == 300);
}
void pickup_uses_mapped_destination_and_separate_live_sources() {
    Route route; route.count = 1; route.steps = {5,0,8,0, 0,1, 4,255,74,0,0,0};
    auto data = compiled({route}); Streams streams; Output output;
    output.pickup(true, 0);
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    output.submit(1, {11,{0xb0,74,64}});
    streams.receive(0, {11,{0xb0,7,20},0}, 0); poll(streams, output, 0);
    assert(output.state().value(1, 0, 74) == 64 && !streams.diagnostics().transmitted);
    streams.receive(0, {11,{0xb0,7,70},0}, 1); poll(streams, output, 1);
    assert(output.state().value(1, 0, 74) == 70 && streams.diagnostics().transmitted == 1);
    streams.receive(0, {11,{0xb0,7,40},1}, 2); poll(streams, output, 2);
    assert(output.state().value(1, 0, 74) == 70);
    streams.receive(0, {11,{0xb0,7,80},1}, 3); poll(streams, output, 3);
    streams.receive(0, {11,{0xb0,7,60},0}, 4); poll(streams, output, 4);
    assert(output.state().value(1, 0, 74) == 80 && streams.diagnostics().transmitted == 2);
}
}
int main() {
    cntrl::memory_budget = 1024 * 1024;
    echo_decay_rounds_half_velocities_up_and_releases_each_note();
    replacement_graph_prepares_without_interrupting_active_notes();
    transforms_and_release(); patch_scope_and_cleanup(); reconnect_and_independent_outputs();
    delay_duration_and_no_stale_onsets(); malformed_sections_are_rejected();
    sustain_cleanup_and_shared_inputs(); thru_preserves_repeated_note_articulation();
    registered_processor_uses_shared_delivery_and_cleanup(); external_patch_scope_restores_local_routes();
    generated_releases_keep_their_traversal_credit_under_exhaustion();
    generated_refusals_keep_fifo_pairing_at_the_note_limit();
    generated_notes_pair_with_the_processor_that_emitted_them();
    generated_refusals_preserve_the_original_input_cable();
    system_message_conversion_has_correct_payload_width();
    merged_clocks_have_one_owner_until_disconnect();
    blocked_fanout_does_not_interrupt_healthy_notes_or_releases();
    overloaded_derived_branch_keeps_shared_dry_route_running();
    paired_pressure_and_release_fields_follow_written_transforms(); controller_only_map_preserves_unrelated_notes();
    stateful_processor_observes_each_input_release_once_across_fanout();
    unused_ports_do_not_reduce_active_output_capacity();
    pickup_uses_mapped_destination_and_separate_live_sources();
    std::cout << "MIDI stream processing passed\n";
}
