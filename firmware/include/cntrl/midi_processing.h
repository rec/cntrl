#pragma once
#include "midi_bridge.h"
#include "midi_file.h"
#include "midi_stream_timeline.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace cntrl::midi {
// The wire section is shared with patch_switcher/midi_processing.py.
// All retained traffic has an explicit owner and a bounded storage allowance.
class Streams {
public:
    struct Emit {
        virtual ~Emit() = default;
        virtual bool emit(const Packet &packet, uint64_t delay = 0) = 0;
    };
    struct Processor {
        virtual ~Processor() = default;
        virtual bool push(const Packet &packet, unsigned source, Emit &output) = 0;
        virtual void service(uint64_t, unsigned, Emit &) {}
        virtual void stop() {}
    };
    using Factory = std::unique_ptr<Processor> (*)(const uint8_t *, unsigned);
    static bool register_processor(unsigned opcode, unsigned version, Factory factory, bool generator = false) {
        if (opcode < 128 || opcode > 255 || !version || version > 255 || !factory) return false;
        for (const auto &entry : registry()) if (entry.opcode == opcode) return false;
        registry().push_back({opcode, version, factory, generator}); return true;
    }
    static constexpr unsigned system_scope = 65535, max_routes = 64;
    struct Limits { unsigned events = 512, voices = 256, notes = 256, captures = 256; };
    struct Diagnostics { uint32_t dropped = 0, overloaded = 0, transmitted = 0, failed = 0; };
    struct Field { uint8_t id = 0; std::vector<int32_t> values; };
    struct Assignment { uint8_t field = 0, source = 255; int32_t value = 0; };
    struct Step {
        uint8_t kind = 0, field = 0;
        std::vector<Field> match;
        std::vector<Assignment> assignments;
        int32_t value = 0, input_low = 0, input_high = 127, output_low = 0, output_high = 127;
        uint64_t time = 0;
        unsigned repeats = 1;
        double feedback = 1, bpm = 120;
        std::vector<uint8_t> parameters;
        std::unique_ptr<Processor> processor;
    };
    struct Input { uint8_t kind = 0; uint16_t index = 0; };
    struct Route {
        char name[64] = {};
        uint16_t scope = system_scope, outputs = 0;
        uint16_t failed_outputs = 0;
        std::vector<Input> inputs;
        std::vector<Step> steps;
        bool enabled = true, failed = false;
        uint64_t next = 0;
        StreamClock clock;
        std::unique_ptr<StreamFileSource> file;
        bool started = false;
        bool foreign = false;
    };
    Streams() = default;
    explicit Streams(Limits limits) : limits_(limits) {}
    bool explicit_system() const { return explicit_system_; }
    bool configured() const { return !routes_.empty() || explicit_system_; }
    const Diagnostics &diagnostics() const { return diagnostics_; }
    unsigned sysex_outputs() const { return sysex_outputs_; }
    unsigned selected() const { return selected_; }
    void clock_owner(unsigned mask) { external_clock_ = mask; }
    unsigned pending() const { return unsigned(events_.size()); }
    unsigned voices() const { return unsigned(voices_.size()); }
    unsigned recovery() const { return unsigned(cleanup_.size()); }
    void file_provider(StreamFileProvider *provider) { file_provider_ = provider; }
    unsigned clock_outputs() const {
        unsigned mask = 0;
        for (unsigned i = 0; i < routes_.size(); ++i) if (active(i) && !routes_[i].steps.empty()) {
            const auto &step = routes_[i].steps.front();
            if (step.kind == 8 || (routes_[i].file && routes_[i].file->clock())) mask |= downstream_outputs(i);
        }
        return mask;
    }
    static bool valid(const uint8_t *data, unsigned size, Outputs outputs, unsigned nodes) {
        Streams candidate;
        return candidate.decode(data, size, outputs, nodes);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(streams_load)
#endif
    bool load(const uint8_t *data, unsigned size, Outputs outputs, unsigned nodes, uint64_t now = 0) {
        Streams candidate(limits_);
        if (!candidate.decode(data, size, outputs, nodes)) return false;
        stop_all();
        routes_ = std::move(candidate.routes_);
        base_routes_ = unsigned(routes_.size()); foreign_signature_ = 0; external_selected_ = false;
        explicit_system_ = candidate.explicit_system_;
        selected_ = system_scope;
        parsers_.assign(outputs.route_count(), Parser{});
        inputs_.assign(outputs.route_count(), Connection{});
        outputs_.resize(outputs.route_count());
        for (auto &route : routes_) route.next = now;
        return true;
    }
    void select(unsigned scope, uint64_t now = 0) {
        if (scope == selected_ && !external_selected_) return;
        for (unsigned i = 0; i < routes_.size(); ++i)
            if (routes_[i].scope != system_scope) { retire(uint64_t(1) << i); routes_[i].file.reset(); }
        selected_ = scope;
        if (external_selected_) { routes_.resize(base_routes_); external_selected_ = false; }
        for (auto &route : routes_) if (route.scope == scope) {
            route.enabled = true; route.failed = false; route.failed_outputs = 0; route.next = now; route.started = false;
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(streams_select_external)
#endif
    bool select_external(const uint8_t *data, unsigned size, Outputs outputs, unsigned nodes, unsigned scope, uint64_t now = 0) {
        if (scope == system_scope) { select(system_scope, now); return true; }
        uint64_t signature = 14695981039346656037ull;
        for (unsigned i = 0; i < size; ++i) { signature ^= data[i]; signature *= 1099511628211ull; }
        if (external_selected_ && foreign_signature_ == signature && foreign_scope_ == scope) return true;
        Streams candidate(limits_);
        if (!candidate.decode(data, size, outputs, nodes)) return false;
        unsigned local_count = 0;
        for (const auto &route : candidate.routes_) if (route.scope == scope) ++local_count;
        if (base_routes_ + local_count > max_routes) return false;
        std::vector<unsigned> mapping(candidate.routes_.size(), max_routes);
        unsigned next = base_routes_;
        for (unsigned i = 0; i < candidate.routes_.size(); ++i) {
            const auto &route = candidate.routes_[i];
            if (route.scope == scope) mapping[i] = next++;
            else if (route.scope == system_scope) {
                for (unsigned j = 0; j < base_routes_; ++j)
                    if (routes_[j].scope == system_scope && !std::strcmp(routes_[j].name, route.name)) mapping[i] = j;
            }
        }
        for (const auto &route : candidate.routes_) if (route.scope == scope)
            for (const auto &input : route.inputs) if (input.kind && mapping[input.index] == max_routes) return false;
        for (unsigned i = 0; i < routes_.size(); ++i) if (routes_[i].scope != system_scope) { retire(uint64_t(1) << i); routes_[i].file.reset(); }
        routes_.resize(base_routes_);
        for (auto &route : candidate.routes_) if (route.scope == scope) {
            for (auto &input : route.inputs) if (input.kind) input.index = uint16_t(mapping[input.index]);
            route.foreign = true; route.next = now;
            routes_.push_back(std::move(route));
        }
        selected_ = scope; foreign_scope_ = scope; foreign_signature_ = signature; external_selected_ = true; return true;
    }
    bool gate(const char *reference, bool enabled) {
        bool system = !std::strncmp(reference, "system/", 7);
        const char *name = system ? reference + 7 : reference;
        unsigned scope = system ? system_scope : selected_;
        for (unsigned i = 0; i < routes_.size(); ++i)
            if (routes_[i].scope == scope && (scope == system_scope || routes_[i].foreign == external_selected_) && !std::strcmp(routes_[i].name, name)) { gate(i, enabled); return true; }
        return false;
    }
    void gate(unsigned route, bool enabled) {
        if (route >= routes_.size() || routes_[route].enabled == enabled) return;
        if (!enabled) retire(uint64_t(1) << route);
        routes_[route].file.reset(); routes_[route].started = false;
        routes_[route].enabled = enabled;
        if (enabled) { routes_[route].failed = false; routes_[route].failed_outputs = 0; }
    }
    void stop_all() {
        retire(~uint64_t(0));
        for (auto &route : routes_) { route.file.reset(); route.started = false; }
        notes_.clear(); captures_.clear(); events_.clear();
        for (auto &input : inputs_) input.blocked = false;
    }
    void connection(unsigned input, bool present, uint64_t identity, uint64_t now = 0) {
        (void)now;
        if (input >= inputs_.size()) return;
        auto &connection = inputs_[input];
        if (connection.present == present && connection.identity == identity) return;
        connection.present = present; connection.identity = identity; ++connection.generation;
        parsers_[input].reset();
        retire_source(input);
    }
    void output_connection(unsigned output, bool present, uint64_t identity) {
        if (output >= outputs_.size()) return;
        auto &connection = outputs_[output];
        if (connection.present == present && connection.identity == identity) return;
        bool replacement = connection.identity != identity;
        connection.present = present; connection.identity = identity; ++connection.generation;
        clock_claimed_[output] = false;
        for (auto &route : routes_) route.failed_outputs &= uint16_t(~(1u << output));
        for (auto &voice : voices_) if (voice.output == output && voice.sounding) owe(voice);
        for (const auto &claim : holds_) if (claim.output == output) cleanup_packet(output, {11, {uint8_t(0xb0 | claim.channel), claim.controller, 0}});
        erase_if(holds_, [output](const Hold &claim) { return claim.output == output; });
        if (sysex_outputs_ & (1u << output)) cleanup_packet(output, {5, {0xf7, 0, 0}});
        erase_if(voices_, [output](const Voice &voice) { return voice.output == output; });
        erase_if(events_, [output](const Event &event) { return event.output == output; });
        if (replacement) erase_if(cleanup_, [output](const Cleanup &entry) { return entry.output == output; });
        sysex_outputs_ &= ~(1u << output);
    }
    void byte(unsigned input, uint8_t byte, uint64_t now) {
        if (input >= parsers_.size()) return;
        Packet packet;
        if (parsers_[input].receive(byte, packet)) receive(input, packet, now);
    }
    void receive(unsigned input, Packet packet, uint64_t now) {
        if (input >= inputs_.size() || !inputs_[input].present) return;
        current_time_ = now;
        unsigned kind = packet.bytes[0] >> 4;
        bool onset = kind == 9 && packet.bytes[2];
        bool release = kind == 8 || (kind == 9 && !packet.bytes[2]);
        uint64_t token = 0;
        if (onset) {
            if (notes_.size() >= limits_.notes) { ++diagnostics_.overloaded; retire_source(input); inputs_[input].blocked = true; return; }
            if (inputs_[input].blocked) { ++diagnostics_.dropped; return; }
            token = ++serial_;
            notes_.push_back({token, input, uint8_t(packet.bytes[0] & 15), packet.bytes[1]});
        } else if (release) {
            auto note = std::find_if(notes_.begin(), notes_.end(), [&](const Note &entry) {
                return entry.input == input && entry.channel == (packet.bytes[0] & 15) && entry.pitch == packet.bytes[1];
            });
            if (note == notes_.end()) return;
            token = note->token; notes_.erase(note);
            release_note(token, now, packet.bytes[2], &packet);
            return;
        } else if (kind == 10) {
            std::vector<Capture> pressure_captures;
            for (const auto &note : notes_)
                if (note.input == input && note.channel == (packet.bytes[0] & 15) && note.pitch == packet.bytes[1]) {
                    for (const auto &voice : voices_) if (voice.token == note.token) {
                        Packet pressure = packet;
                        if (!owned_fields(pressure, voice.owner, input)) continue;
                        if (pressure.bytes[0] >> 4 == 10) { pressure.bytes[0] = uint8_t(0xa0 | (voice.packet.bytes[0] & 15)); pressure.bytes[1] = voice.packet.bytes[1]; }
                        enqueue({now + voice.delay, voice.owner, 0, 0, input, voice.output, pressure});
                    }
                    for (const auto &capture : captures_) if (capture.work.token == note.token && capture.work.packet.bytes[0] >> 4 == 9) pressure_captures.push_back(capture);
                }
            for (const auto &capture : pressure_captures) {
                Work pressure = capture.work;
                pressure.packet = packet;
                if (!owned_fields(pressure.packet, capture.work.owner, input, capture.route, capture.step)) continue;
                if (pressure.packet.bytes[0] >> 4 == 10) { pressure.packet.bytes[0] = uint8_t(0xa0 | (capture.work.packet.bytes[0] & 15)); pressure.packet.bytes[1] = capture.work.packet.bytes[1]; }
                pressure.token = 0; pressure.due = now + capture.delay;
                process(capture.route, capture.step, pressure);
            }
            return;
        }
        Work work{packet, now, token, input, 0};
        for (unsigned i = 0; i < routes_.size(); ++i) if (active(i))
            for (const auto &source : routes_[i].inputs) if (!source.kind && source.index == input) { process(i, 0, work); break; }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(streams_service)
#endif
    void service(Sink &sink, uint64_t now, unsigned available = ~0u, unsigned busy = 0, unsigned budget = 64) {
        current_time_ = now;
        unsigned used = 0;
        for (auto cleanup = cleanup_.begin(); cleanup != cleanup_.end() && used < budget;) {
            unsigned bit = 1u << cleanup->output;
            if (!(available & bit) || (busy & bit) || !outputs_[cleanup->output].present) { ++cleanup; continue; }
            ++used;
            if (!sink.submit(cleanup->output, cleanup->packet)) { ++cleanup; continue; }
            ++diagnostics_.transmitted;
            if (cleanup->packet.bytes[0] == 0xf7) sysex_outputs_ &= ~(1u << cleanup->output);
            cleanup = cleanup_.erase(cleanup);
        }
        for (unsigned i = 0; i < routes_.size(); ++i) if (active(i) && !routes_[i].steps.empty() && routes_[i].steps[0].kind == 8) {
            auto &route = routes_[i];
            if (!route.started) { route.clock.start(now, unsigned(std::llround(route.steps[0].bpm * 1000))); route.started = true; }
            if (route.clock.poll(now) && used < budget) {
                Work work{{15, {0xf8, 0, 0}}, now, 0, system_scope, 0};
                process(i, 1, work); ++used;
            }
        }
        for (unsigned i = 0; i < routes_.size() && used < budget; ++i) if (active(i) && !routes_[i].steps.empty() && routes_[i].steps[0].kind == 9) {
            auto &route = routes_[i];
            if (!route.started) {
                route.started = true;
                const auto &bytes = route.steps[0].parameters;
                PlayMidiFilePatch settings;
                std::memcpy(settings.filename, bytes.data(), 96);
                Reader reader{bytes.data() + 96, bytes.data() + bytes.size()};
                settings.loops = uint32_t(reader.integer(4));
                settings.pre_delay = uint32_t(reader.integer(8) / 1000000); settings.post_delay = uint32_t(reader.integer(8) / 1000000);
                uint64_t speed = reader.integer(8); double value; std::memcpy(&value, &speed, 8); settings.speed = value;
                settings.reverse = reader.byte() != 0; settings.transpose = int8_t(reader.byte()); settings.outputs = 1;
                if (file_provider_) route.file = file_provider_->open_stream(settings, i);
                if (!route.file) { route.failed = true; ++diagnostics_.failed; continue; }
            }
            if (route.file) {
                GeneratedSink output(*this, i, now);
                route.file->poll(uint32_t(now), output); ++used;
                if (route.file->failed()) { retire(uint64_t(1) << i); route.failed = true; ++diagnostics_.failed; }
                else if (route.file->finished()) { finish_reverse(i, now); route.file.reset(); }
            }
        }
        for (unsigned i = 0; i < routes_.size() && used < budget; ++i) if (active(i)) {
            for (unsigned position = 0; position < routes_[i].steps.size() && used < budget; ++position) {
                auto &step = routes_[i].steps[position];
                if (!step.processor) continue;
                Work work{{}, now, 0, max_routes * (position + 1) + i, uint64_t(1) << i};
                ProcessorOutput output(*this, i, position + 1, work, budget - used);
                step.processor->service(now, budget - used, output); ++used;
            }
        }
        flush_reverse(now);
        unsigned delivered = 0;
        for (auto event = events_.begin(); event != events_.end() && used < budget;) {
            if (event->due > now) { ++event; continue; }
            unsigned bit = 1u << event->output;
            if ((delivered & bit) || !(available & bit) || (busy & bit) || recovering(event->output) || !outputs_[event->output].present) { ++event; continue; }
            bool realtime = event->packet.cin == 15;
            if ((sysex_outputs_ & bit) && !realtime && (sysex_owners_[event->output] != event->owner || sysex_sources_[event->output] != event->input)) { ++event; continue; }
            bool blocked_before = false;
            for (auto before = events_.begin(); before != event; ++before)
                if (before->output == event->output && before->due <= event->due
                    && (!(sysex_outputs_ & bit) || (before->owner == sysex_owners_[event->output] && before->input == sysex_sources_[event->output]))) { blocked_before = true; break; }
            if (blocked_before) { ++event; continue; }
            ++used;
            Voice *voice = nullptr;
            for (auto &entry : voices_) if (entry.id == event->voice) { voice = &entry; break; }
            bool onset = event->packet.bytes[0] >> 4 == 9 && event->packet.bytes[2];
            bool release = event->packet.bytes[0] >> 4 == 8;
            bool write = true;
            bool clock = event->packet.bytes[0] == 0xf8 || (event->packet.bytes[0] >= 0xfa && event->packet.bytes[0] <= 0xfc);
            if (clock && (external_clock_ & bit)) write = false;
            if (clock && clock_claimed_[event->output] && (clock_sources_[event->output] != event->input || clock_owners_[event->output] != event->owner)) write = false;
            if (voice && onset) {
                for (const auto &other : voices_) if (other.id != voice->id && other.sounding && same_key(other, *voice) && (other.owner != voice->owner || other.input != voice->input)) write = false;
            }
            if (voice && release) {
                if (!voice->sounding) write = false;
                for (const auto &other : voices_) if (other.id != voice->id && other.sounding && same_key(other, *voice) && (other.owner != voice->owner || other.input != voice->input)) write = false;
            }
            bool hold = is_hold(event->packet);
            bool holding = hold && event->packet.bytes[2] >= 64;
            bool own_hold = false;
            if (hold) for (const auto &claim : holds_)
                if (claim.output == event->output && claim.channel == (event->packet.bytes[0] & 15) && claim.controller == event->packet.bytes[1] && claim.owner == event->owner && claim.input == event->input) own_hold = true;
            if (holding && !own_hold && holds_.size() >= limits_.voices) { write = false; ++diagnostics_.dropped; }
            if (hold && !holding && !own_hold) write = false;
            if (hold && !holding) {
                for (const auto &claim : holds_)
                    if (claim.output == event->output && claim.channel == (event->packet.bytes[0] & 15) && claim.controller == event->packet.bytes[1]
                        && (claim.owner != event->owner || claim.input != event->input)) write = false;
            }
            if (write && !sink.submit(event->output, event->packet)) { ++event; continue; }
            if (write) { ++diagnostics_.transmitted; delivered |= bit; }
            if (write && clock) { clock_claimed_[event->output] = true; clock_sources_[event->output] = event->input; clock_owners_[event->output] = event->owner; }
            if (voice && onset) voice->sounding = true;
            if (write && (event->packet.bytes[0] == 0xff || (event->packet.bytes[0] >> 4 == 11 && event->packet.bytes[1] >= 120))) {
                for (auto &entry : voices_)
                    if (entry.output == event->output && (event->packet.bytes[0] == 0xff || (entry.packet.bytes[0] & 15) == (event->packet.bytes[0] & 15))) entry.sounding = false;
                if (event->packet.bytes[0] == 0xff || event->packet.bytes[1] == 121)
                    erase_if(holds_, [&](const Hold &claim) { return claim.output == event->output && (event->packet.bytes[0] == 0xff || claim.channel == (event->packet.bytes[0] & 15)); });
            }
            if (hold) {
                auto claim = std::find_if(holds_.begin(), holds_.end(), [&](const Hold &entry) { return entry.output == event->output && entry.input == event->input && entry.owner == event->owner && entry.channel == (event->packet.bytes[0] & 15) && entry.controller == event->packet.bytes[1]; });
                if (holding && write && claim == holds_.end()) {
                    if (holds_.size() < limits_.voices) holds_.push_back({event->input, event->output, event->owner, uint8_t(event->packet.bytes[0] & 15), event->packet.bytes[1]});
                } else if (!holding && claim != holds_.end()) holds_.erase(claim);
            }
            if (event->packet.cin == 4) { sysex_outputs_ |= bit; sysex_owners_[event->output] = event->owner; sysex_sources_[event->output] = event->input; }
            else if (event->packet.cin >= 5 && event->packet.cin <= 7 && event->packet.bytes[event->packet.size() - 1] == 0xf7) sysex_outputs_ &= ~bit;
            if (voice && release) { auto id = voice->id; erase_if(voices_, [id](const Voice &entry) { return entry.id == id; }); }
            event = events_.erase(event);
        }
    }
private:
    struct Registration { unsigned opcode, version; Factory factory; bool generator; };
    static std::vector<Registration> &registry() { static std::vector<Registration> result; return result; }
    struct Connection { bool present = true, blocked = false; uint64_t identity = 0; unsigned generation = 0; };
    struct Work { Packet packet; uint64_t due, token; unsigned input; uint64_t owner; };
    struct Note { uint64_t token; unsigned input; uint8_t channel, pitch; };
    struct Voice {
        uint64_t id, token, owner, delay;
        unsigned input, output;
        Packet packet;
        bool sounding = false;
    };
    struct Event { uint64_t due, owner, voice, sequence; unsigned input, output; Packet packet; };
    struct Cleanup { unsigned output; Packet packet; };
    struct Hold { unsigned input, output; uint64_t owner; uint8_t channel, controller; };
    struct Capture {
        Work work;
        unsigned route, step;
        uint64_t start, end = 0, delay = 0, frame = 0;
        bool closed = false;
    };
    struct Visit { uint64_t token, owner; unsigned route, step, input; Packet onset; };
    Limits limits_;
    Diagnostics diagnostics_;
    bool explicit_system_ = false;
    unsigned selected_ = system_scope, sysex_outputs_ = 0;
    unsigned external_clock_ = 0, base_routes_ = 0, foreign_scope_ = system_scope;
    uint64_t foreign_signature_ = 0;
    bool external_selected_ = false;
    uint64_t serial_ = 0;
    std::vector<Route> routes_;
    std::vector<Parser> parsers_;
    std::vector<Connection> inputs_, outputs_;
    std::vector<Note> notes_;
    std::vector<Voice> voices_;
    std::vector<Event> events_;
    std::vector<Cleanup> cleanup_;
    std::vector<Hold> holds_;
    uint64_t sysex_owners_[16] = {};
    unsigned sysex_sources_[16] = {};
    bool clock_claimed_[16] = {};
    unsigned clock_sources_[16] = {};
    uint64_t clock_owners_[16] = {};
    std::vector<Capture> captures_;
    std::vector<Visit> visits_;
    StreamFileProvider *file_provider_ = nullptr;
    struct GeneratedSink : Sink {
        Streams &streams; unsigned route; uint64_t now;
        GeneratedSink(Streams &streams, unsigned route, uint64_t now) : streams(streams), route(route), now(now) {}
        bool write_packet(unsigned, const Packet &packet) override { streams.generated(route, packet, now); return true; }
    };
    struct ProcessorOutput : Emit {
        Streams &engine; unsigned route, position; Work work; unsigned credits;
        ProcessorOutput(Streams &engine, unsigned route, unsigned position, Work work, unsigned credits)
            : engine(engine), route(route), position(position), work(work), credits(credits) {}
        bool emit(const Packet &packet, uint64_t delay = 0) override {
            if (!credits || engine.events_.size() + engine.voices_.size() >= engine.limits_.events) return false;
            --credits;
            Work transformed = work; transformed.packet = packet; transformed.due += delay;
            unsigned kind = packet.bytes[0] >> 4;
            if (!work.token && kind == 9 && packet.bytes[2]) {
                if (engine.notes_.size() >= engine.limits_.notes) return false;
                transformed.token = ++engine.serial_;
                engine.notes_.push_back({transformed.token, work.input, uint8_t(packet.bytes[0] & 15), packet.bytes[1]});
            } else if (!work.token && (kind == 8 || (kind == 9 && !packet.bytes[2]))) {
                auto note = std::find_if(engine.notes_.begin(), engine.notes_.end(), [&](const Note &note) { return note.input == work.input && note.channel == (packet.bytes[0] & 15) && note.pitch == packet.bytes[1]; });
                if (note == engine.notes_.end()) return true;
                uint64_t token = note->token; engine.notes_.erase(note); engine.release_note(token, transformed.due, packet.bytes[2], &packet); return true;
            }
            engine.process(route, position, transformed); return true;
        }
    };
    void generated(unsigned route, const Packet &packet, uint64_t now) {
        unsigned input = max_routes + route;
        unsigned kind = packet.bytes[0] >> 4;
        uint64_t token = 0;
        if (kind == 9 && packet.bytes[2]) {
            if (notes_.size() >= limits_.notes) { overload(uint64_t(1) << route); return; }
            token = ++serial_;
            notes_.push_back({token, input, uint8_t(packet.bytes[0] & 15), packet.bytes[1]});
        } else if (kind == 8 || (kind == 9 && !packet.bytes[2])) {
            auto note = std::find_if(notes_.begin(), notes_.end(), [&](const Note &note) { return note.input == input && note.channel == (packet.bytes[0] & 15) && note.pitch == packet.bytes[1]; });
            if (note == notes_.end()) return;
            token = note->token; notes_.erase(note); release_note(token, now, packet.bytes[2], &packet); return;
        }
        process(route, 1, {packet, now, token, input, 0});
    }
    template<class T, class Predicate> static void erase_if(std::vector<T> &items, Predicate predicate) {
        items.erase(std::remove_if(items.begin(), items.end(), predicate), items.end());
    }
    bool active(unsigned route) const {
        const auto &entry = routes_[route];
        return entry.enabled && !entry.failed && (entry.scope == system_scope || (entry.scope == selected_ && entry.foreign == external_selected_));
    }
    unsigned downstream_outputs(unsigned route) const {
        unsigned outputs = routes_[route].outputs;
        uint64_t reached = uint64_t(1) << route;
        for (unsigned i = route + 1; i < routes_.size(); ++i) if (active(i))
            for (const auto &input : routes_[i].inputs) if (input.kind && (reached & (uint64_t(1) << input.index))) { outputs |= routes_[i].outputs; reached |= uint64_t(1) << i; break; }
        return outputs;
    }
    bool recovering(unsigned output) const {
        for (const auto &entry : cleanup_) if (entry.output == output) return true;
        return false;
    }
    static bool is_hold(const Packet &packet) {
        return packet.bytes[0] >> 4 == 11 && (packet.bytes[1] == 64 || packet.bytes[1] == 66 || packet.bytes[1] == 69);
    }
    void cleanup_packet(unsigned output, Packet packet) {
        for (const auto &entry : cleanup_)
            if (entry.output == output && !std::memcmp(entry.packet.bytes, packet.bytes, 3)) return;
        if (cleanup_.size() < limits_.voices * 2 + 16) cleanup_.push_back({output, packet});
    }
    template<class Predicate> void retire_holds(Predicate predicate) {
        for (const auto &claim : holds_) if (predicate(claim)) {
            bool shared = false;
            for (const auto &other : holds_)
                if (!predicate(other) && other.output == claim.output && other.channel == claim.channel && other.controller == claim.controller) shared = true;
            if (!shared) cleanup_packet(claim.output, {11, {uint8_t(0xb0 | claim.channel), claim.controller, 0}});
        }
        erase_if(holds_, predicate);
    }
    static bool same_key(const Voice &a, const Voice &b) {
        return a.output == b.output && (a.packet.bytes[0] & 15) == (b.packet.bytes[0] & 15) && a.packet.bytes[1] == b.packet.bytes[1];
    }
    void owe(const Voice &voice) {
        Packet packet{8, {uint8_t(0x80 | (voice.packet.bytes[0] & 15)), voice.packet.bytes[1], 0}};
        cleanup_packet(voice.output, packet);
    }
    void retire(uint64_t owners) {
        for (unsigned output = 0; output < outputs_.size(); ++output)
            if (clock_claimed_[output] && (clock_owners_[output] & owners)) clock_claimed_[output] = false;
        for (unsigned i = 0; i < routes_.size(); ++i) if (owners & (uint64_t(1) << i)) {
            routes_[i].started = false;
            for (auto &step : routes_[i].steps) if (step.processor) step.processor->stop();
        }
        retire_holds([owners](const Hold &claim) { return bool(claim.owner & owners); });
        for (unsigned output = 0; output < outputs_.size(); ++output)
            if ((sysex_outputs_ & (1u << output)) && (sysex_owners_[output] & owners)) cleanup_packet(output, {5, {0xf7, 0, 0}});
        for (const auto &voice : voices_) if ((voice.owner & owners) && voice.sounding) {
            bool shared = false;
            for (const auto &other : voices_) if (!(other.owner & owners) && other.sounding && same_key(other, voice)) shared = true;
            if (!shared) owe(voice);
        }
        erase_if(voices_, [owners](const Voice &voice) { return bool(voice.owner & owners); });
        erase_if(events_, [owners](const Event &event) { return bool(event.owner & owners); });
        erase_if(captures_, [owners](const Capture &capture) { return bool(capture.work.owner & owners); });
        erase_if(visits_, [owners](const Visit &visit) { return bool(visit.owner & owners); });
    }
    void retire_source(unsigned input) {
        std::vector<uint64_t> stopped;
        for (const auto &visit : visits_) if (visit.input == input && std::find(stopped.begin(), stopped.end(), visit.token) == stopped.end()) stopped.push_back(visit.token);
        for (auto token : stopped) notify_release(token, 0, current_time_);
        for (unsigned output = 0; output < outputs_.size(); ++output)
            if (clock_claimed_[output] && clock_sources_[output] == input) clock_claimed_[output] = false;
        retire_holds([input](const Hold &claim) { return claim.input == input; });
        for (unsigned output = 0; output < outputs_.size(); ++output)
            if ((sysex_outputs_ & (1u << output)) && sysex_sources_[output] == input) cleanup_packet(output, {5, {0xf7, 0, 0}});
        for (const auto &voice : voices_) if (voice.input == input && voice.sounding) {
            bool shared = false;
            for (const auto &other : voices_) if (other.input != input && other.sounding && same_key(other, voice)) shared = true;
            if (!shared) owe(voice);
        }
        erase_if(voices_, [input](const Voice &voice) { return voice.input == input; });
        erase_if(events_, [input](const Event &event) { return event.input == input; });
        erase_if(notes_, [input](const Note &note) { return note.input == input; });
        erase_if(captures_, [input](const Capture &capture) { return capture.work.input == input; });
        if (input < inputs_.size()) inputs_[input].blocked = false;
    }
    void overload(uint64_t owners) {
        if (!owners) return;
        // The final node owns a failure. Upstream nodes may still feed healthy
        // siblings and must remain active.
        while (owners & (owners - 1)) owners &= owners - 1;
        ++diagnostics_.overloaded;
        retire(owners);
        for (unsigned i = 0; i < routes_.size(); ++i) if (owners & (uint64_t(1) << i)) routes_[i].failed = true;
    }
    unsigned used_outputs() const {
        unsigned mask = 0, count = 0;
        for (unsigned i = 0; i < routes_.size(); ++i) if (active(i)) mask |= routes_[i].outputs;
        while (mask) { mask &= mask - 1; ++count; }
        return std::max(1u, count);
    }
    unsigned output_event_limit() const { return std::max(1u, limits_.events / used_outputs()); }
    unsigned output_voice_limit() const { return std::max(1u, limits_.voices / used_outputs()); }
    unsigned output_events(unsigned output) const {
        unsigned count = 0;
        for (const auto &event : events_) if (event.output == output) ++count;
        return count;
    }
    unsigned output_voices(unsigned output) const {
        unsigned count = 0;
        for (const auto &voice : voices_) if (voice.output == output) ++count;
        return count;
    }
    void fail_output(unsigned route, unsigned output) {
        ++diagnostics_.overloaded;
        uint64_t owner = uint64_t(1) << route;
        routes_[route].failed_outputs |= uint16_t(1u << output);
        retire_holds([&](const Hold &hold) { return hold.output == output && bool(hold.owner & owner); });
        for (const auto &voice : voices_) if (voice.output == output && (voice.owner & owner) && voice.sounding) {
            bool shared = false;
            for (const auto &other : voices_) if (!(other.owner & owner) && other.sounding && same_key(other, voice)) shared = true;
            if (!shared) owe(voice);
        }
        erase_if(voices_, [&](const Voice &voice) { return voice.output == output && bool(voice.owner & owner); });
        erase_if(events_, [&](const Event &event) { return event.output == output && bool(event.owner & owner); });
        if ((sysex_outputs_ & (1u << output)) && (sysex_owners_[output] & owner)) cleanup_packet(output, {5, {0xf7,0,0}});
        if (clock_claimed_[output] && (clock_owners_[output] & owner)) clock_claimed_[output] = false;
    }
    bool enqueue(Event event) {
        bool release = event.packet.bytes[0] >> 4 == 8;
        if (events_.size() + (release ? 0 : voices_.size()) >= limits_.events
            || output_events(event.output) + (release ? 0 : output_voices(event.output)) >= output_event_limit()) { ++diagnostics_.dropped; return false; }
        event.sequence = ++serial_;
        auto position = std::upper_bound(events_.begin(), events_.end(), event, [](const Event &a, const Event &b) { return a.due < b.due; });
        events_.insert(position, event); return true;
    }
    void release_note(uint64_t token, uint64_t now, uint8_t velocity, const Packet *source = nullptr) {
        notify_release(token, velocity, now);
        for (auto &capture : captures_) if (capture.work.token == token && !capture.closed) {
            capture.end = now + capture.delay; capture.closed = true;
        }
        std::vector<Event> releases;
        for (const auto &voice : voices_) if (voice.token == token) {
            Packet release = source ? *source : Packet{8, {uint8_t(0x80 | (voice.packet.bytes[0] & 15)), voice.packet.bytes[1], velocity}};
            release.cin = 8; release.bytes[0] = uint8_t(0x80 | (release.bytes[0] & 15));
            // Transform release fields from the original input coordinates, then
            // restore the mapping recorded when its onset was admitted.
            owned_fields(release, voice.owner, voice.input);
            release.cin = 8; release.bytes[0] = uint8_t(0x80 | (voice.packet.bytes[0] & 15)); release.bytes[1] = voice.packet.bytes[1];
            releases.push_back({now + voice.delay, voice.owner, voice.id, 0, voice.input, voice.output, release});
        }
        for (const auto &event : releases) if (!enqueue(event)) {
            unsigned route = 0;
            for (unsigned i = 0; i < routes_.size(); ++i) if (event.owner & (uint64_t(1) << i)) route = i;
            fail_output(route, event.output);
        }
    }
    void notify_release(uint64_t token, uint8_t velocity, uint64_t now) {
        std::vector<Visit> completed;
        for (const auto &visit : visits_) if (visit.token == token) completed.push_back(visit);
        erase_if(visits_, [token](const Visit &visit) { return visit.token == token; });
        for (const auto &visit : completed) {
            Packet release{8, {uint8_t(0x80 | (visit.onset.bytes[0] & 15)), visit.onset.bytes[1], velocity}};
            Work work{release, now, 0, max_routes * (visit.step + 1) + visit.route, visit.owner};
            ProcessorOutput output(*this, visit.route, visit.step + 1, work, limits_.events);
            auto &processor = routes_[visit.route].steps[visit.step].processor;
            if (processor && !processor->push(release, visit.input, output)) { ++diagnostics_.failed; overload(uint64_t(1) << visit.route); }
        }
    }
    static int field(const Packet &packet, unsigned id, unsigned input) {
        unsigned status = packet.bytes[0], kind = status >> 4;
        if (packet.cin >= 4 && packet.cin <= 7 && status != 0xf6) return id == 0 ? 0xf0 : id == 8 ? int(input) : -1;
        switch (id) {
        case 0: return status < 0xf0 ? int(kind) : int(status);
        case 1: return status < 0xf0 ? int((status & 15) + 1) : -1;
        case 2: return kind >= 8 && kind <= 10 ? packet.bytes[1] : -1;
        case 3: return kind == 8 || kind == 9 ? packet.bytes[2] : -1;
        case 4: return kind == 11 ? packet.bytes[1] : -1;
        case 5: return status == 0xf1 || status == 0xf3 ? int(packet.bytes[1]) : status == 0xf2 || kind == 14 ? int(packet.bytes[1] | unsigned(packet.bytes[2]) << 7) : kind == 11 ? packet.bytes[2] : -1;
        case 6: return kind == 12 ? int(packet.bytes[1] + 1) : -1;
        case 7: return kind == 10 ? packet.bytes[2] : kind == 13 ? packet.bytes[1] : -1;
        case 8: return int(input);
        default: return -1;
        }
    }
    static bool matches(const Step &step, const Work &work) {
        for (const auto &match : step.match) {
            int value = field(work.packet, match.id, work.input);
            if (value < 0 || std::find(match.values.begin(), match.values.end(), value) == match.values.end()) return false;
        }
        return true;
    }
    static bool set(Packet &packet, unsigned id, int value) {
        unsigned kind = packet.bytes[0] >> 4;
        if (id == 0) {
            if (value >= 8 && value <= 14) { packet.bytes[0] = uint8_t(value << 4 | (packet.bytes[0] & 15)); packet.cin = uint8_t(value); return true; }
            if (value == 0xf1 || value == 0xf2 || value == 0xf3 || value == 0xf6 || value >= 0xf8) {
                packet.bytes[0] = uint8_t(value); packet.cin = value == 0xf2 ? 3 : value == 0xf1 || value == 0xf3 ? 2 : value == 0xf6 ? 5 : 15; return true;
            }
            return false;
        }
        if (id == 1) { if (value < 1 || value > 16 || kind >= 15) return false; packet.bytes[0] = uint8_t((packet.bytes[0] & 0xf0) | (value - 1)); return true; }
        if (id == 5 && (kind == 14 || packet.bytes[0] == 0xf2)) { value = std::clamp(value, 0, 16383); packet.bytes[1] = uint8_t(value & 127); packet.bytes[2] = uint8_t(value >> 7); return true; }
        if (id == 6) --value;
        if (value < 0 || value > 127) return false;
        if ((id == 2 && kind >= 8 && kind <= 10) || (id == 4 && kind == 11) || (id == 6 && kind == 12) || (id == 7 && kind == 13) || (id == 5 && (packet.bytes[0] == 0xf1 || packet.bytes[0] == 0xf3))) packet.bytes[1] = uint8_t(value);
        else if ((id == 3 && (kind == 8 || kind == 9)) || (id == 5 && kind == 11) || (id == 7 && kind == 10)) packet.bytes[2] = uint8_t(value);
        else return false;
        return true;
    }
    static bool numeric_fields(const Step &step, Work &work) {
        if ((step.kind == 2 || step.kind == 3) && field(work.packet, 2, work.input) >= 0) {
            int pitch = work.packet.bytes[1]; pitch = step.kind == 2 ? pitch + step.value : 2 * step.value - pitch;
            if (pitch < 0 || pitch > 127) return false;
            work.packet.bytes[1] = uint8_t(pitch); return true;
        }
        if (!matches(step, work)) return true;
        if (step.kind == 4) {
            int original = field(work.packet, step.field, work.input);
            if (original < 0) return true;
            int minimum = step.field == 1 || step.field == 6 ? 1 : 0;
            int maximum = step.field == 1 ? 16 : step.field == 6 ? 128 : step.field == 5 && (work.packet.bytes[0] >> 4 == 14 || work.packet.bytes[0] == 0xf2) ? 16383 : 127;
            int low = step.input_low, high = step.input_high;
            if (low == -1 && high == -1) { low = minimum; high = maximum; }
            double fraction = double(std::clamp(original, low, high) - low) / (high - low);
            int value = int(std::floor(step.output_low + fraction * (step.output_high - step.output_low) + 0.5));
            return set(work.packet, step.field, std::clamp(value, minimum, maximum));
        }
        if (step.kind == 5) {
            Packet original = work.packet, mapped = original;
            for (const auto &assignment : step.assignments) {
                if (assignment.field && field(mapped, assignment.field, work.input) < 0) return true;
                int value = assignment.value;
                if (assignment.source != 255) { int source = field(original, assignment.source, work.input); if (source < 0) return true; value += source; }
                if (!set(mapped, assignment.field, value)) return false;
            }
            work.packet = mapped;
        }
        return true;
    }
    bool owned_fields(Packet &packet, uint64_t owner, unsigned input, unsigned stop_route = max_routes, unsigned stop_step = 0) const {
        Work work{packet, 0, 0, input, owner};
        for (unsigned route = 0; route < routes_.size() && route <= stop_route; ++route) if (owner & (uint64_t(1) << route)) {
            unsigned count = route == stop_route ? stop_step : unsigned(routes_[route].steps.size());
            for (unsigned step = 0; step < count; ++step)
                if (!numeric_fields(routes_[route].steps[step], work)) return false;
        }
        packet = work.packet; return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(streams_process)
#endif
    void process(unsigned route, unsigned position, Work work) {
        if (!active(route)) return;
        work.owner |= uint64_t(1) << route;
        auto &entry = routes_[route];
        for (unsigned i = position; i < entry.steps.size(); ++i) {
            auto &step = entry.steps[i];
            if (step.processor) {
                if (work.token && work.packet.bytes[0] >> 4 == 9) {
                    auto visit = std::find_if(visits_.begin(), visits_.end(), [&](const Visit &entry) { return entry.token == work.token && entry.route == route && entry.step == i; });
                    if (visit == visits_.end()) {
                        if (visits_.size() >= limits_.voices) { overload(uint64_t(1) << route); return; }
                        visits_.push_back({work.token, work.owner, route, i, work.input, work.packet});
                    }
                }
                ProcessorOutput output(*this, route, i + 1, work, limits_.events);
                if (!step.processor->push(work.packet, work.input, output)) { ++diagnostics_.failed; overload(work.owner); }
                return;
            }
            bool match = matches(step, work);
            if ((step.kind == 0 && !match) || (step.kind == 1 && match)) return;
            if ((step.kind == 2 || step.kind == 3) && field(work.packet, 2, work.input) >= 0) {
                int pitch = work.packet.bytes[1];
                pitch = step.kind == 2 ? pitch + step.value : 2 * step.value - pitch;
                if (pitch < 0 || pitch > 127) return;
                work.packet.bytes[1] = uint8_t(pitch);
            } else if (step.kind == 4 || step.kind == 5) {
                if (!numeric_fields(step, work)) return;
            } else if (step.kind == 6) {
                for (unsigned copy = 1; copy <= step.repeats; ++copy) {
                    Work echo = work; echo.due += step.time * copy;
                    if (echo.packet.bytes[0] >> 4 == 9) {
                        int velocity = int(std::floor(echo.packet.bytes[2] * std::pow(step.feedback, copy) + 0.5));
                        if (velocity <= 0) continue;
                        echo.packet.bytes[2] = uint8_t(std::min(127, velocity));
                    }
                    process(route, i + 1, echo);
                }
                return;
            } else if (step.kind == 7) {
                if (captures_.size() >= limits_.captures) { overload(work.owner); return; }
                Capture capture{work, route, i, work.due};
                capture.delay = work.due - current_time_;
                bool note = work.packet.bytes[0] >> 4 == 9 && work.token;
                unsigned cin = work.packet.cin;
                bool sysex = cin == 4 || (cin >= 5 && cin <= 7
                    && work.packet.bytes[work.packet.size() - 1] == 0xf7);
                capture.closed = !note && !sysex;
                capture.end = capture.closed ? work.due : 0;
                if (sysex) {
                    for (const auto &previous : captures_)
                        if (previous.route == route && previous.step == i
                            && previous.work.input == work.input && previous.frame && !previous.closed)
                            capture.frame = previous.frame;
                    if (work.packet.bytes[0] == 0xf0) {
                        if (capture.frame) { overload(work.owner); return; }
                        capture.frame = ++serial_;
                    }
                    if (!capture.frame) { ++diagnostics_.dropped; return; }
                    if (cin != 4) {
                        capture.closed = true; capture.end = work.due;
                        for (auto &previous : captures_) if (previous.frame == capture.frame) {
                            previous.closed = true; previous.end = work.due;
                        }
                    }
                }
                captures_.push_back(capture); return;
            }
        }
        if (work.packet.bytes[0] >> 4 == 9 && !work.packet.bytes[2]) return;
        for (unsigned output = 0; output < outputs_.size(); ++output) if (entry.outputs & (1u << output)) {
            if (entry.failed_outputs & (1u << output)) continue;
            if (!outputs_[output].present || recovering(output)) { ++diagnostics_.dropped; continue; }
            uint64_t voice = 0;
            if (work.packet.bytes[0] >> 4 == 9 && work.token) {
                if (voices_.size() >= limits_.voices || output_voices(output) >= output_voice_limit()
                    || events_.size() + voices_.size() + 2 >= limits_.events
                    || output_events(output) + output_voices(output) + 2 >= output_event_limit()) { fail_output(route, output); continue; }
                voice = ++serial_;
                voices_.push_back({voice, work.token, work.owner, work.due - current_time_, work.input, output, work.packet, false});
            }
            if (!enqueue({work.due, work.owner, voice, 0, work.input, output, work.packet})) { fail_output(route, output); continue; }
        }
        for (unsigned downstream = route + 1; downstream < routes_.size(); ++downstream) if (active(downstream))
            for (const auto &source : routes_[downstream].inputs) if (source.kind && source.index == route) { process(downstream, 0, work); break; }
    }
    uint64_t current_time_ = 0;
    void reverse_group(unsigned route, unsigned step, uint64_t now, bool finite) {
        uint64_t window = routes_[route].steps[step].time;
        uint64_t begin = finite ? routes_[route].next : UINT64_MAX;
        for (const auto &capture : captures_) if (capture.route == route && capture.step == step) {
            uint64_t time = capture.frame ? capture.end : capture.start;
            if (capture.frame && !capture.closed) continue;
            if (!finite) begin = std::min(begin, time / window * window);
        }
        if (begin == UINT64_MAX || now < begin) return;
        uint64_t boundary = finite ? now : begin + window;
        if (now < boundary) return;
        // A service stall cannot turn into an unbounded burst of old windows.
        if (!finite && now - boundary >= window) { overload(uint64_t(1) << route); return; }
        std::vector<TimelinePacket> sequence;
        for (auto capture = captures_.begin(); capture != captures_.end();) {
            if (capture->route != route || capture->step != step
                || (capture->frame && !capture->closed)) { ++capture; continue; }
            uint64_t time = capture->frame ? capture->end : capture->start;
            if (time < begin || (!finite && time >= boundary)) { ++capture; continue; }
            TimelinePacket onset{time - begin, capture->work.packet, capture->work.input,
                capture->work.token, capture->work.owner};
            sequence.push_back(onset);
            bool note = onset.packet.bytes[0] >> 4 == 9 && onset.token;
            if (note) {
                uint64_t end = capture->closed ? std::min(capture->end, boundary) : boundary;
                TimelinePacket release = onset; release.time = end - begin;
                release.packet.cin = 8;
                release.packet.bytes[0] = uint8_t(0x80 | (onset.packet.bytes[0] & 15));
                release.packet.bytes[2] = 0;
                sequence.push_back(release);
                if (!finite && (!capture->closed || capture->end > boundary)) {
                    capture->start = boundary; ++capture; continue;
                }
            }
            capture = captures_.erase(capture);
        }
        if (sequence.empty()) return;
        std::stable_sort(sequence.begin(), sequence.end(), [](const TimelinePacket &a, const TimelinePacket &b) { return a.time < b.time; });
        if (!reverse_sequence(sequence, boundary - begin, limits_.captures * 2)) {
            overload(uint64_t(1) << route); return;
        }
        struct Token { uint64_t old, fresh; };
        std::vector<Token> tokens;
        for (const auto &event : sequence) {
            Work reversed{event.packet, boundary + event.time, event.token, event.source, event.owner};
            unsigned kind = event.packet.bytes[0] >> 4;
            if (kind == 9 && event.token) {
                reversed.token = ++serial_; tokens.push_back({event.token, reversed.token});
            } else if (kind == 8 && event.token) {
                auto token = std::find_if(tokens.begin(), tokens.end(), [&](const Token &entry) { return entry.old == event.token; });
                if (token == tokens.end()) continue;
                release_note(token->fresh, reversed.due, event.packet.bytes[2]);
                tokens.erase(token); continue;
            }
            auto previous = current_time_; current_time_ = reversed.due;
            process(route, step + 1, reversed);
            current_time_ = previous;
        }
    }
public:
    void finish_reverse(unsigned owner_route, uint64_t now) {
        for (unsigned route = 0; route < routes_.size(); ++route)
            for (unsigned step = 0; step < routes_[route].steps.size(); ++step)
                if (routes_[route].steps[step].kind == 7 && !routes_[route].steps[step].time) {
                    bool owned = false;
                    for (const auto &capture : captures_) if (capture.route == route && capture.step == step
                        && (capture.work.owner & (uint64_t(1) << owner_route))) owned = true;
                    if (owned) reverse_group(route, step, now, true);
                }
    }
private:
    void flush_reverse(uint64_t now) {
        uint64_t timed_out = 0;
        for (const auto &capture : captures_) if (capture.frame && !capture.closed) {
            uint64_t latest = capture.start;
            for (const auto &other : captures_) if (other.frame == capture.frame) latest = std::max(latest, other.start);
            if (now >= latest && now - latest >= 1000000) timed_out |= uint64_t(1) << capture.route;
        }
        for (unsigned route = 0; route < routes_.size(); ++route)
            if (timed_out & (uint64_t(1) << route)) overload(uint64_t(1) << route);
        for (unsigned route = 0; route < routes_.size(); ++route)
            for (unsigned step = 0; step < routes_[route].steps.size(); ++step)
                if (routes_[route].steps[step].kind == 7 && routes_[route].steps[step].time)
                    reverse_group(route, step, now, false);
    }
    struct Reader {
        const uint8_t *cursor, *end;
        bool ok = true;
        unsigned byte() { if (cursor == end) { ok = false; return 0; } return *cursor++; }
        uint64_t integer(unsigned count) { uint64_t value = 0; for (unsigned i = 0; i < count; ++i) value |= uint64_t(byte()) << (i * 8); return value; }
        bool match(Step &step) {
            unsigned count = byte();
            if (count > 9) return false;
            unsigned fields = 0;
            for (unsigned i = 0; i < count; ++i) {
                Field field; field.id = uint8_t(byte()); unsigned values = byte();
                if (field.id > 8 || !values || (fields & (1u << field.id))) return false;
                fields |= 1u << field.id;
                for (unsigned j = 0; j < values; ++j) field.values.push_back(int32_t(integer(4)));
                step.match.push_back(std::move(field));
            }
            return ok;
        }
    };
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(streams_decode)
#endif
    bool decode(const uint8_t *data, unsigned size, Outputs outputs, unsigned nodes) {
        if (!data && !size) return true;
        if (!data || size < 12 || std::memcmp(data, "CSTR", 4)) return false;
        Reader reader{data + 4, data + size};
        if (reader.integer(4) != size) return false;
        unsigned count = unsigned(reader.integer(2)), flags = unsigned(reader.integer(2));
        if (count > max_routes || flags > 1) return false;
        explicit_system_ = flags;
        for (unsigned i = 0; i < count; ++i) {
            Route route; route.scope = uint16_t(reader.integer(2)); unsigned inputs = unsigned(reader.integer(2));
            route.outputs = uint16_t(reader.integer(2)); unsigned steps = unsigned(reader.integer(2));
            if ((route.scope != system_scope && route.scope >= nodes) || inputs > 64 || steps > 17 || (route.outputs & ~outputs.mask())) return false;
            bool name_end = false;
            for (unsigned j = 0; j < 64; ++j) { unsigned c = reader.byte(); route.name[j] = char(c); if (!c) name_end = true; else if (name_end || c < 32 || c == '/') return false; }
            if (!name_end) return false;
            if (outputs.find(route.name)) return false;
            for (const auto &existing : routes_) if (existing.scope == route.scope && !std::strcmp(existing.name, route.name)) return false;
            for (unsigned j = 0; j < inputs; ++j) {
                Input input; input.kind = uint8_t(reader.byte()); input.index = uint16_t(reader.integer(2));
                if (input.kind > 1 || (input.kind ? input.index >= i : input.index >= 16 || !(outputs.mask(true) & (1u << input.index)))) return false;
                if (input.kind && routes_[input.index].scope != system_scope && routes_[input.index].scope != route.scope) return false;
                route.inputs.push_back(input);
            }
            for (unsigned j = 0; j < steps; ++j) {
                Step step; step.kind = uint8_t(reader.byte()); unsigned reserved = reader.byte(), length = unsigned(reader.integer(2));
                if (!reader.ok || length > unsigned(reader.end - reader.cursor) || (step.kind < 128 && (reserved || step.kind > 9))) return false;
                Reader payload{reader.cursor, reader.cursor + length}; reader.cursor += length;
                if (step.kind >= 128) {
                    for (const auto &registration : registry()) if (registration.opcode == step.kind && registration.version == reserved) {
                        if (registration.generator != (j == 0 && !inputs)) return false;
                        step.processor = registration.factory(payload.cursor, length);
                    }
                    if (!step.processor) return false;
                    payload.cursor = payload.end;
                } else if (step.kind == 0 || step.kind == 1) { if (!payload.match(step)) return false; }
                else if (step.kind == 2 || step.kind == 3) {
                    step.value = int32_t(payload.integer(4));
                    if (step.value < (step.kind == 2 ? -127 : 0) || step.value > 127) return false;
                }
                else if (step.kind == 4) {
                    if (!payload.match(step)) return false;
                    step.field = uint8_t(payload.byte()); step.input_low = int32_t(payload.integer(4)); step.input_high = int32_t(payload.integer(4));
                    step.output_low = int32_t(payload.integer(4)); step.output_high = int32_t(payload.integer(4));
                    int minimum = step.field == 6 ? 1 : 0, maximum = step.field == 6 ? 128 : step.field == 5 ? 16383 : 127;
                    bool implicit = step.input_low == -1 && step.input_high == -1;
                    if ((step.field != 2 && step.field != 3 && step.field != 5 && step.field != 6 && step.field != 7)
                        || (!implicit && (step.input_low < minimum || step.input_high > maximum || step.input_low >= step.input_high))
                        || step.output_low < minimum || step.output_high < minimum || step.output_low > maximum || step.output_high > maximum) return false;
                } else if (step.kind == 5) {
                    if (!payload.match(step)) return false;
                    unsigned assignments = payload.byte();
                    if (!assignments || assignments > 8) return false;
                    for (unsigned k = 0; k < assignments; ++k) {
                        Assignment assignment; assignment.field = uint8_t(payload.byte()); assignment.source = uint8_t(payload.byte()); assignment.value = int32_t(payload.integer(4));
                        if (assignment.field > 7 || (assignment.source > 7 && assignment.source != 255)) return false;
                        if (assignment.value < -16383 || assignment.value > 16383) return false;
                        if (assignment.source != 255 && (!assignment.source || assignment.field == 0)) return false;
                        if (assignment.source == 255) {
                            int minimum = assignment.field == 1 || assignment.field == 6 ? 1 : 0;
                            int maximum = assignment.field == 1 ? 16 : assignment.field == 6 ? 128 : assignment.field == 5 ? 16383 : 127;
                            if (assignment.field == 0) {
                                int type = assignment.value;
                                if (!((type >= 8 && type <= 14) || type == 0xf1 || type == 0xf2 || type == 0xf3 || type == 0xf6 || type == 0xf8 || type == 0xfa || type == 0xfb || type == 0xfc || type == 0xfe || type == 0xff)) return false;
                            } else if (assignment.value < minimum || assignment.value > maximum) return false;
                        }
                        step.assignments.push_back(assignment);
                    }
                } else if (step.kind == 6) {
                    step.time = payload.integer(8); step.repeats = unsigned(payload.integer(2)); uint32_t bits = uint32_t(payload.integer(4)); float feedback; std::memcpy(&feedback, &bits, 4); step.feedback = feedback;
                    if (!step.time || step.time > 3600000000ull || !step.repeats || step.repeats > 64 || !std::isfinite(feedback) || feedback < 0 || feedback > 1) return false;
                } else if (step.kind == 7) { step.time = payload.integer(8); if (step.time > 3600000000ull) return false; }
                else if (step.kind == 8) {
                    uint64_t bits = payload.integer(8); std::memcpy(&step.bpm, &bits, 8);
                    if (!std::isfinite(step.bpm) || step.bpm <= 0 || step.bpm > 1000 || j || inputs) return false;
                } else {
                    step.parameters.assign(payload.cursor, payload.end); payload.cursor = payload.end;
                    if (j || inputs || length != 126 || !std::memchr(step.parameters.data(), 0, 96)) return false;
                    bool ended = false;
                    for (unsigned k = 0; k < 96; ++k) {
                        unsigned c = step.parameters[k];
                        if (!c) ended = true;
                        else if (ended || c < 32 || c > 126 || c == '/' || c == '\\') return false;
                    }
                    if (!std::strcmp(reinterpret_cast<const char *>(step.parameters.data()), ".") || !std::strcmp(reinterpret_cast<const char *>(step.parameters.data()), "..")) return false;
                    Reader settings{step.parameters.data() + 96, step.parameters.data() + step.parameters.size()};
                    settings.integer(4);
                    uint64_t pre = settings.integer(8), post = settings.integer(8), bits = settings.integer(8);
                    double speed; std::memcpy(&speed, &bits, 8);
                    if (pre % 1000000 || post % 1000000 || pre / 1000000 > UINT32_MAX || post / 1000000 > UINT32_MAX || !std::isfinite(speed) || speed <= 0 || settings.byte() > 1) return false;
                    int transpose = int8_t(settings.byte()); if (transpose == -128) return false;
                }
                if (!payload.ok || payload.cursor != payload.end) return false;
                route.steps.push_back(std::move(step));
            }
            if (!inputs && (route.steps.empty() || route.steps[0].kind < 8)) return false;
            routes_.push_back(std::move(route));
        }
        return reader.ok && reader.cursor == reader.end;
    }
};
} // namespace cntrl::midi
