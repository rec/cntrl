#pragma once
#ifndef CNTRL_RUNTIME_TRACE
#define CNTRL_RUNTIME_TRACE(name) ((void)0)
#endif
#ifndef CNTRL_RUNTIME_PACKET
#define CNTRL_RUNTIME_PACKET(route, packet) ((void)0)
#endif
#include "document.h"
#include "midi_bridge.h"
#include "midi_monitor.h"
#include "text_messages.h"
#include "midi_arpeggiator.h"
#include "checked_array.h"
#include "checked_list.h"
#include <cstdio>
#include <charconv>
#include <limits>

namespace cntrl::midi {
struct InputEvent {
    static constexpr unsigned absent = std::numeric_limits<unsigned>::max();
    unsigned input = 0, forward = absent, clock_source = 0, cable_input = absent;
    unsigned cable = 0;
    Packet packet{};
    uint8_t byte = 0;
    bool packetized = false, relay = false;
};

// Platform services supply nonblocking raw events, time and physical delivery.
// This class owns fairness, musical service order and diagnostics policy.
class Runtime {
public:
    TextMessages text_messages;
    MidiArpeggiator arpeggiator;
    Monitor &monitor() { return monitor_; }
    Streams &streams() { return streams_; }
    uint64_t stream_time(uint32_t now) { return extend(now); }
    bool prepare(Outputs outputs) {
        const unsigned count = outputs.route_count();
        if (generations_.size() == count) return true;
        CheckedArray<uint64_t> candidate;
        if (!candidate.resize(count)) return false;
        for (unsigned i = 0; i < std::min(count,generations_.size()); ++i) candidate[i] = generations_[i];
        generations_.swap(candidate); return true;
    }
    unsigned parser_memory_failures() const { return parser_memory_failures_; }
    bool prepare_arpeggiator_configuration(const uint8_t *settings,Sink &sink,Outputs outputs) {
        const unsigned capacity = arpeggiator_parsers_.capacity();
        unsigned count = arpeggiator_parsers_.size();
        const unsigned inputs = settings ? u16(settings + 10) : 0;
        for (const auto &endpoint : outputs) {
            if (!endpoint.input || endpoint.transport != OutputTransport::physical || !(inputs & (1u << endpoint.route))) continue;
            bool present = false;
            for (const auto &entry : arpeggiator_parsers_) if (entry.input == endpoint.route && !entry.cable) present = true;
            if (!present) ++count;
        }
        if (!arpeggiator_parsers_.reserve(count)) return false;
        if (arpeggiator.prepare_configuration(settings,sink)) {
            prepared_parser_capacity_ = capacity; parser_configuration_prepared_ = true;
            return true;
        }
        arpeggiator_parsers_.trim(arpeggiator_parsers_.capacity() - capacity);
        return false;
    }
    void finish_arpeggiator_configuration(bool commit) {
        if (!parser_configuration_prepared_) return;
        if (commit) arpeggiator.finish_configuration();
        else {
            arpeggiator.discard_prepared_configuration();
            if (parser_configuration_prepared_)
                arpeggiator_parsers_.trim(arpeggiator_parsers_.capacity() - prepared_parser_capacity_);
        }
        parser_configuration_prepared_ = false;
    }
#ifdef CNTRL_NOINLINE
    CNTRL_NOINLINE
#endif
    Runtime(unsigned source_items = 64, unsigned input_budget_us = 180)
        : source_items_(source_items), input_budget_us_(input_budget_us) {}
    template<class Services>
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_runtime_poll)
#endif
    void poll(Services &s) {
        const uint32_t poll_time = s.microseconds();
        if (polled_) max_service_gap_us_ = std::max(max_service_gap_us_, uint32_t(poll_time - previous_poll_us_));
        previous_poll_us_ = poll_time; polled_ = true;
        s.midi_transport_poll();
        auto &bridge = s.midi_bridge();
        auto &files = s.runtime_midi_files();
        auto &pages = s.runtime_pages();
        const bool topology_ready = prepare(s.midi_outputs());
        if (!topology_ready && !topology_memory_failed_) { ++parser_memory_failures_; s.error("MIDI input memory unavailable"); }
        topology_memory_failed_ = !topology_ready;
        s.note_routing = &arpeggiator;
        bridge.host_connected(s.midi_host_connected());
        uint32_t begin = s.microseconds(), now = s.milliseconds();
        text_messages.poll(now);
        s.pickup_time(begin);
        bridge.connected(s.midi_device_connected(), now);
        uint64_t stream_time = extend(s.microseconds());
        for (const auto &endpoint : s.midi_outputs()) {
            if (endpoint.route >= generations_.size()) continue;
            bool present = endpoint.transport == OutputTransport::physical
                || (endpoint.transport == OutputTransport::usb_host ? s.midi_host_connected() : s.midi_device_connected());
            uint64_t identity = endpoint_identity(s, endpoint.route, 0);
            uint64_t generation = endpoint_generation(s, endpoint.route, 0);
            if (!present || generation != generations_[endpoint.route] || !bridge.usb_enabled(endpoint.route)) text_messages.forget(endpoint.route);
            if (!bridge.usb_enabled(endpoint.route)) arpeggiator.disconnect(endpoint.route);
            if (!present || generation != generations_[endpoint.route]) s.forget_pickup_source(endpoint.route);
            if (!present || generation != generations_[endpoint.route]) {
                arpeggiator.disconnect(endpoint.route);
                arpeggiator_parsers_.erase_if([&](const ArpeggiatorParser &entry) { return entry.input == endpoint.route; });
                files.looper.connection(endpoint.route); files.forget_input(endpoint.route); s.forget_output(endpoint.route);
            }
            if (monitor_.active()) monitor_.connection(endpoint.route, present, identity, generation, s.microseconds());
            if (generation != generations_[endpoint.route]) {
                streams_.connection(endpoint.route, false, identity, stream_time);
                streams_.output_connection(endpoint.route, false, identity);
                generations_[endpoint.route] = generation;
            }
            streams_.connection(endpoint.route, present, identity, stream_time);
            streams_.output_connection(endpoint.route, present, identity);
        }
        if (!s.midi_document_valid()) streams_.stop_all();
        if (!s.midi_document_valid()) pages.generator.stop();
        if (!s.midi_document_valid()) files.looper.clear();
        if (!s.midi_document_valid()) arpeggiator.stop();
        unsigned available = s.clock_available();
        bool reserved = false;
#if CNTRL_PANIC
        reserved = s.panic_owns_midi;
#endif
#if CNTRL_CABLE_TEST
        reserved = reserved || s.cable_test.enabled();
#endif
        if (reserved) files.looper.stop();
        if (reserved) arpeggiator.suspend();
        pages.clock_connections(available);
        unsigned stream_clocks = streams_.clock_outputs();
        streams_.clock_owner(files.player.clock_outputs() | (pages.clock_owner() & ~stream_clocks));
        bridge.clock_owner(pages.clock_owner() | files.player.clock_outputs() | stream_clocks);
        pages.generator.poll(s.microseconds(), available & ~files.player.clock_outputs() & ~stream_clocks, s);
        unsigned count = s.midi_source_count();
        CNTRL_RUNTIME_TRACE(midi_inputs);
        for (unsigned i = 0; i < count && uint32_t(s.microseconds() - begin) < input_budget_us_; ++i) {
            unsigned source = (source_cursor_ + i) % count;
            for (unsigned item = 0; item < source_items_ && uint32_t(s.microseconds() - begin) < input_budget_us_; ++item) {
                InputEvent event;
                if (!s.midi_read_event(source, event)) break;
                CNTRL_RUNTIME_TRACE(midi_ingress);
                CNTRL_RUNTIME_PACKET(event.input, event.packetized ? event.packet.packed(event.input) : uint32_t(event.byte) << 8);
                event.packet.source = uint16_t(event.input * 16 + event.cable);
                uint32_t timestamp = s.microseconds();
                bool diagnostic_traffic = false;
#if CNTRL_CABLE_TEST
                diagnostic_traffic = s.cable_test.enabled();
#endif
                if (event.packetized) monitor_.packet(Monitor::Direction::incoming, event.input, event.cable, event.packet, timestamp, diagnostic_traffic);
                else monitor_.byte(Monitor::Direction::incoming, event.input, event.cable, event.byte, timestamp, diagnostic_traffic);
                if (!bridge.usb_enabled(event.input)) continue;
                if (event.packetized) {
                    for (unsigned b = 0; b < event.packet.size(); ++b)
                        text_messages.byte(event.input, event.cable, event.packet.bytes[b], now);
                } else text_messages.byte(event.input, event.cable, event.byte, now);
                uint8_t status = event.packetized ? event.packet.bytes[0] : event.byte;
                if (status == 0xf8 || (status >= 0xfa && status <= 0xfc))
                    pages.clock_message(event.clock_source, status, timestamp);
                if (event.input >= generations_.size()) continue;
                auto position = pages.beat_position(timestamp);
                if (!reserved) {
                    auto arp_position = pages.arpeggiator_beat_position(timestamp);
                    arpeggiator.poll(arp_position,available,bridge.busy_outputs() | files.player.sysex_outputs() | streams_.sysex_outputs(),s,pages.clock_period());
                    prepare_arpeggiator_inputs(s.midi_outputs());
                    if (event.packetized) arpeggiator.receive(event.input,event.cable,event.packet,arp_position);
                    else if (arpeggiator.input_selected(event.input) && event.cable < 16) {
                        Packet packet;
                        auto *parser = arpeggiator_parser(event.input,event.cable,event.byte);
                        if (parser && parser->receive(event.byte,packet)) arpeggiator.receive(event.input,event.cable,packet,arp_position);
                    }
                }
                if (!reserved) files.looper.poll(position,pages.clock_ready(),available,s,pages.clock_period());
                if (event.packetized) files.receive(event.input,event.packet,timestamp,reserved ? BeatPosition{} : position);
                else files.receive(event.input,event.byte,timestamp,reserved ? BeatPosition{} : position);
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
                if (event.packetized) streams_.receive(event.input, event.packet, extend(timestamp));
                else streams_.byte(event.input, event.byte, extend(timestamp), event.cable);
                if (streams_.explicit_system()) continue;
                if (event.forward == InputEvent::absent) continue;
                if (event.relay) {
                    bridge.relay_packet(event.forward, event.packet, now);
                    continue;
                }
                if (event.packetized) {
                    const auto *output = s.midi_outputs().route(event.forward);
                    if (output && s.available(output->name)) bridge.forward_packet(event.forward, event.packet, now);
                } else bridge.receive(event.forward, event.byte, now, event.input * 16 + event.cable);
            }
        }
        source_cursor_ = count ? (source_cursor_ + 1) % count : 0;
        CNTRL_RUNTIME_TRACE(midi_derived);
        files.looper.poll(pages.beat_position(s.microseconds()),pages.clock_ready(),available,s,pages.clock_period());
        arpeggiator.poll(pages.arpeggiator_beat_position(s.microseconds()),available,bridge.busy_outputs() | files.player.sysex_outputs() | streams_.sysex_outputs(),s,pages.clock_period());
        prepare_arpeggiator_inputs(s.midi_outputs());
#if CNTRL_SWITCHES
        s.midi_switch_tick(now);
#endif
#if CNTRL_PANIC
        if (s.panic_owns_midi) {
            if (!s.loop_cleaning()) s.panic.tick(now, bridge.handoff(s));
            if (!s.panic.active()) { bridge.configure(s.runtime.interface); s.panic_owns_midi = false; }
        } else
#endif
        CNTRL_RUNTIME_TRACE(midi_bridge);
#if CNTRL_CABLE_TEST
        if (s.cable_test.enabled()) { if (!s.loop_cleaning() && bridge.handoff(s)) s.cable_test.tick(now); }
        else
#endif
        bridge.service(s, now, files.player.sysex_outputs() | streams_.sysex_outputs());
        bool exclusive = false;
#if CNTRL_PANIC
        exclusive = s.panic_owns_midi;
#endif
#if CNTRL_CABLE_TEST
        exclusive = exclusive || s.cable_test.enabled();
#endif
        CNTRL_RUNTIME_TRACE(midi_streams);
        if (exclusive) streams_.stop_all();
        else {
            streams_.beat_position(pages.beat_position(s.microseconds()));
            streams_.service(s, extend(s.microseconds()), available,
                bridge.busy_outputs() | files.player.sysex_outputs(), 64, files.player.sysex_outputs());
        }
        CNTRL_RUNTIME_TRACE(midi_reclaim);
        if (!exclusive) s.pickup_poll(s.microseconds(), bridge.destinations(),
            available & ~bridge.busy_outputs() & ~files.player.sysex_outputs() & ~streams_.sysex_outputs());
        CNTRL_RUNTIME_TRACE(midi_flush);
        s.midi_flush();
        if (monitor_.active()) monitor_.poll(s.microseconds());
        CNTRL_RUNTIME_TRACE(midi_files);
        files.recorder.poll(s.microseconds());
        if (files.recorder.failed() && !recording_failed_) s.error("MIDI recording write failed");
        recording_failed_ = files.recorder.failed();
        if (!files.looper.owned() && bridge.sysex_ready(files.player.sysex_outputs())) files.player.poll(s.microseconds(), s.clock_available(), s);
        files.loop_storage_poll();
        s.midi_flush();
        uint32_t elapsed = s.microseconds() - begin;
        if (elapsed > max_midi_us_) max_midi_us_ = elapsed;
    }
    void scanned(uint32_t gap) { if (gap > max_scan_gap_) max_scan_gap_ = gap; ++scan_count_; }
    void displayed() { ++display_count_; }
    template<class Replies>
    void stream_status(Replies &replies) const {
        const auto &counts = streams_.diagnostics();
        const auto lfos = streams_.lfo_status();
        char response[512];
        char failed[17];
        *std::to_chars(failed,failed + sizeof(failed) - 1,lfos.failed,16).ptr = 0;
        std::snprintf(response, sizeof(response), "CNTRL MIDI STREAMS %u %u %u %u %lu %lu %lu %lu lfos=%u pending=%u stopping=%u waiting=%u missing=%u failed=%s failed_count=%u coalesced=%lu skipped=%lu bytes=%lu heap_used=%u heap_peak=%u heap_budget=%u allocations=%u allocation_failures=%u",
            streams_.selected(), streams_.pending(), streams_.voices(), streams_.recovery(),
            (unsigned long)counts.transmitted, (unsigned long)counts.dropped,
            (unsigned long)counts.overloaded, (unsigned long)counts.failed,
            lfos.running, lfos.pending, lfos.stopping, lfos.waiting, lfos.missing,
            failed, lfos.failed_count, (unsigned long)counts.coalesced, (unsigned long)counts.skipped,
            (unsigned long)streams_.retained_bytes(), memory_used, memory_peak, memory_budget, memory_allocations, memory_failures);
        replies.reply(response);
    }
    template<class Replies>
    void reset(Replies &replies) { max_midi_us_ = max_scan_gap_ = max_service_gap_us_ = 0; polled_ = false; replies.reply("CNTRL OK"); }
    template<class Services>
    void status(const Bridge &bridge, unsigned route, const Document &document, Services &s) const {
        if (route >= bridge.destinations()) { s.reply("CNTRL ERROR midi-route"); return; }
        unsigned errors = 0;
        if (document.valid()) for (const auto &endpoint : s.midi_outputs())
            if (endpoint.port != no_port && uint8_t(s.port_mode(endpoint.port)) != document.trs_modes()[endpoint.port])
                errors |= 1u << endpoint.port;
        const auto &counts = bridge.counters(route);
        unsigned quarantine = 0;
        for (unsigned cable = 0; cable < 16; ++cable) if (bridge.input_quarantined(route, cable)) quarantine |= 1u << cable;
        char response[400];
        std::snprintf(response, sizeof(response), "CNTRL MIDI %u %u %lu %lu %lu %lu %u %lu %lu %lu %lu %u bridge_quarantine=%x stream_quarantine=%x pedal_quarantine=%u service_gap_us=%lu",
            unsigned(bridge.enabled()), route, (unsigned long)counts.received,
            (unsigned long)counts.transmitted, (unsigned long)counts.dropped, (unsigned long)counts.aborted,
            counts.high_water, (unsigned long)max_midi_us_, (unsigned long)max_scan_gap_,
            (unsigned long)scan_count_, (unsigned long)display_count_, errors, quarantine,
            streams_.input_quarantine(route), unsigned(bridge.pedal_quarantined(route)), (unsigned long)max_service_gap_us_);
        s.reply(response);
    }
private:
    Monitor monitor_;
    struct ArpeggiatorParser { unsigned input, cable; Parser parser; };
    CheckedList<ArpeggiatorParser> arpeggiator_parsers_;
    unsigned prepared_parser_capacity_ = 0;
    bool parser_configuration_prepared_ = false;
    Streams streams_;
    CheckedArray<uint64_t> generations_;
    unsigned parser_memory_failures_ = 0;
    bool topology_memory_failed_ = false;
    void prepare_arpeggiator_inputs(Outputs outputs) {
        if (!arpeggiator.configured()) { arpeggiator_parsers_.release(); arpeggiator.input_memory_ready(true); return; }
        unsigned count = 0;
        for (const auto &endpoint : outputs)
            if (endpoint.input && endpoint.transport == OutputTransport::physical && arpeggiator.input_selected(endpoint.route)) ++count;
        arpeggiator_parsers_.erase_if([&](const ArpeggiatorParser &entry) { return !arpeggiator.input_selected(entry.input); });
        const bool ready = arpeggiator_parsers_.reserve(std::max(count,arpeggiator_parsers_.size()));
        arpeggiator.input_memory_ready(ready);
        if (!ready) ++parser_memory_failures_;
    }
    Parser *arpeggiator_parser(unsigned input, unsigned cable, uint8_t byte) {
        for (auto &entry : arpeggiator_parsers_) if (entry.input == input && entry.cable == cable) return &entry.parser;
        if (byte < 0x80 || byte >= 0xf0) return nullptr;
        auto *entry = arpeggiator_parsers_.append({input,cable,{}});
        if (!entry) { ++parser_memory_failures_; arpeggiator.input_memory_ready(false); return nullptr; }
        return &entry->parser;
    }
    uint64_t stream_time_ = 0;
    uint32_t previous_time_ = 0;
    uint64_t extend(uint32_t now) {
        stream_time_ += uint32_t(now - previous_time_); previous_time_ = now;
        return stream_time_;
    }
    template<class Services>
    static auto endpoint_identity(Services &s, unsigned route, int) -> decltype(s.midi_endpoint_identity(route)) {
        return s.midi_endpoint_identity(route);
    }
    template<class Services>
    static uint64_t endpoint_identity(Services &, unsigned route, long) { return route + 1; }
    template<class Services>
    static auto endpoint_generation(Services &s, unsigned route, int) -> decltype(s.midi_endpoint_generation(route)) {
        return s.midi_endpoint_generation(route);
    }
    template<class Services>
    static uint64_t endpoint_generation(Services &, unsigned, long) { return 0; }
    unsigned source_items_, input_budget_us_, source_cursor_ = 0;
    uint32_t max_midi_us_ = 0, max_scan_gap_ = 0, scan_count_ = 0, display_count_ = 0;
    uint32_t previous_poll_us_ = 0, max_service_gap_us_ = 0;
    bool polled_ = false;
    bool recording_failed_ = false;
};
} // namespace cntrl::midi
