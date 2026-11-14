// Standalone allocation measurement, not a pedal application or timing test.
#include <cntrl/midi_processing.h>
#include <cassert>
#include <cstdio>
#include <vector>

// Symbol sizes expose the actual compiler ABI without executing an ARM image.
extern "C" {
unsigned char size_basic_routing[sizeof(cntrl::midi::BasicRouting)];
unsigned char size_advanced_routing[sizeof(cntrl::midi::Streams)];
unsigned char size_basic_route[sizeof(cntrl::midi::BasicProcessing::Route)];
unsigned char size_advanced_route[sizeof(cntrl::midi::AdvancedProcessing::Route)];
unsigned char size_basic_step[sizeof(cntrl::midi::BasicProcessing::Step)];
unsigned char size_advanced_step[sizeof(cntrl::midi::AdvancedProcessing::Step)];
}

namespace {
using namespace cntrl::midi;
const OutputConnection endpoints[] = {
    {"in", "Input", 0, OutputTransport::physical, true, false},
    {"out", "Output", 1, OutputTransport::physical, false, true},
};
const Outputs outputs{endpoints, 2};
struct Output : Sink {
    bool write_packet(unsigned, const Packet &) override { return true; }
};

void integer(std::vector<uint8_t> &bytes, unsigned value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) bytes.push_back(uint8_t(value >> (8 * i)));
}

std::vector<uint8_t> graph(unsigned count, unsigned steps) {
    std::vector<uint8_t> bytes{'C','S','T','R',0,0,0,0};
    integer(bytes,count,2); integer(bytes,1,2);
    for (unsigned i = 0; i < count; ++i) {
        integer(bytes,65535,2); integer(bytes,1,2); integer(bytes,2,2); integer(bytes,steps,2);
        char name[64] = {};
        std::snprintf(name,sizeof(name),"route%u",i);
        bytes.insert(bytes.end(),name,name + sizeof(name));
        bytes.push_back(0); integer(bytes,0,2);
        for (unsigned j = 0; j < steps; ++j) {
            // Match all messages and map their channel to channel 1.
            bytes.insert(bytes.end(),{5,0,8,0,0,1,1,255,1,0,0,0});
        }
    }
    for (unsigned i = 0; i < 4; ++i) bytes[4 + i] = uint8_t(bytes.size() >> (8 * i));
    return bytes;
}

template<class Engine> void measure(const char *name, unsigned count, unsigned steps) {
    assert(cntrl::memory_used == 0);
    const auto bytes = graph(count,steps);
    {
        Engine engine;
        Output output;
        cntrl::memory_peak = 0;
        assert(engine.load(bytes.data(),unsigned(bytes.size()),outputs,1));
        const unsigned loaded = cntrl::memory_used, load_peak = cntrl::memory_peak;
        const size_t retained = engine.retained_bytes();
        cntrl::memory_peak = loaded;
        assert(engine.load(bytes.data(),unsigned(bytes.size()),outputs,1));
        engine.service(output,0); // Reclaim the obsolete configuration generation.
        std::printf("%s routes=%u steps=%u object=%zu retained=%zu ledger=%u load_peak=%u replacement_peak=%u settled=%u\n",
            name,count,steps,sizeof(Engine),retained,loaded,load_peak,cntrl::memory_peak,cntrl::memory_used);
    }
    assert(cntrl::memory_used == 0);
}
}

int main() {
    cntrl::memory_budget = 1024 * 1024;
    for (unsigned count : {0u,1u,24u}) {
        measure<BasicRouting>("basic",count,0);
        measure<Streams>("advanced",count,0);
    }
    measure<BasicRouting>("basic",24,16);
    measure<Streams>("advanced",24,16);
}
