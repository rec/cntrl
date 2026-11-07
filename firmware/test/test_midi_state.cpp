#include "midi_endpoints.h"
#include <cntrl/midi_bridge.h>
#include <cassert>

struct Output : cntrl::midi::Sink {
    bool writable = true;
protected:
    bool write_packet(unsigned, const cntrl::midi::Packet &) override { return writable; }
};

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
    for (uint8_t mode : {120, 123, 124, 125, 126, 127}) {
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

int main() {
    test_sparse_state_follows_successful_delivery();
    test_notes_and_pressure_are_sparse();
    test_outputs_and_channels_keep_independent_values();
    test_channel_modes_clear_affected_state();
    test_bridge_clients_get_tracking_without_recording_state();
}
