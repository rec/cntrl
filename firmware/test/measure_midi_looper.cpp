// Standalone checked-allocation measurement, not a pedal timing test.
#include <cntrl/midi_looper.h>
#include <cassert>
#include <cstdio>

namespace {
using namespace cntrl::midi;
struct Output : Sink {
    unsigned capture_contexts(unsigned inputs, unsigned channels) const override {
        return Sink::capture_contexts(inputs & 1, channels) / 16;
    }
    bool write_packet(unsigned, const Packet &) override { return true; }
};

void measure(unsigned count, unsigned outputs, bool controllers, bool together = false) {
    assert(cntrl::memory_used == 0);
    {
        LoopFile source, index;
        MidiFilePlayer player(source,index);
        MidiFileRecorder recorder(source);
        MidiLooper loop(player,recorder);
        Output output;
        LooperSettings settings;
        settings.inputs = 1; settings.outputs = uint16_t(outputs); settings.events = 512;
        assert(loop.configure(settings,output));
        const unsigned idle = cntrl::memory_used;
        cntrl::memory_peak = idle;
        assert(loop.record({0,1,true},500000));
        const unsigned armed = cntrl::memory_used;
        loop.poll({MidiLooper::beat,1,true},true,outputs,output);
        for (unsigned i = 0; i < count; ++i) {
            const uint64_t position = MidiLooper::beat + MidiLooper::beat * i / (count + 1);
            const bool on = together ? i < count / 2 : !(i & 1);
            const unsigned key = together ? i % (count / 2) : (i / 2) % 128;
            const Packet packet = controllers
                ? Packet{11,{uint8_t(0xb0 | ((i / 10) % 16)),uint8_t(70 + i % 10),uint8_t(i % 128)}}
                : Packet{uint8_t(on ? 9 : 8),{uint8_t(on ? 0x90 : 0x80),uint8_t(key),uint8_t(on ? 100 : 0)}};
            loop.receive(0,0,packet,{position,1,true});
            loop.poll({position,1,true},true,outputs,output);
        }
        const unsigned captured = cntrl::memory_used;
        assert(loop.record({MidiLooper::beat * 2 - 1,1,true},500000));
        for (unsigned i = 0; i < 256; ++i) loop.poll({MidiLooper::beat * 2,1,true},true,outputs,output);
        assert(loop.state() == MidiLooper::State::playing && loop.events() == count);
        const unsigned playing = cntrl::memory_used;
        for (unsigned i = 1; i <= count + 1; ++i)
            loop.poll({MidiLooper::beat * 2 + MidiLooper::beat * i / (count + 1),1,true},true,outputs,output);
        loop.stop();
        for (unsigned i = 0; i < 256; ++i) loop.poll({MidiLooper::beat * 3,1,true},true,outputs,output);
        assert(!output.loop_cleaning());
        std::printf("events=%u output_mask=%u kind=%s idle=%u armed=%u captured=%u playing=%u stopped=%u peak=%u\n",
            count,outputs,controllers ? "controllers" : together ? "chord" : "notes",
            idle,armed,captured,playing,cntrl::memory_used,cntrl::memory_peak);
    }
    assert(cntrl::memory_used == 0);
}
}

int main() {
    cntrl::memory_budget = 16 * 1024 * 1024;
    std::printf("pointer_bytes=%zu allocation_model=align8_plus8\n",sizeof(void *));
    for (unsigned outputs : {1u,3u}) for (unsigned count : {0u,24u,512u}) {
        measure(count,outputs,false);
        if (count) measure(count,outputs,true);
    }
    measure(128,1,false,true);
}
