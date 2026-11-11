#define CNTRL_CABLE_TEST 1
#define CNTRL_PANIC 1
#define CNTRL_SWITCHES 1
#include <cntrl/midi_runtime.h>
#include <algorithm>
#include <cassert>
#include <deque>
#include <sstream>
#include <iterator>
#include <string>
#include <vector>

constexpr cntrl::midi::OutputConnection endpoints[] = {
    {"wire", "Wire", 0, cntrl::midi::OutputTransport::physical, true},
    {"computer", "Computer", 1, cntrl::midi::OutputTransport::usb_device, true},
};
struct Recorder {
    std::vector<std::string> *log;
    bool failure = false;
    bool failed() const { return failure; }
    void poll(uint32_t) { log->push_back("record"); }
};
struct Player {
    std::vector<std::string> *log;
    unsigned clock_outputs() const { return 0; }
    unsigned sysex_outputs() const { return 0; }
    template<class Sink> void poll(uint32_t, unsigned, Sink &) { log->push_back("play"); }
};
struct Files {
    struct Looper {
        void connection(unsigned) {}
        void clear() {}
        void stop() {}
        bool owned() const { return false; }
        template<class Sink> void poll(cntrl::midi::BeatPosition,bool,unsigned,Sink &,unsigned) {}
    } looper;
    Recorder recorder;
    Player player;
    explicit Files(std::vector<std::string> &log) : recorder{&log}, player{&log} {}
    void receive(unsigned, const cntrl::midi::Packet &, uint32_t, cntrl::midi::BeatPosition) { recorder.log->push_back("input"); }
    void receive(unsigned, uint8_t, uint32_t, cntrl::midi::BeatPosition) { recorder.log->push_back("input"); }
    void forget_input(unsigned) {}
    void loop_storage_poll() {}
};
struct Generator {
    std::vector<std::string> *log;
    void stop() {}
    template<class Sink> void poll(uint32_t, unsigned, Sink &) { log->push_back("clock"); }
};
struct Pages {
    Generator generator;
    void clock_connections(unsigned) {}
    unsigned clock_owner() const { return 0; }
    void clock_message(unsigned, uint8_t, uint32_t) {}
    cntrl::midi::BeatPosition beat_position(uint32_t) { return {}; }
    bool clock_ready() const { return true; }
    unsigned clock_period() const { return 500000; }
};
struct Cable {
    bool running = false;
    unsigned received = 0, ticks = 0;
    bool enabled() const { return running; }
    void receive(unsigned, uint8_t, uint32_t) { ++received; }
    void tick(uint32_t) { ++ticks; }
};
struct Panic {
    bool running = false;
    unsigned ticks = 0;
    bool active() const { return running; }
    void tick(uint32_t, bool ready) { ++ticks; if (ready) running = false; }
};
struct Services : cntrl::midi::Sink {
    cntrl::midi::Monitor *observer = nullptr;
    std::vector<std::string> log, replies;
    std::vector<unsigned> reads, writes;
    std::vector<cntrl::midi::Packet> packets;
    std::vector<std::deque<cntrl::midi::InputEvent>> sources;
    cntrl::midi::Bridge bridge{{endpoints, 2}};
    Files files{log};
    Pages pages{{&log}};
    Cable cable_test;
    Panic panic;
    bool panic_owns_midi = false;
    bool host_present = false;
    struct { bool interface = true; } runtime;
    uint32_t time = 0, timer_step = 1;
    unsigned errors = 0;
    explicit Services(unsigned count = 3) : sources(count) {
        pickup(false);
        bridge.configure(true);
        for (unsigned i = 0; i < count; ++i) {
            cntrl::midi::InputEvent event;
            event.packetized = true; event.forward = 0;
            event.packet = {11, {0xb0, 1, uint8_t(i)}};
            sources[i].push_back(event); sources[i].push_back(event);
        }
    }
    uint32_t milliseconds() const { return time / 1000; }
    uint32_t microseconds() { time += timer_step; return time; }
    void midi_transport_poll() { log.push_back("transport"); }
    bool midi_host_connected() const { return host_present; }
    bool midi_device_connected() const { return true; }
    bool midi_document_valid() const { return true; }
    cntrl::midi::Bridge &midi_bridge() { return bridge; }
    Files &runtime_midi_files() { return files; }
    Pages &runtime_pages() { return pages; }
    cntrl::midi::Outputs midi_outputs() const { return {endpoints, 2}; }
    unsigned midi_source_count() const { return unsigned(sources.size()); }
    unsigned clock_available() const { return 3; }
    bool available(const char *) const { return true; }
    void midi_switch_tick(uint32_t) { log.push_back("switches"); }
    void midi_flush() { log.push_back("flush"); }
    void error(const char *) { ++errors; }
    void reply(const char *text) { replies.emplace_back(text); }
    unsigned port_mode(unsigned) const { return 0; }
    bool midi_read_event(unsigned source, cntrl::midi::InputEvent &event) {
        if (sources[source].empty()) return false;
        event = sources[source].front(); sources[source].pop_front(); reads.push_back(source); return true;
    }
    bool write_packet(unsigned route, const cntrl::midi::Packet &packet) override {
        writes.push_back(route); packets.push_back(packet); log.push_back("send"); return true;
    }
    void packet_sent(unsigned route, const cntrl::midi::Packet &packet) override {
        if (observer) observer->packet(cntrl::midi::Monitor::Direction::outgoing, route, 0, packet, time, cable_test.enabled());
    }
};

void test_rotating_sources_and_delivery_precede_file_work() {
    Services services;
    cntrl::midi::Runtime runtime(1, 180);
    runtime.poll(services);
    assert((services.reads == std::vector<unsigned>{0, 1, 2}));
    assert(services.writes.size() == 1);
    auto position = [&](const char *name) { return std::find(services.log.begin(), services.log.end(), name); };
    assert(position("clock") < position("input"));
    assert(position("input") < position("switches") && position("switches") < position("send"));
    assert(position("send") < position("record") && position("record") < position("play"));
    runtime.poll(services);
    assert((services.reads == std::vector<unsigned>{0, 1, 2, 1, 2, 0}));
}
void test_time_budget_zero_sources_and_failure_edges() {
    Services services(5);
    cntrl::midi::Runtime runtime(1, 180);
    services.timer_step = 200;
    runtime.poll(services);
    assert(services.reads.empty());
    services.sources.clear(); services.timer_step = 1;
    services.files.recorder.failure = true;
    runtime.poll(services); runtime.poll(services);
    assert(services.errors == 1);
    services.files.recorder.failure = false; runtime.poll(services);
    services.files.recorder.failure = true; runtime.poll(services);
    assert(services.errors == 2);
}
void test_exclusive_utilities_and_diagnostics() {
    Services services;
    cntrl::midi::Runtime runtime(1, 180);
    services.panic_owns_midi = services.panic.running = true;
    runtime.poll(services);
    assert(services.writes.empty() && services.panic.ticks == 1 && !services.panic_owns_midi);
    services.cable_test.running = true;
    services.sources[1].front().packetized = false;
    services.sources[1].front().byte = 0xb0;
    services.sources[1].front().cable_input = 0;
    runtime.poll(services);
    assert(services.writes.empty() && services.cable_test.received == 1 && services.cable_test.ticks == 1);
    runtime.scanned(42); runtime.displayed();
    cntrl::Document document;
    runtime.status(services.bridge, 0, document, services);
    std::istringstream stream(services.replies.back());
    std::vector<std::string> fields{std::istream_iterator<std::string>(stream), {}};
    assert(fields[10] == "42" && fields[11] == "1" && fields[12] == "1");
    runtime.reset(services); assert(services.replies.back() == "CNTRL OK");
    runtime.status(services.bridge, 2, document, services);
    assert(services.replies.back() == "CNTRL ERROR midi-route");
}
void test_relay_runs_when_physical_interface_is_disabled() {
    Services services(1);
    services.sources[0].clear(); services.host_present = true;
    services.bridge.configure(false);
    cntrl::midi::InputEvent event;
    event.packetized = event.relay = true; event.input = 0; event.forward = 1;
    event.packet = {9, {0x90, 60, 100}};
    services.sources[0].push_back(event);
    cntrl::midi::Runtime runtime;
    runtime.poll(services);
    assert(services.writes == std::vector<unsigned>{1});
}
void test_explicit_routes_replace_forwarding_and_yield_to_utilities() {
    Services services(1); services.sources[0].clear();
    cntrl::midi::Runtime runtime;
    std::vector<uint8_t> graph(87, 0);
    std::memcpy(graph.data(), "CSTR", 4); cntrl::put32(graph.data() + 4, unsigned(graph.size()));
    graph[8] = graph[10] = graph[14] = 1;
    graph[12] = graph[13] = 255; graph[16] = 2; graph[20] = 'a';
    assert(runtime.streams().load(graph.data(), unsigned(graph.size()), services.midi_outputs(), 0));
    cntrl::midi::InputEvent event;
    event.input = event.forward = 0; event.packetized = true;
    event.packet = {9, {0x90, 60, 100}};
    services.sources[0].push_back(event);
    runtime.poll(services);
    assert(services.writes == std::vector<unsigned>{1});
    runtime.streams().receive(0, {9, {0x90, 62, 100}}, services.time);
    services.cable_test.running = true;
    runtime.poll(services);
    assert(services.writes.size() == 1);
    services.cable_test.running = false;
    runtime.poll(services);
    assert(services.writes.size() == 2);
    assert(services.packets.back().bytes[0] == 0x80 && services.packets.back().bytes[1] == 60);
    runtime.stream_status(services);
    assert(services.replies.back().find("CNTRL MIDI STREAMS ") == 0);
}
void test_monitor_observes_inputs_before_gates_and_actual_transformed_outputs() {
    cntrl::memory_budget = 128 * 1024;
    Services services(1); services.sources[0].clear();
    cntrl::midi::Runtime runtime; services.observer = &runtime.monitor();
    assert(runtime.monitor().open(services.time));
    services.bridge.configure_usb(false, true, 0);
    cntrl::midi::InputEvent event;
    event.input = 1; event.cable = 3; event.packetized = true; event.packet = {9, {0x90,60,100}};
    services.sources[0].push_back(event); runtime.poll(services);
    assert(runtime.monitor().tail() == 1 && services.writes.empty());
    assert(runtime.monitor().entry(0)->route == 1 && runtime.monitor().entry(0)->cable == 3);
    runtime.monitor().close(); assert(runtime.monitor().open(services.time));
    services.cable_test.running = true; event.input = 0;
    services.sources[0].push_back(event); runtime.poll(services);
    assert(runtime.monitor().tail() == 1);
    assert(runtime.monitor().entry(0)->flags & cntrl::midi::Monitor::diagnostic);
    services.cable_test.running = false;
    std::vector<uint8_t> graph(87, 0);
    graph.insert(graph.end(), {2,0,4,0,12,0,0,0}); // Transpose by twelve semitones.
    std::memcpy(graph.data(), "CSTR", 4); cntrl::put32(graph.data() + 4, unsigned(graph.size()));
    graph[8] = graph[10] = graph[14] = graph[18] = 1;
    graph[12] = graph[13] = 255; graph[16] = 1; graph[20] = 'a'; // Input 0, output 0.
    assert(runtime.streams().load(graph.data(), unsigned(graph.size()), services.midi_outputs(), 0));
    services.sources[0].push_back(event); runtime.poll(services);
    assert(runtime.monitor().tail() == 3 && services.writes.size() == 1);
    assert(runtime.monitor().entry(1)->direction == cntrl::midi::Monitor::Direction::incoming);
    assert(runtime.monitor().entry(2)->direction == cntrl::midi::Monitor::Direction::outgoing);
    assert(runtime.monitor().entry(2)->bytes[1] == 72);
    runtime.streams().stop_all(); runtime.poll(services);
    assert(runtime.monitor().tail() == 4);
    assert(runtime.monitor().entry(3)->bytes[0] == 0x80 && runtime.monitor().entry(3)->bytes[1] == 72);
}
void test_text_arrives_without_forwarding_and_usb_enable_is_respected() {
    cntrl::memory_budget = 128 * 1024;
    Services services(1); services.sources[0].clear(); services.timer_step = 0;
    cntrl::midi::Runtime runtime;
    assert(runtime.monitor().open(0));
    const uint8_t bytes[] = {0xf0, 0x7d, 0x43, 0x4e, 0x54, 0x52, 0x4c, 1, 1, 'G', 'o', 0xf7};
    for (auto byte : bytes) {
        cntrl::midi::InputEvent event; event.byte = byte;
        services.sources[0].push_back(event);
    }
    runtime.poll(services);
    assert(runtime.text_messages.active(0) && !std::strcmp(runtime.text_messages.text(), "Go"));
    assert(services.writes.empty());
    assert(runtime.monitor().tail() > 0);
    services.bridge.configure_usb(false, true, 0);
    for (unsigned i = 0; i < sizeof(bytes); i += 3) {
        cntrl::midi::InputEvent event; event.input = 1; event.packetized = true;
        event.packet = {4, {bytes[i], bytes[i + 1], bytes[i + 2]}};
        if (i + 3 == sizeof(bytes)) event.packet.cin = 7;
        services.sources[0].push_back(event);
    }
    services.time = 6000000;
    runtime.poll(services); assert(!runtime.text_messages.active(6000));
    services.bridge.configure_usb(true, true, 6000);
    for (unsigned i = 0; i < sizeof(bytes); i += 3) {
        cntrl::midi::InputEvent event; event.input = 1; event.packetized = true;
        event.packet = {uint8_t(i + 3 == sizeof(bytes) ? 7 : 4), {bytes[i], bytes[i + 1], bytes[i + 2]}};
        services.sources[0].push_back(event);
    }
    runtime.poll(services); assert(runtime.text_messages.active(6000));
}
int main() {
    test_text_arrives_without_forwarding_and_usb_enable_is_respected();
    test_monitor_observes_inputs_before_gates_and_actual_transformed_outputs();
    test_rotating_sources_and_delivery_precede_file_work();
    test_time_budget_zero_sources_and_failure_edges();
    test_exclusive_utilities_and_diagnostics();
    test_relay_runs_when_physical_interface_is_disabled();
    test_explicit_routes_replace_forwarding_and_yield_to_utilities();
}
