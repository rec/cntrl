#include "cntrl/midi_stream_timeline.h"
#include <cassert>
#include <vector>

using namespace cntrl::midi;

void reverses_note_intervals_with_original_velocity() {
    std::vector<TimelinePacket> events{
        {20, {9, {0x90, 60, 99}}, 2, 7},
        {80, {8, {0x80, 60, 0}}, 2, 8},
    };
    assert(reverse_sequence(events, 100));
    assert(events.size() == 2 && events[0].time == 20 && events[1].time == 80);
    assert(events[0].packet.bytes[0] == 0x90 && events[0].packet.bytes[2] == 99);
    assert(events[1].packet.bytes[0] == 0x80 && events[0].token == events[1].token);
}
void pairs_repeated_notes_fifo_and_orders_zero_duration_notes() {
    std::vector<TimelinePacket> events{
        {5, {9, {0x90, 60, 20}}, 0, 1},
        {10, {9, {0x90, 60, 70}}, 0, 2},
        {20, {8, {0x80, 60, 0}}, 0, 1},
        {30, {9, {0x90, 60, 0}}, 0, 2},
        {30, {9, {0x90, 61, 90}}, 0, 3},
        {30, {8, {0x80, 61, 0}}, 0, 3},
    };
    assert(reverse_sequence(events, 50));
    assert(events[0].packet.bytes[0] == 0x90 && events[0].packet.bytes[1] == 61);
    assert(events[1].packet.bytes[0] == 0x80 && events[1].packet.bytes[1] == 61);
    assert(events[2].packet.bytes[2] == 70);
    assert(events[3].packet.bytes[2] == 20);
}
void sysex_frames_keep_byte_order_and_completion_placement() {
    std::vector<TimelinePacket> events{
        {10, {4, {0xf0, 0x7d, 1}}, 0},
        {20, {7, {2, 3, 0xf7}}, 0},
        {30, {15, {0xf8, 0, 0}}, 1},
    };
    assert(reverse_sequence(events, 40));
    assert(events[0].time == 10 && events[0].packet.bytes[0] == 0xf8);
    assert(events[1].time == 20 && events[1].packet.bytes[0] == 0xf0);
    assert(events[2].time == 20 && events[2].packet.bytes[0] == 2);
    std::vector<TimelinePacket> incomplete{{10, {4, {0xf0, 0x7d, 1}}, 0}};
    assert(!reverse_sequence(incomplete, 20));
}
void simultaneous_controls_keep_their_order() {
    std::vector<TimelinePacket> events{
        {10, {11, {0xb0, 0, 3}}, 0},
        {10, {11, {0xb0, 32, 4}}, 0},
        {10, {12, {0xc0, 5, 0}}, 0},
    };
    assert(reverse_sequence(events, 20));
    assert(events[0].packet.bytes[1] == 0 && events[1].packet.bytes[1] == 32);
    assert(events[2].packet.bytes[0] == 0xc0);
}
void pedal_reversal_restores_previous_values_and_closes_boundary_holds() {
    for (uint8_t control = 64; control <= 69; ++control) {
        std::vector<TimelinePacket> events{
            {20, {11, {0xb0, control, 80}}, 0},
            {30, {11, {0xb0, control, 110}}, 0},
            {80, {11, {0xb0, control, 0}}, 0},
        };
        assert(reverse_sequence(events, 100));
        assert(events.size() == 3);
        assert(events[0].time == 20 && events[0].packet.bytes[2] == 110);
        assert(events[1].time == 70 && events[1].packet.bytes[2] == 80);
        assert(events[2].time == 80 && events[2].packet.bytes[2] == 0);
        events = {{20, {11, {0xb0, control, 96}}, 0}};
        assert(!reverse_sequence(events, 100, 1));
        assert(events.size() == 1 && events[0].time == 20 && events[0].packet.bytes[2] == 96);
        assert(reverse_sequence(events, 100, 2));
        assert(events.size() == 2 && events[0].time == 0 && events[0].packet.bytes[2] == 96);
        assert(events[1].time == 80 && events[1].packet.bytes[2] == 0);
        events = {{20, {11, {0xb0, control, 96}}, 0}, {20, {11, {0xb0, control, 0}}, 0}};
        assert(reverse_sequence(events, 100));
        assert(events.size() == 2 && events[0].time == 80 && events[1].time == 80);
        assert(events[0].packet.bytes[2] == 96 && events[1].packet.bytes[2] == 0);
    }
}
void pedal_state_is_independent_for_each_source_channel_and_controller() {
    std::vector<TimelinePacket> events{
        {10, {11, {0xb0, 64, 70}}, 0},
        {20, {11, {0xb1, 64, 80}}, 0},
        {30, {11, {0xb0, 65, 90}}, 0},
        {40, {11, {0xb0, 64, 100}}, 1},
        {50, {11, {0xb0, 64, 0}}, 0},
        {60, {11, {0xb1, 64, 0}}, 0},
        {70, {11, {0xb0, 65, 0}}, 0},
        {80, {11, {0xb0, 64, 0}}, 1},
    };
    assert(reverse_sequence(events, 100));
    assert(events.size() == 8);
    assert(events[0].source == 1 && events[0].packet.bytes[2] == 100);
    assert(events[1].packet.bytes[1] == 65 && events[1].packet.bytes[2] == 90);
    assert(events[2].packet.bytes[0] == 0xb1 && events[2].packet.bytes[2] == 80);
    assert(events[3].source == 0 && events[3].packet.bytes[0] == 0xb0 && events[3].packet.bytes[2] == 70);
    for (unsigned i = 4; i < 8; ++i) assert(!events[i].packet.bytes[2]);
}
void pedal_state_does_not_cross_owner_paths_or_usb_cables() {
    for (bool cables : {false,true}) {
        const uint64_t other_owner = cables ? 1 : 2;
        const uint16_t other_source = cables ? 1 : 0;
        std::vector<TimelinePacket> events{
            {10, {11, {0xb0, 64, 96}, 0}, 0, 0, 1},
            {20, {11, {0xb0, 64, 80}, other_source}, 0, 0, other_owner},
            {30, {11, {0xb0, 64, 0}, 0}, 0, 0, 1},
            {40, {11, {0xb0, 64, 0}, other_source}, 0, 0, other_owner},
        };
        assert(reverse_sequence(events, 100));
        assert(events.size() == 4 && events[0].time == 60 && events[1].time == 70);
        assert(events[0].packet.bytes[2] == 80 && events[0].owner == other_owner && events[0].packet.source == other_source);
        assert(events[1].packet.bytes[2] == 96 && events[1].owner == 1 && events[1].packet.source == 0);
        assert(events[2].time == 80 && events[3].time == 90 && !events[2].packet.bytes[2] && !events[3].packet.bytes[2]);
    }
}
void clock_skips_overdue_ticks_without_drifting() {
    StreamClock clock;
    clock.start(0, 120000);
    assert(!clock.poll(20833));
    assert(clock.poll(20834));
    assert(clock.poll(1000000));
    assert(clock.skipped() == 46);
    assert(!clock.poll(1000001));
    clock.stop();
    assert(!clock.poll(2000000));
}
int main() {
    cntrl::memory_budget = 1024 * 1024;
    reverses_note_intervals_with_original_velocity();
    pairs_repeated_notes_fifo_and_orders_zero_duration_notes();
    sysex_frames_keep_byte_order_and_completion_placement();
    simultaneous_controls_keep_their_order();
    pedal_reversal_restores_previous_values_and_closes_boundary_holds();
    pedal_state_is_independent_for_each_source_channel_and_controller();
    pedal_state_does_not_cross_owner_paths_or_usb_cables();
    clock_skips_overdue_ticks_without_drifting();
}
