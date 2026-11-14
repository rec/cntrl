#include <cntrl/midi_processing.h>
#include <cassert>
#include <vector>
using namespace cntrl::midi;
namespace {
const OutputConnection endpoints[] = {{"din", "DIN", 0, OutputTransport::physical, true, true},
    {"trs", "TRS", 1, OutputTransport::physical, true, true}};
Outputs outputs{endpoints, 2};
void integer(std::vector<uint8_t> &data, uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) data.push_back(uint8_t(value >> (i * 8)));
}
std::vector<uint8_t> compiled(uint64_t window) {
    std::vector<uint8_t> bytes{'C','S','T','R',0,0,0,0};
    integer(bytes, 1, 2); integer(bytes, 1, 2);
    integer(bytes, 65535, 2); integer(bytes, 1, 2); integer(bytes, 2, 2); integer(bytes, 1, 2);
    bytes.push_back('a'); bytes.insert(bytes.end(), 63, 0);
    bytes.push_back(0); integer(bytes, 0, 2);
    bytes.insert(bytes.end(), {7,0,8,0}); integer(bytes, window, 8);
    for (unsigned i = 0; i < 4; ++i) bytes[4 + i] = uint8_t(bytes.size() >> (i * 8));
    return bytes;
}
struct Output : Sink {
    std::vector<Packet> packets;
    bool write_packet(unsigned output, const Packet &packet) override {
        assert(output == 1); packets.push_back(packet); return true;
    }
};
void poll(Streams &streams, Output &output, uint64_t now) {
    for (unsigned i = 0; i < 20; ++i) streams.service(output, now);
}
void note(Streams &streams, uint8_t status, uint8_t pitch, uint8_t velocity, uint64_t now) {
    streams.receive(0, {uint8_t(status >> 4), {status,pitch,velocity}}, now);
}
void held_notes_split_and_use_original_velocity() {
    auto data = compiled(100000); Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    note(streams, 0x90, 60, 93, 20000);
    poll(streams, output, 100000);
    assert(output.packets.size() == 1 && output.packets[0].bytes[2] == 93);
    note(streams, 0x80, 60, 0, 150000);
    poll(streams, output, 179999); assert(output.packets.size() == 1);
    poll(streams, output, 180000); assert(output.packets.size() == 2 && output.packets[1].bytes[0] == 0x80);
    poll(streams, output, 200000); assert(output.packets.size() == 2);
    poll(streams, output, 250000); assert(output.packets.size() == 3 && output.packets[2].bytes[2] == 93);
    poll(streams, output, 300000); assert(output.packets.size() == 4 && !streams.voices());
}
void controls_are_emitted_once_at_mirrored_time() {
    auto data = compiled(100000); Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    streams.receive(0, {11,{0xb0,7,100}}, 20000);
    poll(streams, output, 100000); assert(output.packets.empty());
    poll(streams, output, 180000); assert(output.packets.size() == 1);
    poll(streams, output, 300000); assert(output.packets.size() == 1);
}
void pedals_split_at_window_boundaries_and_finish_released() {
    for (uint8_t control = 64; control <= 69; ++control) {
        auto data = compiled(100000); Streams streams; Output output;
        assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
        note(streams, 0xb0, control, 96, 20000);
        poll(streams, output, 100000);
        assert(output.packets.size() == 1 && output.packets[0].bytes[2] == 96);
        note(streams, 0xb0, control, 0, 150000);
        poll(streams, output, 179999); assert(output.packets.size() == 1);
        poll(streams, output, 180000); assert(output.packets.size() == 2 && !output.packets.back().bytes[2]);
        poll(streams, output, 200000); assert(output.packets.size() == 2);
        poll(streams, output, 250000); assert(output.packets.size() == 3 && output.packets.back().bytes[2] == 96);
        poll(streams, output, 300000); assert(output.packets.size() == 4 && !output.packets.back().bytes[2]);
        poll(streams, output, 400000); assert(output.packets.size() == 4);
    }
}
void finite_reverse_and_disconnect_release_pedals() {
    for (uint8_t control = 64; control <= 69; ++control) {
        auto data = compiled(0); Streams streams; Output output;
        assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
        note(streams, 0xb0, control, 96, 20000);
        note(streams, 0xb0, control, 0, 80000);
        poll(streams, output, 100000); assert(output.packets.empty());
        streams.finish_reverse(0, 100000);
        poll(streams, output, 119999); assert(output.packets.empty());
        poll(streams, output, 120000); assert(output.packets.size() == 1 && output.packets[0].bytes[2] == 96);
        streams.connection(0, false, 2);
        poll(streams, output, 130000);
        assert(output.packets.size() == 2 && !output.packets.back().bytes[2]);
        poll(streams, output, 200000); assert(output.packets.size() == 2);
    }
}
void one_usb_cables_release_does_not_discard_another_cables_capture() {
    auto data = compiled(100000); Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    streams.receive(0, {11,{0xb0,64,96},0}, 10000);
    streams.receive(0, {11,{0xb0,64,80},1}, 20000);
    streams.receive(0, {11,{0xb0,64,0},0}, 30000);
    poll(streams, output, 100000);
    assert(output.packets.size() == 1 && output.packets[0].bytes[2] == 80);
    poll(streams, output, 190000);
    unsigned before = unsigned(output.packets.size());
    poll(streams, output, 200000);
    assert(output.packets.size() == before + 1 && output.packets.back().bytes[2] == 80);
    streams.connection(0, false, 2); poll(streams, output, 210000);
    assert(!output.packets.back().bytes[2]);
}
void sysex_crossing_boundary_stays_complete_and_ordered() {
    auto data = compiled(100000); Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    streams.receive(0, {4,{0xf0,0x7d,1}}, 80000);
    poll(streams, output, 100000); assert(output.packets.empty());
    streams.receive(0, {7,{2,3,0xf7}}, 120000);
    poll(streams, output, 200000); assert(output.packets.empty());
    poll(streams, output, 280000); assert(output.packets.size() == 2);
    assert(output.packets[0].bytes[0] == 0xf0 && output.packets[1].bytes[0] == 2);
}
void finite_reverse_waits_for_clip_end() {
    auto data = compiled(0); Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    note(streams, 0x90, 60, 80, 10000); note(streams, 0x80, 60, 0, 60000);
    poll(streams, output, 100000); assert(output.packets.empty());
    streams.finish_reverse(0, 100000);
    poll(streams, output, 140000); assert(output.packets.size() == 1 && output.packets[0].bytes[2] == 80);
    poll(streams, output, 190000); assert(output.packets.size() == 2 && !streams.voices());
}
void disconnect_and_late_service_discard_captured_traffic() {
    auto data = compiled(100000); Streams streams; Output output;
    assert(streams.load(data.data(), unsigned(data.size()), outputs, 1));
    note(streams, 0x90, 60, 90, 10000);
    streams.connection(0, false, 1); streams.connection(0, true, 1);
    poll(streams, output, 100000); assert(output.packets.empty());
    note(streams, 0x90, 61, 90, 110000);
    poll(streams, output, 400000);
    assert(output.packets.empty() && streams.diagnostics().overloaded == 1);
    assert(!streams.voices() && !streams.pending());
}
}
int main() {
    cntrl::memory_budget = 524288;
    held_notes_split_and_use_original_velocity(); controls_are_emitted_once_at_mirrored_time();
    pedals_split_at_window_boundaries_and_finish_released();
    finite_reverse_and_disconnect_release_pedals();
    one_usb_cables_release_does_not_discard_another_cables_capture();
    sysex_crossing_boundary_stays_complete_and_ordered(); finite_reverse_waits_for_clip_end();
    disconnect_and_late_service_discard_captured_traffic();
}
