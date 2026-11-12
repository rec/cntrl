#include "midi_endpoints.h"
#include <cntrl/midi_bridge.h>
#include <cassert>
#include <initializer_list>

struct Output : cntrl::midi::Sink {
    bool writable = true;
    unsigned writes = 0;
protected:
    bool write_packet(unsigned, const cntrl::midi::Packet &) override { if (writable) ++writes; return writable; }
};

void allocation_failure_precedes_output_and_releases_need_no_allocation() {
    for (int failure = 0; failure < 3; ++failure) {
        Output out; cntrl::memory_fail_after = failure;
        assert(!out.submit(0, {9, {0x90, 60, 100}}));
        assert(!out.writes && out.state().outputs.empty());
        cntrl::memory_fail_after = -1;
    }
    Output out;
    assert(out.submit(0, {9, {0x90, 60, 100}}));
    const unsigned allocations = cntrl::memory_allocations;
    cntrl::memory_fail_after = 0;
    assert(out.submit(0, {8, {0x80, 60, 0}}));
    assert(out.submit(0, {11, {0xb0, 123, 0}}));
    assert(out.writes == 3 && cntrl::memory_allocations == allocations);
    cntrl::memory_fail_after = -1;
}

void parameter_admission_reserves_the_whole_message_group() {
    Output out;
    cntrl::midi::Parameter parameter{cntrl::midi::Parameter::nrpn14, 0, 17, 1234};
    cntrl::midi::ParameterTransaction transaction{parameter, 11};
    assert(out.submit_parameter(0, transaction));
    const unsigned allocations = cntrl::memory_allocations;
    cntrl::memory_fail_after = 0;
    while (transaction.position < parameter.messages()) assert(out.submit_parameter(0, transaction));
    assert(out.writes == 6 && cntrl::memory_allocations == allocations);
    assert(out.parameter_value(0, parameter) == 1234);
    cntrl::memory_fail_after = -1;
}

void playback_credits_survive_live_memory_pressure_and_recycle() {
    Output out;
    assert(out.reserve_loop_ownership(4));
    assert(out.reserve_playback_state(4, 1, 1));
    const unsigned budget = cntrl::memory_budget;
    cntrl::memory_budget = cntrl::memory_used;
    assert(!out.submit(0, {9, {0x90, 70, 100}}));
    const unsigned allocations = cntrl::memory_allocations;
    assert(out.submit_loop(0, {9, {0x90, 60, 100}}));
    assert(out.submit_loop(0, {8, {0x80, 60, 0}}));
    assert(out.submit_loop(0, {9, {0x90, 62, 100}}));
    assert(out.submit_loop(0, {8, {0x80, 62, 0}}));
    assert(cntrl::memory_allocations == allocations);
    cntrl::memory_budget = budget;
}

void arming_does_not_lose_a_preexisting_live_note_owner() {
    Output out;
    assert(out.submit(0, {9, {0x90, 60, 100}, 7}));
    assert(out.reserve_loop_ownership(4));
    assert(out.reserve_playback_state(4, 1, 1));
    const unsigned before = out.writes;
    assert(out.submit_loop(0, {9, {0x90, 60, 100}}));
    assert(out.submit_loop(0, {8, {0x80, 60, 0}}));
    assert(out.writes == before);
    assert(out.submit(0, {8, {0x80, 60, 0}, 7}));
    assert(out.writes == before + 1);
}

void failed_playback_reservation_returns_all_new_memory() {
    for (int failure = 0; failure < 17; ++failure) {
        Output out; const unsigned before = cntrl::memory_used;
        cntrl::memory_fail_after = failure;
        assert(!out.reserve_playback_state(3, 1, 1));
        assert(cntrl::memory_used == before);
        cntrl::memory_fail_after = -1;
    }
}

void test_sparse_state_follows_successful_delivery() {
    Output out;
    assert(out.state().outputs.empty());
    out.submit(0, {15, {0xf8, 0, 0}});
    out.submit(1, {4, {0xf0, 1, 2}});
    out.submit(2, {8, {0x80, 60, 0}});
    out.submit(3, {9, {0x90, 60, 0}});
    assert(out.state().outputs.empty());
    out.writable = false;
    assert(!out.submit(5, {11, {0xb3, 7, 90}}));
    assert(out.state().outputs.empty());
    out.writable = true;
    assert(out.submit(5, {11, {0xb3, 7, 90}}));
    assert(out.state().outputs.size() == 1);
    const auto &output = out.state().outputs.front();
    assert(output.output == 5 && output.channels.size() == 1);
    const auto &channel = output.channels.front();
    assert(channel.channel == 3 && channel.notes.empty());
    assert(channel.controllers.size() == 1 && channel.controllers[0].value == 90);
    assert(!channel.program && !channel.pressure && !channel.pitch_bend);
    out.submit(5, {11, {0xb3, 7, 0}});
    assert(channel.controllers.size() == 1 && channel.controllers[0].value == 0);
    out.writable = false;
    assert(!out.submit(5, {11, {0xb3, 7, 100}}));
    assert(channel.controllers[0].value == 0);
}

void test_notes_and_pressure_are_sparse() {
    Output out;
    out.submit(0, {9, {0x90, 60, 100}});
    const auto &channel = out.state().outputs[0].channels[0];
    assert(channel.notes.size() == 1 && channel.notes[0].velocity == 100);
    out.submit(0, {10, {0xa0, 60, 20}});
    out.submit(0, {9, {0x90, 60, 90}});
    assert(channel.notes.size() == 1 && channel.notes[0].velocity == 90);
    assert(channel.notes[0].pressure == 20);
    out.submit(0, {9, {0x90, 64, 80}});
    assert(channel.notes.size() == 2);
    out.submit(0, {8, {0x80, 60, 64}});
    assert(channel.notes.size() == 1 && channel.notes[0].note == 64);
    out.submit(0, {9, {0x90, 64, 0}});
    assert(channel.notes.empty());
    out.submit(0, {10, {0xa0, 127, 0}});
    assert(channel.notes.size() == 1 && !channel.notes[0].velocity);
    assert(channel.notes[0].pressure == 0);
}

void test_outputs_and_channels_keep_independent_values() {
    Output out;
    for (unsigned route : {0u, 8u}) for (unsigned channel : {0u, 15u}) {
        out.submit(route, {12, {uint8_t(0xc0 | channel), uint8_t(route + channel), 0}});
        out.submit(route, {13, {uint8_t(0xd0 | channel), 0, 0}});
        out.submit(route, {14, {uint8_t(0xe0 | channel), 127, 127}});
    }
    assert(out.state().outputs.size() == 2);
    for (const auto &output : out.state().outputs) {
        assert(output.channels.size() == 2);
        for (const auto &channel : output.channels) {
            assert(channel.program == output.output + channel.channel);
            assert(channel.pressure == 0 && channel.pitch_bend == 16383);
            assert(channel.notes.empty() && channel.controllers.empty());
        }
    }
    out.submit(8, {15, {0xff, 0, 0}});
    assert(out.state().outputs.size() == 1 && out.state().outputs[0].output == 0);
}

void test_channel_modes_clear_affected_state() {
    Output out;
    out.submit(0, {12, {0xc0, 12, 0}});
    out.submit(0, {11, {0xb0, 7, 90}});
    out.submit(0, {13, {0xd0, 40, 0}});
    out.submit(0, {14, {0xe0, 1, 2}});
    out.submit(0, {9, {0x90, 60, 100}});
    out.submit(0, {10, {0xa0, 60, 20}});
    out.submit(0, {11, {0xb0, 121, 0}});
    const auto &channel = out.state().outputs[0].channels[0];
    assert(channel.program == 12 && !channel.pressure && !channel.pitch_bend);
    assert(channel.controllers.size() == 1 && channel.controllers[0].controller == 121);
    assert(channel.notes.size() == 1 && channel.notes[0].velocity == 100 && !channel.notes[0].pressure);
    for (uint8_t mode : std::initializer_list<uint8_t>{120, 123, 124, 125, 126, 127}) {
        out.submit(0, {9, {0x90, 60, 100}});
        out.submit(0, {11, {0xb0, mode, 0}});
        assert(channel.notes.empty());
    }
}

void test_bridge_clients_get_tracking_without_recording_state() {
    cntrl::midi::Bridge bridge{test_midi_outputs};
    Output out;
    bridge.configure(true); bridge.connected(true, 0);
    uint8_t patch[] = {0xb0, 7, 90, 0xc0, 12};
    assert(bridge.pedal(0, patch, sizeof(patch)));
    out.writable = false; bridge.service(out, 0);
    assert(out.state().outputs.empty());
    out.writable = true;
    for (unsigned i = 0; i < 2; ++i) bridge.service(out, 0);
    assert(out.state().outputs.size() == 1);
    const auto &channel = out.state().outputs[0].channels[0];
    assert(channel.controllers[0].value == 90 && channel.program == 12);
    bridge.forward_packet(0, {9, {0x90, 60, 100}}, 0); bridge.service(out, 0);
    assert(channel.notes.size() == 1 && channel.notes[0].velocity == 100);
}

void configured_terminal_streams_keep_their_admitted_memory() {
    Output out;
    int identity = 0;
    uint8_t on[] = {0xb0, 0, 1, 0xc0, 12};
    uint8_t off[] = {0xb1, 7, 0, 0xc1, 13};
    assert(out.retain_owner_state(&identity, 17));
    assert(out.reserve_owner_stream(&identity, 17, 0, on, sizeof(on)));
    assert(out.reserve_owner_stream(&identity, 17, 8, off, sizeof(off)));
    assert(out.prepare_owner_cycle(&identity));
    out.activate_owner_state(&identity, 17);
    cntrl::memory_fail_after = 0;
    assert(out.prepare_stream(17, 0, on, sizeof(on)));
    assert(out.submit_owned(17, 0, {11, {0xb0, 0, 1}}));
    assert(out.submit_owned(17, 0, {12, {0xc0, 12, 0}}));
    out.finish_stream(17);
    assert(out.prepare_stream(17, 8, off, sizeof(off)));
    assert(out.submit_owned(17, 8, {11, {0xb1, 7, 0}}));
    assert(out.submit_owned(17, 8, {12, {0xc1, 13, 0}}));
    out.finish_stream(17);
    cntrl::memory_fail_after = -1;
    assert(out.state().outputs.size() == 2);
    out.release_owner_state(&identity);
}

int main() {
    cntrl::memory_budget = 524288;
    configured_terminal_streams_keep_their_admitted_memory();
    allocation_failure_precedes_output_and_releases_need_no_allocation();
    parameter_admission_reserves_the_whole_message_group();
    playback_credits_survive_live_memory_pressure_and_recycle();
    arming_does_not_lose_a_preexisting_live_note_owner();
    failed_playback_reservation_returns_all_new_memory();
    test_sparse_state_follows_successful_delivery();
    test_notes_and_pressure_are_sparse();
    test_outputs_and_channels_keep_independent_values();
    test_channel_modes_clear_affected_state();
    test_bridge_clients_get_tracking_without_recording_state();
}
