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
    clock_skips_overdue_ticks_without_drifting();
}
