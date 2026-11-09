#include <cntrl/midi_processing.h>
#include <cntrl/tempo.h>
#include "patch_hardware.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace cntrl::midi;
namespace {
struct Output : Sink {
    uint64_t now = 0;
    unsigned blocked = 0, count[2] = {}, observed = 0;
    bool trace = false;
    bool write_packet(unsigned output, const Packet &packet) override {
        if (blocked & (1u << output)) return false;
        ++count[output];
        if (trace) {
            std::printf("%llu %u", (unsigned long long)now, output);
            for (unsigned i = 0; i < packet.size(); ++i) std::printf(" %02x", unsigned(packet.bytes[i]));
            std::puts("");
        }
        return true;
    }
    void packet_sent(unsigned, const Packet &) override { ++observed; }
};
struct DocumentHardware : TestPatchHardware {
    unsigned switch_capacity() const override { return 5; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    bool supports_trs_mode(unsigned, cntrl::midi::TrsMode) const override { return true; }
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override { assert(false); }
};
const OutputConnection endpoints[] = {
    {"din", "DIN", 0, OutputTransport::physical, true, true},
    {"trs", "TRS", 1, OutputTransport::physical, true, true},
};
void poll(Streams &streams, Output &output, uint64_t now, unsigned times = 12) {
    output.now = now;
    for (unsigned i = 0; i < times; ++i) streams.service(output, now);
}
void shapes() {
    constexpr uint64_t one = uint64_t(1) << 32;
    assert(Lfo::shape(0, 0, 0) == one / 2);
    assert(Lfo::shape(0, one / 4, 0) == one);
    assert(Lfo::shape(0, one * 3 / 4, 0) == 0);
    assert(Lfo::shape(1, one / 2, 0) == one);
    assert(Lfo::shape(6, one / 8, 0) == one / 2);
    assert(Lfo::shape(6, one / 2, 0) == one);
    assert(Lfo::shape(6, one * 3 / 4, 0) == 0);
    assert(Lfo::shape(5, one / 2, 7) == Lfo::shape(5, 0, 7));
    assert(Lfo::shape(7, one - 1, 7) + 2 >= Lfo::shape(7, one, 7));
    for (unsigned shape = 0; shape < 8; ++shape) {
        std::printf("shape %u:", shape);
        for (unsigned i = 0; i <= 8; ++i) std::printf(" %llu", (unsigned long long)Lfo::shape(shape, one * i / 8, 7));
        std::puts("");
    }
    Lfo oscillator;
    oscillator.config.low = -8192; oscillator.config.high = 8191;
    oscillator.scope(0, {}); oscillator.enable(0, {}, 1);
    assert(oscillator.sample() == 0);
    oscillator.advance(250000, {}, true); assert(oscillator.sample() == 8191);
    oscillator.advance(750000, {}, true); assert(oscillator.sample() == -8192);
    oscillator.config.low = 8191; oscillator.config.high = -8192;
    assert(oscillator.sample() == 8191);
    oscillator.config.once = true;
    oscillator.config.shape = 2;
    oscillator.config.period = 1000;
    oscillator.enable(0, {}, 2);
    oscillator.advance(UINT64_MAX - 1, {}, true);
    assert(oscillator.complete());
    assert(oscillator.sample(true) == -8192);
}
void transactions() {
    Output output;
    Parameter parameter{Parameter::cc14, 0, 1, 12345};
    ParameterTransaction transaction{parameter, 1, 0};
    assert(output.submit_parameter(0, transaction));
    assert(output.parameter_locked(0));
    assert(output.parameter_value(0, parameter) == Parameter::unknown);
    assert(!output.submit(0, {11,{0xb0,7,100}}));
    assert(!output.submit(0, {15,{0xf6,0,0}}));
    assert(output.submit(0, {15,{0xf8,0,0}}));
    output.blocked = 1;
    assert(!output.submit_parameter(0, transaction));
    output.blocked = 0;
    assert(output.submit_parameter(0, transaction));
    assert(!output.parameter_locked(0));
    assert(output.parameter_value(0, parameter) == 12345);
    ParameterTransaction partial{parameter, 2, 0};
    assert(output.submit_parameter(0, partial));
    output.abandon_parameter(0, partial);
    assert(output.parameter_value(0, parameter) == Parameter::unknown);
    assert(!output.parameter_locked(0));
    assert(output.observed == output.count[0] + output.count[1]);
}
void beat_clocks() {
    constexpr uint64_t one = uint64_t(1) << 32;
    cntrl::ClockGenerator internal;
    internal.start(0, 0, false);
    assert(internal.beat_position(250000).value == one / 2);
    internal.set_tempo(1000000);
    assert(internal.beat_position(750000).value == one);
    internal.stop();
    assert(internal.beat_position(2000000).value == one);
    internal.start(2000000, 0, true);
    assert(internal.beat_position(2500000).value == one * 3 / 2);
    internal.stop(); internal.start(3000000, 0, false);
    assert(internal.beat_position(3000000).value == 0);
    cntrl::ClockTempo external;
    external.configure(24, 1, 500000);
    external.transport(0, 0xfa, 0);
    for (unsigned i = 0; i <= 24; ++i) external.clock(0, i * 10000);
    assert(external.beat_position(240000).value == one);
    external.transport(0, 0xfc, 240000);
    assert(!external.beat_position(300000).running);
    assert(external.beat_position(300000).value == one);
    external.transport(0, 0xfb, 400000);
    for (unsigned i = 0; i <= 24; ++i) external.clock(0, 400000 + i * 10000);
    assert(external.beat_position(640000).value == one * 2);
    external.poll(1200000);
    auto held = external.beat_position(1200000);
    assert(!held.running && external.beat_position(2000000).value == held.value);
    external.transport(0, 0xfa, 2100000); external.clock(0, 2100000);
    assert(external.beat_position(2100000).value == 0);
}
}
int main(int argc, char **argv) {
    if (argc == 1) { shapes(); transactions(); beat_clocks(); return 0; }
    assert(argc == 3);
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), {});
    if (std::string(argv[2]) == "document") {
        DocumentHardware hardware;
        cntrl::Document document;
        assert(document.load(data.data(), unsigned(data.size()), hardware));
        return 0;
    }
    Streams streams;
    assert(streams.load(data.data(), unsigned(data.size()), {endpoints, 2}, 2));
    Output output;
    std::string mode = argv[2];
    if (mode == "stress") {
        for (unsigned i = 0; i < 256; ++i) streams.receive(0, {9,{uint8_t(0x90 | (i / 128)),uint8_t(i % 128),100}}, 0);
        for (unsigned i = 0; i < 2000; ++i) {
            output.blocked = i < 500 ? 1 : 0;
            poll(streams, output, uint64_t(i) * 1000, 1);
            assert(streams.retained_bytes() + 32768 <= 200704 + 4 * data.size());
        }
        assert(streams.lfo_status().pending == 0);
        assert(streams.diagnostics().coalesced != 0);
        std::printf("retained storage: %llu\n", (unsigned long long)streams.retained_bytes());
        return 0;
    }
    assert(data.size() >= 133);
    LfoConfig configuration;
    assert(configuration.decode(data.data() + 88, 45));
    if (mode != "missing-restore") {
        Parameter previous = configuration.target; previous.value = 42;
        for (unsigned destination = 0; destination < 2; ++destination) {
            ParameterTransaction seed{previous, 1, 0};
            while (seed.position != previous.messages()) assert(output.submit_parameter(destination, seed));
        }
    }
    output.trace = true;
    if (mode == "missing-restore") {
        poll(streams, output, 0);
        assert(streams.diagnostics().failed == 1);
        assert(!output.count[0] && !output.count[1]);
        return 0;
    }
    if (mode == "blocked") output.blocked = 1;
    poll(streams, output, 0);
    assert(streams.gate("sweep", true)); // Already enabled must not restart.
    if (mode == "clock") {
        streams.beat_position({uint64_t(1) << 30, 0, true}); poll(streams, output, 250000);
        streams.beat_position({uint64_t(1) << 31, 0, false}); poll(streams, output, 500000);
        poll(streams, output, 750000);
        streams.beat_position({uint64_t(3) << 30, 0, true}); poll(streams, output, 1000000);
        streams.beat_position({0, 1, true}); poll(streams, output, 1250000);
    } else if (mode == "gate") {
        poll(streams, output, 250000);
        assert(streams.gate("sweep", false)); poll(streams, output, 250000);
        poll(streams, output, 500000);
        assert(streams.gate("sweep", true)); poll(streams, output, 750000);
    } else if (mode == "once") {
        poll(streams, output, 2000000);
        poll(streams, output, 3000000);
        assert(streams.gate("sweep", true)); poll(streams, output, 4000000);
    } else if (mode == "restart-stopping") {
        poll(streams, output, 250000);
        output.blocked = 1;
        assert(streams.gate("sweep", false)); poll(streams, output, 500000);
        assert(streams.gate("sweep", true)); poll(streams, output, 750000);
        output.blocked = 0; poll(streams, output, 1000000);
    } else if (mode == "partial-stop") {
        poll(streams, output, 250000, 1);
        assert(output.parameter_locked(0));
        assert(streams.gate("sweep", false));
        poll(streams, output, 250000);
        assert(!output.parameter_locked(0) && !output.parameter_locked(1));
    } else if (mode == "replace") {
        streams.output_connection(0, false, 1); streams.output_connection(0, true, 2);
        poll(streams, output, 250000); poll(streams, output, 500000);
    } else if (mode == "manual") {
        Parameter manual = configuration.target; manual.value = 13;
        uint8_t bytes[3] = {}; manual.message(0, bytes);
        Packet packet{uint8_t(bytes[0] >> 4), {bytes[0],bytes[1],bytes[2]}};
        assert(!output.submit(0, packet)); poll(streams, output, 250000);
        assert(output.submit(0, packet));
        poll(streams, output, 500000);
    } else {
        poll(streams, output, 250000);
        if (mode == "blocked") output.blocked = 0;
        poll(streams, output, 500000); poll(streams, output, 750000); poll(streams, output, 1000000);
    }
    assert(streams.gate("sweep", false));
    poll(streams, output, 1500000);
    assert(streams.gate("sweep", false));
    poll(streams, output, 1500000);
}
