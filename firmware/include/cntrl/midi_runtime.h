#pragma once
#include "document.h"
#include "midi_bridge.h"
#include <cstdio>
#include <limits>

namespace cntrl::midi {
struct InputEvent {
    static constexpr unsigned absent = std::numeric_limits<unsigned>::max();
    unsigned input = 0, forward = absent, clock_source = 0, cable_input = absent;
    Packet packet{};
    uint8_t byte = 0;
    bool packetized = false;
};

// Platform services supply nonblocking raw events, time and physical delivery.
// This class owns fairness, musical service order and diagnostics policy.
class Runtime {
public:
    Runtime(unsigned source_items = 64, unsigned input_budget_us = 180)
        : source_items_(source_items), input_budget_us_(input_budget_us) {}
    template<class Services>
    void poll(Services &s) {
        s.midi_transport_poll();
        auto &bridge = s.midi_bridge();
        auto &files = s.runtime_midi_files();
        auto &pages = s.runtime_pages();
        bridge.host_connected(s.midi_host_connected());
        uint32_t begin = s.microseconds(), now = s.milliseconds();
        bridge.connected(s.midi_device_connected(), now);
        if (!s.midi_document_valid()) pages.generator.stop();
        unsigned available = s.clock_available();
        pages.clock_connections(available);
        bridge.clock_owner(pages.clock_owner() | files.player.clock_outputs());
        pages.generator.poll(s.microseconds(), available & ~files.player.clock_outputs(), s);
        unsigned count = s.midi_source_count();
        for (unsigned i = 0; i < count && uint32_t(s.microseconds() - begin) < input_budget_us_; ++i) {
            unsigned source = (source_cursor_ + i) % count;
            for (unsigned item = 0; item < source_items_ && uint32_t(s.microseconds() - begin) < input_budget_us_; ++item) {
                InputEvent event;
                if (!s.midi_read_event(source, event)) break;
                if (!bridge.usb_enabled(event.input)) continue;
                uint32_t timestamp = s.microseconds();
                if (event.packetized) files.receive(event.input, event.packet, timestamp);
                else files.receive(event.input, event.byte, timestamp);
                if ((event.packetized ? event.packet.bytes[0] : event.byte) == 0xf8)
                    pages.clock(event.clock_source, timestamp);
#if CNTRL_CABLE_TEST
                if (s.cable_test.enabled()) {
                    if (!event.packetized && event.cable_input != InputEvent::absent)
                        s.cable_test.receive(event.cable_input, event.byte, now);
                    continue;
                }
#endif
#if CNTRL_PANIC
                if (s.panic_owns_midi) continue;
#endif
                if (event.forward == InputEvent::absent) continue;
                if (event.packetized) {
                    const auto *output = s.midi_outputs().route(event.forward);
                    if (output && s.available(output->name)) bridge.forward_packet(event.forward, event.packet, now);
                } else bridge.receive(event.forward, event.byte, now);
            }
        }
        source_cursor_ = count ? (source_cursor_ + 1) % count : 0;
#if CNTRL_SWITCHES
        s.midi_switch_tick(now);
#endif
#if CNTRL_PANIC
        if (s.panic_owns_midi) {
            s.panic.tick(now, bridge.handoff(s));
            if (!s.panic.active()) { bridge.configure(s.runtime.interface); s.panic_owns_midi = false; }
        } else
#endif
#if CNTRL_CABLE_TEST
        if (s.cable_test.enabled()) { if (bridge.handoff(s)) s.cable_test.tick(now); }
        else
#endif
        bridge.service(s, now, files.player.sysex_outputs());
        s.midi_flush();
        files.recorder.poll(s.microseconds());
        if (files.recorder.failed() && !recording_failed_) s.error("MIDI recording write failed");
        recording_failed_ = files.recorder.failed();
        if (bridge.sysex_ready(files.player.sysex_outputs())) files.player.poll(s.microseconds(), s.clock_available(), s);
        s.midi_flush();
        uint32_t elapsed = s.microseconds() - begin;
        if (elapsed > max_midi_us_) max_midi_us_ = elapsed;
    }
    void scanned(uint32_t gap) { if (gap > max_scan_gap_) max_scan_gap_ = gap; ++scan_count_; }
    void displayed() { ++display_count_; }
    template<class Replies>
    void reset(Replies &replies) { max_midi_us_ = max_scan_gap_ = 0; replies.reply("CNTRL OK"); }
    template<class Services>
    void status(const Bridge &bridge, unsigned route, const Document &document, Services &s) const {
        if (route >= bridge.destinations()) { s.reply("CNTRL ERROR midi-route"); return; }
        unsigned errors = 0;
        if (document.valid()) for (const auto &endpoint : s.midi_outputs())
            if (endpoint.port != no_port && uint8_t(s.port_mode(endpoint.port)) != document.trs_modes()[endpoint.port])
                errors |= 1u << endpoint.port;
        const auto &counts = bridge.counters(route);
        char response[400];
        std::snprintf(response, sizeof(response), "CNTRL MIDI %u %u %lu %lu %lu %lu %u %lu %lu %lu %lu %u",
            unsigned(bridge.enabled()), route, (unsigned long)counts.received,
            (unsigned long)counts.transmitted, (unsigned long)counts.dropped, (unsigned long)counts.aborted,
            counts.high_water, (unsigned long)max_midi_us_, (unsigned long)max_scan_gap_,
            (unsigned long)scan_count_, (unsigned long)display_count_, errors);
        s.reply(response);
    }
private:
    unsigned source_items_, input_budget_us_, source_cursor_ = 0;
    uint32_t max_midi_us_ = 0, max_scan_gap_ = 0, scan_count_ = 0, display_count_ = 0;
    bool recording_failed_ = false;
};
} // namespace cntrl::midi
