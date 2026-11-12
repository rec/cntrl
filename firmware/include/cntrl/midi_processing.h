#pragma once
#include "midi_bridge.h"
#include "midi_file.h"
#include "midi_stream_timeline.h"
#include "midi_lfo.h"
#include "checked_array.h"
#include "checked_list.h"
#include "input_notes.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace cntrl::midi {
// The wire section is shared with patch_switcher/midi_processing.py.
// All retained traffic has an explicit owner and a bounded storage allowance.
class Streams : public ParameterObserver {
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
        return registry().append({opcode, version, factory, generator}) != nullptr;
    }
    static constexpr unsigned system_scope = 65535, max_routes = UINT16_MAX;
    struct Limits { unsigned events = UINT32_MAX, voices = UINT32_MAX, notes = UINT32_MAX, captures = UINT32_MAX; };
    struct Diagnostics { uint32_t dropped = 0, overloaded = 0, transmitted = 0, failed = 0, coalesced = 0, skipped = 0; };
    struct LfoStatus { unsigned running = 0, pending = 0, stopping = 0, waiting = 0, missing = 0, failed_count = 0; uint64_t failed = 0; };
    struct Field { uint8_t id = 0; CheckedArray<int32_t> values; };
    struct Assignment { uint8_t field = 0, source = 255; int32_t value = 0; };
    struct Step {
        uint8_t kind = 0, field = 0;
        CheckedArray<Field> match;
        CheckedArray<Assignment> assignments;
        int32_t value = 0, input_low = 0, input_high = 127, output_low = 0, output_high = 127;
        uint64_t time = 0;
        unsigned repeats = 1;
        double feedback = 1, bpm = 120;
        CheckedArray<uint8_t> parameters;
        std::unique_ptr<Processor> processor;
    };
    struct Input { uint8_t kind = 0; uint16_t index = 0; };
    struct Route {
        char name[64] = {};
        uint16_t scope = system_scope, outputs = 0;
        uint16_t failed_outputs = 0;
        CheckedArray<Input> inputs;
        CheckedArray<Step> steps;
        bool enabled = true, failed = false;
        uint64_t next = 0;
        StreamClock clock;
        std::unique_ptr<StreamFileSource> file;
        bool started = false;
        bool foreign = false;
        bool note_admission_closed = false;
        bool activated = false;
        CheckedObject<Lfo> lfo;
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
    unsigned input_quarantine(unsigned input) const {
        unsigned mask = 0;
        for (unsigned cable = 0; cable < 16; ++cable)
            if (input_notes_.quarantined(input, uint16_t(input * 16 + cable))) mask |= 1u << cable;
        return mask;
    }
    void file_provider(StreamFileProvider *provider) { file_provider_ = provider; }
    bool file_sources_open() const {
        for (const auto &route : routes_) if (route.file) return true;
        return false;
    }
    void beat_position(BeatPosition position) { beat_ = position; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(lfo_status)
#endif
    LfoStatus lfo_status() const {
        LfoStatus result;
        for (unsigned i = 0; i < routes_.size(); ++i) if (routes_[i].lfo) {
            if (routes_[i].failed) { ++result.failed_count; if (i < 64) result.failed |= uint64_t(1) << i; }
            if (active(i) && routes_[i].started && !routes_[i].lfo->ended) {
                if (routes_[i].lfo->waiting) ++result.waiting;
                else ++result.running;
            }
        }
        for (const auto &delivery : parameters_) {
            if (!delivery.finished && !delivery.initial) ++result.pending;
            if (!delivery.finished && delivery.stopping) ++result.stopping;
            if (!outputs_[delivery.output].present) ++result.missing;
        }
        return result;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(streams_retained_bytes)
#endif
    size_t retained_bytes() const {
        size_t bytes = routes_.capacity() * sizeof(Route) + parsers_.capacity() * sizeof(Parser)
            + (inputs_.capacity() + outputs_.capacity()) * sizeof(Connection)
            + input_notes_.memory_bytes() + voices_.memory_bytes() + processing_.memory_bytes() + processing_release_tokens_.memory_bytes()
            + events_.memory_bytes() + cleanup_.memory_bytes()
            + holds_.memory_bytes() + captures_.memory_bytes()
            + reverse_parameters_.memory_bytes() + visits_.memory_bytes()
            + parameters_.memory_bytes() + bindings_.memory_bytes()
            + owner_generations_.memory_bytes()
            + binding_targets_.capacity() * sizeof(Parameter) + binding_owners_.capacity() * sizeof(uint64_t)
            + downstream_reached_.capacity()
            + parameter_cursor_.capacity() * sizeof(unsigned) + sysex_owners_.capacity() * sizeof(uint64_t)
            + sysex_sources_.capacity() * sizeof(unsigned) + clock_claimed_.capacity() * sizeof(bool)
            + clock_sources_.capacity() * sizeof(unsigned) + clock_owners_.capacity() * sizeof(uint64_t);
        for (const auto &generation : owner_generations_)
            bytes += generation.paths.capacity() * sizeof(OwnerPath) + generation.offsets.capacity() * sizeof(unsigned);
        for (const auto &route : routes_) {
            bytes += route.inputs.capacity() * sizeof(Input) + route.steps.capacity() * sizeof(Step);
            if (route.lfo) bytes += sizeof(Lfo);
            for (const auto &step : route.steps) {
                bytes += step.match.capacity() * sizeof(Field) + step.assignments.capacity() * sizeof(Assignment)
                    + step.parameters.capacity();
                for (const auto &field : step.match) bytes += field.values.capacity() * sizeof(int32_t);
            }
        }
        return bytes;
    }
    bool parameter_write(unsigned output, const uint8_t *bytes, unsigned size, const Parameter *logical, uint64_t origin) override {
        bool blocked = false;
        for (auto &delivery : parameters_) if (delivery.output == output && !delivery.finished && (!origin || delivery.lfo != origin)
            && (logical ? delivery.target.overlaps(*logical) || (logical->kind == Parameter::program && delivery.target.channel == logical->channel)
                : delivery.target.conflicts(bytes, size))) {
            bool bank = size >= 3 && bytes[0] >> 4 == 11 && (bytes[1] == 0 || bytes[1] == 32);
            stop_parameter(delivery, false);
            if (bank && delivery.target.kind == Parameter::program) delivery.off = 0;
            if (delivery.touched || delivery.flight.position) blocked = true;
        }
        return !blocked;
    }
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
        if (!prepare_load(data, size, outputs, nodes, now)) return false;
        finish_load(true);
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(streams_prepare_load)
#endif
    bool prepare_load(const uint8_t *data, unsigned size, Outputs outputs, unsigned nodes, uint64_t now = 0) {
        finish_load(false);
        if (!prepared_.create()) return false;
        auto &candidate = *prepared_;
        candidate.limits_ = limits_;
        if (!candidate.decode(data, size, outputs, nodes)) { prepared_.reset(); return false; }
        if (!candidate.parsers_.resize(outputs.route_count()) || !candidate.inputs_.resize(outputs.route_count())
            || !candidate.outputs_.resize(outputs.route_count()) || !candidate.prepare_output_state(outputs.route_count())
            || !candidate.input_notes_.configure(outputs.route_count())) { prepared_.reset(); return false; }
        if (candidate.owner_generation_) {
            prepared_paths_ = owner_generations_.append(std::move(*candidate.owner_generation_));
            if (!prepared_paths_) { prepared_.reset(); return false; }
        }
        prepared_time_ = now;
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(streams_finish_load)
#endif
    void finish_load(bool commit) {
        if (!prepared_) return;
        if (!commit) {
            owner_generations_.erase_if([&](const OwnerGeneration &generation) { return &generation == prepared_paths_; });
            prepared_paths_ = nullptr;
            prepared_.reset();
            return;
        }
        auto &candidate = *prepared_;
        // A graph change does not replace the physical endpoint session. Keep
        // identities current through publication so queued terminal work survives
        // the next connection poll.
        for (unsigned i = 0; i < std::min(inputs_.size(), candidate.inputs_.size()); ++i) candidate.inputs_[i] = inputs_[i];
        for (unsigned i = 0; i < std::min(outputs_.size(), candidate.outputs_.size()); ++i) candidate.outputs_[i] = outputs_[i];
        stop_all();
        owner_generation_ = prepared_paths_;
        prepared_paths_ = nullptr;
        routes_ = std::move(candidate.routes_);
        binding_targets_ = std::move(candidate.binding_targets_);
        binding_owners_ = std::move(candidate.binding_owners_);
        downstream_reached_ = std::move(candidate.downstream_reached_);
        bindings_ = std::move(candidate.bindings_);
        processing_ = std::move(candidate.processing_);
        base_routes_ = unsigned(routes_.size()); foreign_signature_ = 0; external_selected_ = false;
        explicit_system_ = candidate.explicit_system_;
        selected_ = system_scope;
        parsers_ = std::move(candidate.parsers_);
        inputs_ = std::move(candidate.inputs_);
        outputs_ = std::move(candidate.outputs_);
        input_notes_ = std::move(candidate.input_notes_);
        parameter_cursor_ = std::move(candidate.parameter_cursor_);
        sysex_owners_ = std::move(candidate.sysex_owners_);
        sysex_sources_ = std::move(candidate.sysex_sources_);
        clock_claimed_ = std::move(candidate.clock_claimed_);
        clock_sources_ = std::move(candidate.clock_sources_);
        clock_owners_ = std::move(candidate.clock_owners_);
        outputs_count_ = outputs_.size();
        for (auto &route : routes_) {
            route.next = prepared_time_;
            if (route.lfo) route.lfo->scope(prepared_time_, beat_);
        }
        for (auto &route : routes_) route.activated = true;
        activation_pending_ = true;
        prepared_.reset();
    }
    void select(unsigned scope, uint64_t now = 0) {
        if (scope == selected_ && !external_selected_) return;
        for (unsigned i = 0; i < routes_.size(); ++i)
            if (routes_[i].scope != system_scope) { retire(i); routes_[i].file.reset(); }
        selected_ = scope;
        if (external_selected_) { routes_.resize(base_routes_); external_selected_ = false; }
        for (auto &route : routes_) if (route.scope == scope) {
            route.enabled = true; route.failed = false; route.failed_outputs = 0; route.next = now; route.started = false;
            if (route.lfo) route.lfo->scope(now, beat_);
        }
        for (auto &route : routes_) if (route.scope == scope) { route.activated = true; route.note_admission_closed = false; }
        activation_pending_ = true;
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
        CheckedArray<unsigned> mapping;
        if (!mapping.resize(candidate.routes_.size()) || !routes_.reserve(base_routes_ + local_count)) return false;
        std::fill(mapping.begin(), mapping.end(), max_routes);
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
        CheckedArray<Route *> merged;
        if (!merged.reserve(base_routes_ + local_count)) return false;
        for (unsigned i = 0; i < base_routes_; ++i) if (!merged.append(&routes_[i])) return false;
        for (auto &route : candidate.routes_) if (route.scope == scope) {
            for (auto &input : route.inputs) if (input.kind) input.index = uint16_t(mapping[input.index]);
            if (!merged.append(&route)) return false;
        }
        auto *old_generation = owner_generation_;
        if (!build_owners(merged.size(), [&](unsigned i) -> const Route & { return *merged[i]; })) return false;
        auto *prepared_paths = owner_generation_; owner_generation_ = old_generation;
        CheckedArray<Parameter> targets;
        CheckedArray<uint64_t> owners;
        CheckedArray<uint8_t> reached;
        if (!targets.resize(merged.size()) || !owners.resize(merged.size()) || !reached.resize(merged.size())) return false;
        CheckedList<Processing> processing;
        if (!prepare_processing(processing, merged.size(), [&](unsigned i) -> const Route & { return *merged[i]; })) return false;
        for (unsigned i = 0; i < routes_.size(); ++i) if (routes_[i].scope != system_scope) { retire(i); routes_[i].file.reset(); }
        owner_generation_ = prepared_paths;
        binding_targets_ = std::move(targets); binding_owners_ = std::move(owners); downstream_reached_ = std::move(reached);
        processing_ = std::move(processing);
        routes_.resize(base_routes_);
        for (auto &route : candidate.routes_) if (route.scope == scope) {
            route.foreign = true; route.next = now;
            if (route.lfo) route.lfo->scope(now, beat_);
            if (!routes_.append(std::move(route))) return false;
            routes_.back().activated = true;
        }
        activation_pending_ = true;
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
        if (!enabled) retire(route);
        routes_[route].file.reset(); routes_[route].started = false;
        routes_[route].enabled = enabled;
        if (enabled) { routes_[route].failed = false; routes_[route].failed_outputs = 0; }
        if (enabled) { routes_[route].activated = true; routes_[route].note_admission_closed = false; activation_pending_ = true; }
    }
    void stop_all() {
        retire(UINT32_MAX);
        for (auto &route : routes_) { route.file.reset(); route.started = false; }
        input_notes_.clear(); processing_release_tokens_.clear(); captures_.clear(); events_.clear(); reverse_parameters_.clear(); bindings_.clear();
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
        for (auto &delivery : parameters_) if (delivery.output == output && !delivery.finished) {
            stop_parameter(delivery, false);
            if (sink_) sink_->abandon_parameter(output, delivery.flight);
            if (replacement) delivery.finished = true;
        }
        if (sink_) sink_->forget_output(output);
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
    void byte(unsigned input, uint8_t byte, uint64_t now, unsigned cable = 0) {
        if (input >= parsers_.size()) return;
        Packet packet;
        if (parsers_[input].receive(byte, packet)) { packet.source = uint16_t(input * 16 + cable); receive(input, packet, now); }
    }
    void receive(unsigned input, Packet packet, uint64_t now) {
        if (packet.source == 65535) packet.source = uint16_t(input * 16);
        if (input >= inputs_.size() || !inputs_[input].present) return;
        current_time_ = now;
        refresh_output_divisor();
        unsigned kind = packet.bytes[0] >> 4;
        bool onset = kind == 9 && packet.bytes[2];
        bool release = kind == 8 || (kind == 9 && !packet.bytes[2]);
        uint64_t token = 0;
        if (onset) {
            if (!admit_note(input, packet, token)) return;
        } else if (release) {
            release_input_note(input, packet, now);
            return;
        } else if (kind == 10) {
            CheckedArray<Capture> pressure_captures;
            if (!pressure_captures.reserve(captures_.size())) { ++diagnostics_.dropped; return; }
            for (const auto &note : input_notes_.strikes())
                if (note.input == input && note.channel == (packet.bytes[0] & 15) && note.pitch == packet.bytes[1]) {
                    for (const auto &voice : voices_) if (voice.token == note.token) {
                        Packet pressure = packet;
                        if (!owned_fields(pressure, voice.owner, input)) continue;
                        if (pressure.bytes[0] >> 4 == 10) { pressure.bytes[0] = uint8_t(0xa0 | (voice.packet.bytes[0] & 15)); pressure.bytes[1] = voice.packet.bytes[1]; }
                        enqueue({now + voice.delay, voice.owner, 0, 0, input, voice.output, pressure});
                    }
                    for (const auto &capture : captures_) if (capture.work.token == note.token && capture.work.packet.bytes[0] >> 4 == 9)
                        if (!pressure_captures.append(capture)) { ++diagnostics_.dropped; return; }
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
    void service(Sink &sink, uint64_t now, unsigned available = ~0u, unsigned busy = 0, unsigned budget = 64, unsigned exclusive = 0) {
        current_time_ = now;
        sink_ = &sink; sink.parameter_observer(this);
        refresh_output_divisor();
        unsigned used = 0;
        bool modulation = false;
        for (unsigned i = 0; i < routes_.size(); ++i) if (routes_[i].lfo && active(i)) modulation = true;
        for (const auto &delivery : parameters_) if (!delivery.finished) modulation = true;
        unsigned work_budget = modulation ? budget - std::min(budget / 2, unsigned(outputs_.size())) : budget;
        service_lfos(now, used, work_budget);
        for (auto cleanup = cleanup_.begin(); cleanup != cleanup_.end() && used < budget;) {
            unsigned bit = 1u << cleanup->output;
            if (!(available & bit) || (busy & bit) || !outputs_[cleanup->output].present) { ++cleanup; continue; }
            ++used;
            Packet packet = cleanup->packet;
            if (packet.cin == 8 || packet.cin == 9 || is_hold(packet)) packet.source = 65533;
            if (!sink.submit(cleanup->output, packet)) { ++cleanup; continue; }
            ++diagnostics_.transmitted;
            if (cleanup->packet.bytes[0] == 0xf7) sysex_outputs_ &= ~(1u << cleanup->output);
            cleanup = cleanup_.erase(cleanup);
        }
        for (unsigned i = 0; i < routes_.size(); ++i) if (active(i) && !routes_[i].steps.empty() && routes_[i].steps[0].kind == 8) {
            auto &route = routes_[i];
            if (!route.started) { route.clock.start(now, unsigned(std::llround(route.steps[0].bpm * 1000))); route.started = true; }
            if (route.clock.poll(now) && used < work_budget) {
                Work work{{15, {0xf8, 0, 0}}, now, 0, system_scope, 0};
                process(i, 1, work); ++used;
            }
        }
        for (unsigned i = 0; i < routes_.size() && used < work_budget; ++i) if (active(i) && !routes_[i].steps.empty() && routes_[i].steps[0].kind == 9) {
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
                if (route.file->failed()) { retire(i); route.failed = true; ++diagnostics_.failed; }
                else if (route.file->finished()) { finish_reverse(i, now); route.file.reset(); }
            }
        }
        for (unsigned i = 0; i < routes_.size() && used < work_budget; ++i) if (active(i)) {
            for (unsigned position = 0; position < routes_[i].steps.size() && used < work_budget; ++position) {
                auto &step = routes_[i].steps[position];
                if (!step.processor) continue;
                Work work{{}, now, 0, max_routes * (position + 1) + i, extend_owner(0, i)};
                ProcessorOutput output(*this, i, position + 1, work, work_budget - used);
                step.processor->service(now, work_budget - used, output); ++used;
            }
        }
        flush_reverse(now);
        unsigned delivered = 0;
        unsigned attempted = 0;
        for (auto event = events_.begin(); event != events_.end() && used < work_budget;) {
            if (event->due > now) { ++event; continue; }
            if (event->parameter.kind) {
                accept_parameter(*event);
                event = events_.erase(event); ++used; continue;
            }
            unsigned bit = 1u << event->output;
            if (((delivered | attempted) & bit) || !(available & bit) || (busy & bit) || recovering(event->output) || !outputs_[event->output].present) { ++event; continue; }
            bool realtime = event->packet.cin == 15;
            if ((sysex_outputs_ & bit) && !realtime && (!same_owner(sysex_owners_[event->output], event->owner) || sysex_sources_[event->output] != event->input)) { ++event; continue; }
            bool blocked_before = false;
            for (auto before = events_.begin(); before != event; ++before)
                if (before->output == event->output && before->due <= event->due
                    && (!(sysex_outputs_ & bit) || (same_owner(before->owner, sysex_owners_[event->output]) && before->input == sysex_sources_[event->output]))) { blocked_before = true; break; }
            if (blocked_before) { ++event; continue; }
            ++used;
            Voice *voice = nullptr;
            for (auto &entry : voices_) if (entry.id == event->voice) { voice = &entry; break; }
            bool onset = event->packet.bytes[0] >> 4 == 9 && event->packet.bytes[2];
            bool release = event->packet.bytes[0] >> 4 == 8;
            bool write = true;
            bool clock = event->packet.bytes[0] == 0xf8 || (event->packet.bytes[0] >= 0xfa && event->packet.bytes[0] <= 0xfc);
            if (clock && (external_clock_ & bit)) write = false;
            if (clock && clock_claimed_[event->output] && (clock_sources_[event->output] != event->input || !same_owner(clock_owners_[event->output], event->owner))) write = false;
            if (voice && onset) {
                for (const auto &other : voices_) if (other.id != voice->id && other.sounding && same_key(other, *voice) && (!same_owner(other.owner, voice->owner) || other.input != voice->input)) write = false;
            }
            if (voice && release) {
                if (!voice->sounding) write = false;
                for (const auto &other : voices_) if (other.id != voice->id && other.sounding && same_key(other, *voice) && (!same_owner(other.owner, voice->owner) || other.input != voice->input)) write = false;
            }
            bool hold = is_hold(event->packet);
            bool holding = hold && event->packet.bytes[2] >= 64;
            bool own_hold = false;
            if (hold) for (const auto &claim : holds_)
                if (claim.output == event->output && claim.channel == (event->packet.bytes[0] & 15) && claim.controller == event->packet.bytes[1] && same_owner(claim.owner, event->owner) && claim.input == event->input) own_hold = true;
            if (holding && !own_hold && (holds_.size() >= limits_.voices || !holds_.reserve(holds_.size() + 1)
                || !cleanup_.reserve(cleanup_.size() + voices_.size() + holds_.size() + outputs_.size() + 1))) { write = false; ++diagnostics_.dropped; }
            if (hold && !holding && !own_hold) write = false;
            if (hold && !holding) {
                for (const auto &claim : holds_)
                    if (claim.output == event->output && claim.channel == (event->packet.bytes[0] & 15) && claim.controller == event->packet.bytes[1]
                        && (!same_owner(claim.owner, event->owner) || claim.input != event->input)) write = false;
            }
            bool live = event->input < max_routes && event->packet.source != 65535;
            Packet delivered_packet = event->packet;
            // Streams already merges its logical owners. The physical ledger
            // sees that aggregate, including a last release from another route.
            if (onset || release || hold) delivered_packet.source = 65533;
            if (write && !(live ? sink.submit_live(event->output, delivered_packet, &diagnostics_.transmitted) : sink.submit(event->output, delivered_packet))) {
                if (sink.memory_refused() && onset && voice && !voice->sounding) {
                    // An onset refused before its first byte must not block
                    // releases already owed by other accepted voices.
                    const uint64_t id = voice->id;
                    erase_if(voices_, [id](const Voice &entry) { return entry.id == id; });
                    event = events_.erase(event);
                    erase_if(events_, [id](const Event &entry) { return entry.voice == id; });
                    event = events_.begin();
                    ++diagnostics_.dropped;
                    continue;
                }
                attempted |= bit; ++event; continue;
            }
            if (write) { if (!live) ++diagnostics_.transmitted; delivered |= bit; }
            if (write && clock) { clock_claimed_[event->output] = true; clock_sources_[event->output] = event->input; clock_owners_[event->output] = event->owner; }
            if (voice && onset) voice->sounding = true;
            if (write && (event->packet.bytes[0] == 0xff || (event->packet.bytes[0] >> 4 == 11 && event->packet.bytes[1] >= 120))) {
                for (auto &entry : voices_)
                    if (entry.output == event->output && (event->packet.bytes[0] == 0xff || (entry.packet.bytes[0] & 15) == (event->packet.bytes[0] & 15))) entry.sounding = false;
                if (event->packet.bytes[0] == 0xff || event->packet.bytes[1] == 121)
                    erase_if(holds_, [&](const Hold &claim) { return claim.output == event->output && (event->packet.bytes[0] == 0xff || claim.channel == (event->packet.bytes[0] & 15)); });
            }
            if (hold) {
                auto claim = std::find_if(holds_.begin(), holds_.end(), [&](const Hold &entry) { return entry.output == event->output && entry.input == event->input && same_owner(entry.owner, event->owner) && entry.channel == (event->packet.bytes[0] & 15) && entry.controller == event->packet.bytes[1]; });
                if (holding && write && claim == holds_.end()) {
                    if (!holds_.append({event->input, event->output, event->owner, uint8_t(event->packet.bytes[0] & 15), event->packet.bytes[1]})) ++diagnostics_.dropped;
                } else if (!holding && claim != holds_.end()) holds_.erase(claim);
            }
            if (event->packet.cin == 4) { sysex_outputs_ |= bit; sysex_owners_[event->output] = event->owner; sysex_sources_[event->output] = event->input; }
            else if (event->packet.cin >= 5 && event->packet.cin <= 7 && event->packet.bytes[event->packet.size() - 1] == 0xf7) sysex_outputs_ &= ~bit;
            if (voice && release) { auto id = voice->id; erase_if(voices_, [id](const Voice &entry) { return entry.id == id; }); }
            event = events_.erase(event);
        }
        service_parameters(sink, available, busy, exclusive, used, budget, delivered);
        for (unsigned i = 0; i < routes_.size(); ++i) if (routes_[i].lfo && routes_[i].lfo->ended && !routes_[i].lfo->finishing) {
            auto &lfo = *routes_[i].lfo;
            bool pending = false;
            for (const auto &event : events_) if (event.lfo == lfo.id && event.sample != 1) pending = true;
            for (const auto &capture : captures_) if (capture.work.lfo == lfo.id && capture.work.sample != 1) pending = true;
            if (!pending) {
                lfo.finishing = true; routes_[i].enabled = false;
                for (auto &delivery : parameters_) if (delivery.lfo == lfo.id) stop_parameter(delivery, true);
            }
        }
        erase_if(parameters_, [&](const ParameterDelivery &delivery) {
            if (!delivery.finished) return false;
            for (unsigned i = 0; i < routes_.size(); ++i) if (owns(delivery.owner, i) && !active(i)) return true;
            for (const auto &route : routes_) if (route.lfo && route.lfo->id == delivery.lfo) return false;
            return true;
        });
        reclaim_owner_generation();
    }
private:
    struct OwnerPath { unsigned route; const OwnerPath *parent = nullptr; };
    struct OwnerGeneration {
        CheckedArray<OwnerPath> paths;
        CheckedArray<unsigned> offsets;
    };
    CheckedList<OwnerGeneration> owner_generations_;
    OwnerGeneration *owner_generation_ = nullptr;
    OwnerGeneration *prepared_paths_ = nullptr;
    CheckedObject<Streams> prepared_;
    uint64_t prepared_time_ = 0;
    static const OwnerPath *owner_path(uint64_t owner) { return reinterpret_cast<const OwnerPath *>(uintptr_t(owner)); }
    static uint64_t owner_id(const OwnerPath *owner) { return uint64_t(reinterpret_cast<uintptr_t>(owner)); }
    static bool same_owner(uint64_t a, uint64_t b) {
        auto *left = owner_path(a); auto *right = owner_path(b);
        while (left && right) {
            if (left->route != right->route) return false;
            left = left->parent; right = right->parent;
        }
        return !left && !right;
    }
    static bool owns(uint64_t owner, unsigned route) {
        for (auto *path = owner_path(owner); path; path = path->parent) if (path->route == route) return true;
        return false;
    }
    uint64_t extend_owner(uint64_t parent, unsigned route) const {
        if (parent && owner_path(parent)->route == route) return parent;
        if (!owner_generation_ || route + 1 >= owner_generation_->offsets.size()) return 0;
        for (unsigned i = owner_generation_->offsets[route]; i < owner_generation_->offsets[route + 1]; ++i)
            if (same_owner(owner_id(owner_generation_->paths[i].parent), parent)) return owner_id(&owner_generation_->paths[i]);
        return 0;
    }
    template<class RouteAt> bool build_owners(unsigned count, RouteAt route_at) {
        OwnerGeneration candidate;
        if (!candidate.offsets.resize(count + 1)) return false;
        uint64_t total = 0;
        for (unsigned i = 0; i < count; ++i) {
            const auto &route = route_at(i);
            unsigned roots = route.inputs.empty() ? 1 : 0;
            for (const auto &input : route.inputs) {
                if (!input.kind) roots = 1;
                else total += candidate.offsets[input.index + 1] - candidate.offsets[input.index];
            }
            total += roots;
            if (total > UINT32_MAX / sizeof(OwnerPath)) return false;
            candidate.offsets[i + 1] = unsigned(total);
        }
        if (!candidate.paths.resize(unsigned(total))) return false;
        for (unsigned i = 0; i < count; ++i) {
            const auto &route = route_at(i);
            unsigned cursor = candidate.offsets[i];
            bool root = route.inputs.empty();
            for (const auto &input : route.inputs) if (!input.kind) root = true;
            if (root) candidate.paths[cursor++] = {i, nullptr};
            for (const auto &input : route.inputs) if (input.kind)
                for (unsigned j = candidate.offsets[input.index]; j < candidate.offsets[input.index + 1]; ++j)
                    candidate.paths[cursor++] = {i, &candidate.paths[j]};
        }
        auto *generation = owner_generations_.append(std::move(candidate));
        if (!generation) return false;
        owner_generation_ = generation; return true;
    }
    bool owner_activated(uint64_t owner) const {
        for (auto *path = owner_path(owner); path; path = path->parent)
            if (path->route < routes_.size() && routes_[path->route].activated) return true;
        return false;
    }
    void reclaim_owner_generation() {
        for (auto generation = owner_generations_.begin(); generation != owner_generations_.end(); ++generation) {
            if (&*generation == owner_generation_ || &*generation == prepared_paths_) continue;
            const uintptr_t first = reinterpret_cast<uintptr_t>(generation->paths.data());
            const uintptr_t last = first + generation->paths.size() * sizeof(OwnerPath);
            auto contains = [&](uint64_t owner) { return owner && owner >= first && owner < last; };
            bool referenced = false;
            for (const auto &voice : voices_) referenced = referenced || contains(voice.owner);
            for (const auto &event : events_) referenced = referenced || contains(event.owner);
            for (const auto &capture : captures_) referenced = referenced || contains(capture.work.owner);
            for (const auto &visit : visits_) referenced = referenced || contains(visit.owner);
            for (const auto &hold : holds_) referenced = referenced || contains(hold.owner);
            for (const auto &delivery : parameters_) referenced = referenced || contains(delivery.owner);
            for (unsigned output = 0; output < outputs_.size(); ++output)
                referenced = referenced || ((sysex_outputs_ & (1u << output)) && contains(sysex_owners_[output]))
                    || (clock_claimed_[output] && contains(clock_owners_[output]));
            if (!referenced) { owner_generations_.erase(generation); return; }
        }
    }
    struct Registration { unsigned opcode, version; Factory factory; bool generator; };
    static CheckedList<Registration> &registry() { static CheckedList<Registration> result; return result; }
    struct Connection { bool present = true; uint16_t blocked = 0; uint64_t identity = 0; unsigned generation = 0; };
    struct Work {
        Packet packet; uint64_t due, token; unsigned input; uint64_t owner;
        Parameter parameter = {}; uint64_t lfo = 0; uint8_t sample = 1;
    };
    struct Processing { unsigned route, position; Work work; };
    CheckedList<Processing> processing_;
    CheckedList<uint64_t> processing_release_tokens_;
    CheckedList<Processing>::iterator processing_position_;
    bool processing_active_ = false;
    template<class RouteAt> static bool prepare_processing(CheckedList<Processing> &pending, unsigned count, RouteAt route_at) {
        uint64_t capacity = 1;
        for (unsigned i = 0; i < count; ++i) {
            const auto &route = route_at(i);
            for (const auto &input : route.inputs) capacity += bool(input.kind);
            for (const auto &step : route.steps) if (step.kind == 6) capacity += step.repeats;
        }
        return capacity <= UINT32_MAX && pending.reserve(unsigned(capacity));
    }
    struct Voice {
        uint64_t id, token, owner, delay;
        unsigned input, output;
        Packet packet;
        bool sounding = false;
    };
    struct Event {
        uint64_t due, owner, voice, sequence; unsigned input, output; Packet packet;
        Parameter parameter = {}; uint64_t lfo = 0; uint8_t sample = 1;
    };
    struct ParameterBinding { unsigned output; uint64_t owner; Parameter target; };
    struct ParameterDelivery {
        unsigned output; uint64_t owner, lfo, identity;
        Parameter target;
        uint8_t off = 0, sample = 1;
        int final_value = 0, snapshot = Parameter::unknown, last = Parameter::unknown;
        int values[3] = {Parameter::unknown, Parameter::unknown, Parameter::unknown};
        ParameterTransaction flight = {};
        bool initial = false, touched = false, stopping = false, finished = false;
    };
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
    CheckedArray<Route> routes_;
    CheckedArray<Parser> parsers_;
    CheckedArray<Connection> inputs_, outputs_;
    InputNotes input_notes_;
    CheckedList<Voice> voices_;
    CheckedList<Event> events_;
    CheckedList<Cleanup> cleanup_;
    CheckedList<Hold> holds_;
    CheckedList<ParameterDelivery> parameters_;
    CheckedList<ParameterBinding> bindings_;
    bool activation_pending_ = false;
    CheckedArray<Parameter> binding_targets_;
    CheckedArray<uint64_t> binding_owners_;
    mutable CheckedArray<uint8_t> downstream_reached_;
    BeatPosition beat_;
    Sink *sink_ = nullptr;
    static bool parameter_step(const Step &step, Parameter &target) {
        if (step.processor || step.kind >= 8) return false;
        if (step.kind == 2 || step.kind == 3) {
            if (target.kind == Parameter::key_pressure)
                return target.set(2, step.kind == 2 ? target.address + step.value : 2 * step.value - target.address);
        } else if (step.kind == 4) {
            if (step.field != target.value_field() || step.output_low < target.minimum() || step.output_low > target.maximum()
                || step.output_high < target.minimum() || step.output_high > target.maximum()) return false;
        } else if (step.kind == 5) {
            for (const auto &a : step.assignments) if (a.field != target.value_field()) {
                if (!step.match.empty() || a.source != 255 || !target.set(a.field, a.value)) return false;
            }
        } else if (step.kind == 6) return step.repeats == 1 && step.feedback == 1;
        else if (step.kind == 7) return step.time != 0;
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(lfo_bindings)
#endif
    bool parameter_bindings(unsigned root) {
        bindings_.clear();
        std::fill(binding_targets_.begin(), binding_targets_.end(), Parameter{});
        std::fill(binding_owners_.begin(), binding_owners_.end(), uint64_t(0));
        auto &targets = binding_targets_;
        auto &owners = binding_owners_;
        targets[root] = routes_[root].lfo->config.target;
        for (unsigned i = root; i < routes_.size(); ++i) if (active(i)) {
            unsigned sources = 0;
            if (i == root) owners[i] = extend_owner(0, root);
            else for (const auto &input : routes_[i].inputs) if (input.kind && owners[input.index]) {
                if (++sources > 1) return false;
                targets[i] = targets[input.index]; owners[i] = extend_owner(owners[input.index], i);
            }
            if (!owners[i]) continue;
            unsigned begin = i == root ? 1 : 0;
            for (unsigned j = begin; j < routes_[i].steps.size(); ++j)
                if (!parameter_step(routes_[i].steps[j], targets[i])) return false;
            for (unsigned output = 0; output < outputs_count_; ++output) if (routes_[i].outputs & (1u << output)) {
                if (!bindings_.append({output, owners[i], targets[i]})) return false;
            }
        }
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(lfo_validation)
#endif
    bool validate_lfos() {
        unsigned generators = 0;
        for (const auto &route : routes_) generators += bool(route.lfo);
        (void)generators;
        CheckedList<ParameterBinding> all;
        for (unsigned context = 0; context <= routes_.size(); ++context) {
            selected_ = context == routes_.size() ? system_scope : routes_[context].scope;
            all.clear();
            for (unsigned root = 0; root < routes_.size(); ++root) if (routes_[root].lfo && active(root)) {
                if (!parameter_bindings(root)) return false;
                for (const auto &binding : bindings_) {
                    if (routes_[root].lfo->config.off == 1 && (routes_[root].lfo->config.final_value < binding.target.minimum()
                        || routes_[root].lfo->config.final_value > binding.target.maximum())) return false;
                    for (const auto &other : all) if (binding.output == other.output && binding.target.overlaps(other.target)) return false;
                    if (!all.append(binding)) return false;
                }
            }
        }
        selected_ = system_scope; bindings_.clear(); return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(lfo_bind)
#endif
    bool bind_lfo(unsigned root, uint64_t id, bool fresh) {
        auto &route = routes_[root];
        if (!parameter_bindings(root)) { route.failed = true; ++diagnostics_.failed; return false; }
        unsigned needed = 0;
        for (const auto &binding : bindings_) {
            bool existing = false;
            for (const auto &delivery : parameters_) if (delivery.lfo == id && delivery.output == binding.output
                && same_owner(delivery.owner, binding.owner) && delivery.target.same(binding.target)) {
                if (!delivery.finished || (!fresh && !owner_activated(binding.owner))) existing = true;
            }
            if (existing || (!fresh && !owner_activated(binding.owner))) continue;
            ++needed;
            for (const auto &delivery : parameters_) if (!delivery.finished && delivery.output == binding.output && delivery.target.overlaps(binding.target)) {
                if (!delivery.stopping) { route.failed = true; ++diagnostics_.failed; }
                return false;
            }
            if (outputs_[binding.output].present && route.lfo->config.off == 2 && sink_->parameter_value(binding.output, binding.target) == Parameter::unknown) {
                route.failed = true; ++diagnostics_.failed; return false;
            }
        }
        if (!parameters_.reserve(parameters_.size() + needed)) { ++diagnostics_.overloaded; return false; }
        for (const auto &binding : bindings_) {
            bool existing = false;
            for (const auto &delivery : parameters_) if (delivery.lfo == id && delivery.output == binding.output
                && same_owner(delivery.owner, binding.owner) && delivery.target.same(binding.target)
                && (!delivery.finished || (!fresh && !owner_activated(binding.owner)))) existing = true;
            if (existing || (!fresh && !owner_activated(binding.owner))) continue;
            ParameterDelivery delivery{binding.output, binding.owner, id, outputs_[binding.output].identity, binding.target};
            delivery.off = route.lfo->config.off; delivery.final_value = route.lfo->config.final_value;
            delivery.snapshot = sink_->parameter_value(binding.output, binding.target);
            delivery.finished = !outputs_[binding.output].present;
            if (!parameters_.append(delivery)) return false;
        }
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(lfo_service)
#endif
    void service_lfos(uint64_t now, unsigned &used, unsigned budget) {
        bool synchronized = true;
        for (unsigned root = 0; root < routes_.size() && used < budget; ++root) {
            auto &route = routes_[root];
            if (!route.lfo || (route.scope != system_scope && (route.scope != selected_ || route.foreign != external_selected_))) continue;
            auto &lfo = *route.lfo;
            lfo.advance(now, beat_, active(root) && route.started);
            if (!active(root)) continue;
            if (!route.started) {
                uint64_t id = serial_ + 1;
                if (!bind_lfo(root, id, true)) { synchronized = false; continue; }
                ++serial_; lfo.enable(now, beat_, id); route.started = true;
            } else if (activation_pending_ && !bind_lfo(root, lfo.id, false)) { synchronized = false; continue; }
            if (lfo.ended || (lfo.generated && (now < lfo.next || lfo.waiting))) continue;
            bool terminal = lfo.generated && lfo.complete();
            uint8_t sample = !lfo.generated ? 0 : terminal ? 2 : 1;
            if (terminal) {
                erase_if(events_, [&](const Event &event) { return event.lfo == lfo.id && event.sample == 1; });
                erase_if(captures_, [&](const Capture &capture) { return capture.work.lfo == lfo.id && capture.work.sample == 1; });
            }
            if (lfo.generated && now > lfo.next) diagnostics_.skipped += uint32_t((now - lfo.next) / lfo.config.interval);
            Parameter value = lfo.config.target; value.value = lfo.sample(terminal);
            uint8_t bytes[3] = {}; value.message(0, bytes);
            Work work{{uint8_t(bytes[0] >> 4), {bytes[0], bytes[1], bytes[2]}}, now, 0, max_routes + root, 0, value, lfo.id, sample};
            process(root, 1, work); ++used;
            lfo.generated = true; lfo.ended = terminal; lfo.next = now + lfo.config.interval;
        }
        if (synchronized) { activation_pending_ = false; for (auto &route : routes_) route.activated = false; }
    }
    void stop_parameter(ParameterDelivery &delivery, bool terminal) {
        if (delivery.finished) return;
        delivery.stopping = true;
        delivery.values[1] = Parameter::unknown;
        if (!terminal) delivery.values[0] = delivery.values[2] = Parameter::unknown;
        if (!delivery.flight.position && (!terminal || delivery.sample == 1)) delivery.flight = {};
    }
    void accept_parameter(const Event &event) {
        for (auto &delivery : parameters_) if (delivery.lfo == event.lfo && same_owner(delivery.owner, event.owner)
            && delivery.output == event.output && delivery.target.same(event.parameter)) {
            if (delivery.finished || delivery.stopping) continue;
            unsigned slot = event.sample;
            if (slot == 1 && delivery.flight.token && !delivery.flight.position && delivery.sample == 1) {
                delivery.flight.parameter.value = event.parameter.value;
                ++diagnostics_.coalesced; return;
            }
            if (!delivery.initial && delivery.values[0] == Parameter::unknown && slot == 1
                && !(delivery.flight.token && delivery.sample == 0)) slot = 0;
            if (slot == 1 && delivery.values[slot] != Parameter::unknown) ++diagnostics_.coalesced;
            if (slot != 1 && delivery.values[slot] != Parameter::unknown) return;
            delivery.values[slot] = event.parameter.value; return;
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(parameter_service)
#endif
    void service_parameters(Sink &sink, unsigned available, unsigned busy, unsigned exclusive, unsigned &used, unsigned budget, unsigned delivered) {
        if (parameters_.empty()) return;
        for (unsigned output = 0; output < outputs_.size() && used < budget; ++output) {
            unsigned bit = 1u << output;
            if ((delivered & bit) || !(available & bit) || !outputs_[output].present || ((exclusive | sysex_outputs_) & bit)) continue;
            uint64_t lock = 0;
            for (const auto &d : parameters_) if (d.output == output && d.flight.position) lock = d.flight.token;
            for (unsigned scan = 0; scan < parameters_.size(); ++scan) {
                unsigned index = (parameter_cursor_[output] + scan) % unsigned(parameters_.size());
                auto &delivery = parameters_[index];
                if (delivery.output != output || delivery.finished || outputs_[output].identity != delivery.identity
                    || (lock && delivery.flight.token != lock)) continue;
                if (!delivery.flight.token) {
                    if (!delivery.stopping && (busy & bit)) continue;
                    int value = Parameter::unknown;
                    unsigned slot = 0;
                    if (delivery.values[0] != Parameter::unknown) { value = delivery.values[0]; slot = 0; }
                    else if (delivery.values[2] != Parameter::unknown) {
                        bool initial_pending = false;
                        for (const auto &event : events_) if (event.lfo == delivery.lfo && event.sample == 0 && event.output == delivery.output) initial_pending = true;
                        for (const auto &capture : captures_) if (capture.work.lfo == delivery.lfo && capture.work.sample == 0) initial_pending = true;
                        if (initial_pending) continue;
                        value = delivery.values[2]; slot = 2;
                    }
                    else if (delivery.stopping) {
                        if (!delivery.touched || !delivery.off) { delivery.finished = true; continue; }
                        value = delivery.off == 1 ? delivery.final_value : delivery.snapshot; slot = 3;
                    } else if (delivery.values[1] != Parameter::unknown) { value = delivery.values[1]; slot = 1; }
                    if (value == Parameter::unknown) continue;
                    if (slot < 3) delivery.values[slot] = Parameter::unknown;
                    if (delivery.last == value) {
                        if (slot == 0) delivery.initial = true;
                        if (slot == 3) delivery.finished = true;
                        continue;
                    }
                    delivery.sample = uint8_t(slot);
                    delivery.flight = {delivery.target, ++serial_, 0, delivery.lfo}; delivery.flight.parameter.value = value;
                }
                ++used;
                parameter_cursor_[output] = (index + 1) % unsigned(parameters_.size());
                if (!sink.submit_parameter(delivery.output, delivery.flight)) break;
                ++diagnostics_.transmitted; delivered |= bit; delivery.touched = true;
                if (delivery.flight.position == delivery.flight.parameter.messages()) {
                    delivery.last = delivery.flight.parameter.value; delivery.flight = {};
                    if (delivery.sample == 0) delivery.initial = true;
                    if (delivery.sample == 3) delivery.finished = true;
                }
                break;
            }
        }
    }
    unsigned outputs_count_ = 0;
    CheckedArray<unsigned> parameter_cursor_;
    CheckedArray<uint64_t> sysex_owners_;
    CheckedArray<unsigned> sysex_sources_;
    CheckedArray<bool> clock_claimed_;
    CheckedArray<unsigned> clock_sources_;
    CheckedArray<uint64_t> clock_owners_;
    bool prepare_output_state(unsigned count) {
        return parameter_cursor_.resize(count) && sysex_owners_.resize(count) && sysex_sources_.resize(count)
            && clock_claimed_.resize(count) && clock_sources_.resize(count) && clock_owners_.resize(count);
    }
    CheckedList<Capture> captures_;
    CheckedList<Work> reverse_parameters_;
    CheckedList<Visit> visits_;
    StreamFileProvider *file_provider_ = nullptr;
    bool admit_note(unsigned input, const Packet &packet, uint64_t &token, bool resources_ready = true, unsigned destination = 0) {
        const unsigned generated_route = input % max_routes;
        const bool blocked = input >= inputs_.size() && generated_route < routes_.size() && routes_[generated_route].note_admission_closed;
        bool ready = resources_ready && !blocked && input_notes_.outstanding() < limits_.notes;
        if (ready && processing_active_)
            ready = processing_.reserve(processing_.size() + processing_release_tokens_.size() + 2)
                && processing_release_tokens_.reserve(processing_release_tokens_.size() + 1);
        const auto admission = input_notes_.admit(input, packet.source, packet, serial_ + 1, ready, destination);
        if (!admission.accepted) {
            ++diagnostics_.dropped;
            if (admission.untracked && generated_route < routes_.size()) routes_[generated_route].note_admission_closed = true;
            return false;
        }
        token = ++serial_;
        if (processing_active_ && !processing_release_tokens_.append(token)) return false;
        return true;
    }
    void release_input_note(unsigned input, const Packet &packet, uint64_t now, unsigned destination = 0) {
        const auto release = input_notes_.release(input, packet.source, packet, destination);
        if (!release.token) return;
        processing_release_tokens_.erase_if([&](uint64_t token) { return token == release.token; });
        if (processing_active_) {
            if (!processing_.insert(processing_position_, {UINT32_MAX, 0, {packet, now, release.token, input, 0}})) ++diagnostics_.failed;
        } else release_note(release.token, now, packet.bytes[2], &packet);
    }
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
            Work transformed = work; transformed.packet = packet; transformed.due += delay;
            transformed.packet.source = work.packet.source;
            const unsigned destination = max_routes * position + route;
            unsigned kind = packet.bytes[0] >> 4;
            if (!work.token && (kind == 8 || (kind == 9 && !packet.bytes[2]))) {
                engine.release_input_note(work.input, transformed.packet, transformed.due, destination); return true;
            }
            if (!credits || engine.events_.size() + engine.voices_.size() >= engine.limits_.events) {
                if (!work.token && kind == 9 && packet.bytes[2]) {
                    uint64_t refused = 0;
                    engine.admit_note(work.input, transformed.packet, refused, false, destination);
                }
                return false;
            }
            --credits;
            if (!work.token && kind == 9 && packet.bytes[2]) {
                if (!engine.admit_note(work.input, transformed.packet, transformed.token, true, destination)) return false;
            }
            engine.process(route, position, transformed); return true;
        }
    };
    void generated(unsigned route, const Packet &packet, uint64_t now) {
        unsigned input = max_routes + route;
        unsigned kind = packet.bytes[0] >> 4;
        uint64_t token = 0;
        if (kind == 9 && packet.bytes[2]) {
            if (!admit_note(input, packet, token)) return;
        } else if (kind == 8 || (kind == 9 && !packet.bytes[2])) {
            release_input_note(input, packet, now); return;
        }
        process(route, 1, {packet, now, token, input, 0});
    }
    template<class T, class Predicate> static void erase_if(CheckedList<T> &items, Predicate predicate) {
        items.erase_if(predicate);
    }
    bool active(unsigned route) const {
        const auto &entry = routes_[route];
        return entry.enabled && !entry.failed && (entry.scope == system_scope || (entry.scope == selected_ && entry.foreign == external_selected_));
    }
    unsigned downstream_outputs(unsigned route) const {
        unsigned outputs = routes_[route].outputs;
        std::fill(downstream_reached_.begin(), downstream_reached_.end(), uint8_t(0));
        auto &reached = downstream_reached_; reached[route] = 1;
        for (unsigned i = route + 1; i < routes_.size(); ++i) if (active(i))
            for (const auto &input : routes_[i].inputs) if (input.kind && reached[input.index]) { outputs |= routes_[i].outputs; reached[i] = 1; break; }
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
        if (!cleanup_.append({output, packet})) ++diagnostics_.failed;
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
    void retire(unsigned route) {
        const auto selected = [route](uint64_t owner) { return route == UINT32_MAX || owns(owner, route); };
        for (auto &delivery : parameters_) if (selected(delivery.owner)) stop_parameter(delivery, false);
        for (unsigned output = 0; output < outputs_.size(); ++output)
            if (clock_claimed_[output] && selected(clock_owners_[output])) clock_claimed_[output] = false;
        for (unsigned i = 0; i < routes_.size(); ++i) if (route == UINT32_MAX || i == route) {
            routes_[i].started = false;
            for (auto &step : routes_[i].steps) if (step.processor) step.processor->stop();
        }
        retire_holds([&](const Hold &claim) { return selected(claim.owner); });
        for (unsigned output = 0; output < outputs_.size(); ++output)
            if ((sysex_outputs_ & (1u << output)) && selected(sysex_owners_[output])) cleanup_packet(output, {5, {0xf7, 0, 0}});
        for (const auto &voice : voices_) if (selected(voice.owner) && voice.sounding) {
            bool shared = false;
            for (const auto &other : voices_) if (!selected(other.owner) && other.sounding && same_key(other, voice)) shared = true;
            if (!shared) owe(voice);
        }
        erase_if(voices_, [&](const Voice &voice) { return selected(voice.owner); });
        erase_if(events_, [&](const Event &event) { return selected(event.owner); });
        erase_if(captures_, [&](const Capture &capture) { return selected(capture.work.owner); });
        erase_if(visits_, [&](const Visit &visit) { return selected(visit.owner); });
    }
    void retire_source(unsigned input) {
        const uint64_t before = serial_;
        for (;;) {
            auto visit = std::find_if(visits_.begin(), visits_.end(), [&](const Visit &entry) { return entry.input == input && entry.token <= before; });
            if (visit == visits_.end()) break;
            notify_release(visit->token, 0, current_time_);
        }
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
        for (const auto &strike : input_notes_.strikes()) if (strike.input == input)
            processing_release_tokens_.erase_if([&](uint64_t token) { return token == strike.token; });
        input_notes_.forget_input(input);
        erase_if(captures_, [input](const Capture &capture) { return capture.work.input == input; });
        if (input < inputs_.size()) inputs_[input].blocked = false;
    }
    void overload_route(unsigned route) {
        ++diagnostics_.overloaded;
        retire(route); routes_[route].failed = true;
    }
    void overload(uint64_t owner) {
        if (owner) overload_route(owner_path(owner)->route);
    }
    unsigned output_divisor_ = 1;
    void refresh_output_divisor() {
        unsigned mask = 0, count = 0;
        for (unsigned i = 0; i < routes_.size(); ++i) if (active(i)) mask |= routes_[i].outputs;
        while (mask) { mask &= mask - 1; ++count; }
        output_divisor_ = std::max(1u, count);
    }
    unsigned output_event_limit() const { return std::max(1u, limits_.events / output_divisor_); }
    unsigned output_voice_limit() const { return std::max(1u, limits_.voices / output_divisor_); }
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
        routes_[route].failed_outputs |= uint16_t(1u << output);
        retire_holds([&](const Hold &hold) { return hold.output == output && owns(hold.owner, route); });
        for (const auto &voice : voices_) if (voice.output == output && owns(voice.owner, route) && voice.sounding) {
            bool shared = false;
            for (const auto &other : voices_) if (!owns(other.owner, route) && other.sounding && same_key(other, voice)) shared = true;
            if (!shared) owe(voice);
        }
        erase_if(voices_, [&](const Voice &voice) { return voice.output == output && owns(voice.owner, route); });
        erase_if(events_, [&](const Event &event) { return event.output == output && owns(event.owner, route); });
        if ((sysex_outputs_ & (1u << output)) && owns(sysex_owners_[output], route)) cleanup_packet(output, {5, {0xf7,0,0}});
        if (clock_claimed_[output] && owns(clock_owners_[output], route)) clock_claimed_[output] = false;
    }
    bool enqueue(Event event) {
        if (event.parameter.kind && event.sample == 1 && event.due <= current_time_) {
            for (auto &pending : events_) if (pending.lfo == event.lfo && pending.output == event.output && pending.sample == 1
                && pending.due <= current_time_ && pending.parameter.same(event.parameter)) {
                if (event.due >= pending.due) { pending.parameter = event.parameter; pending.due = event.due; }
                ++diagnostics_.coalesced; return true;
            }
        }
        bool release = event.packet.bytes[0] >> 4 == 8;
        if (events_.size() + (release ? 0 : voices_.size()) >= limits_.events
            || output_events(event.output) + (release ? 0 : output_voices(event.output)) >= output_event_limit()) { ++diagnostics_.dropped; return false; }
        event.sequence = ++serial_;
        auto position = std::upper_bound(events_.begin(), events_.end(), event, [](const Event &a, const Event &b) { return a.due < b.due; });
        if (!release && !events_.reserve(events_.size() + voices_.size() + 2)) { ++diagnostics_.dropped; return false; }
        if (!events_.insert(position, event)) { ++diagnostics_.dropped; return false; }
        return true;
    }
    void release_note(uint64_t token, uint64_t now, uint8_t velocity, const Packet *source = nullptr) {
        notify_release(token, velocity, now);
        for (auto &capture : captures_) if (capture.work.token == token && !capture.closed) {
            capture.end = now + capture.delay; capture.closed = true;
        }
        unsigned failed_outputs = 0;
        for (const auto &voice : voices_) if (voice.token == token) {
            Packet release = source ? *source : Packet{8, {uint8_t(0x80 | (voice.packet.bytes[0] & 15)), voice.packet.bytes[1], velocity}};
            release.cin = 8; release.bytes[0] = uint8_t(0x80 | (release.bytes[0] & 15));
            // Transform release fields from the original input coordinates, then
            // restore the mapping recorded when its onset was admitted.
            owned_fields(release, voice.owner, voice.input);
            release.cin = 8; release.bytes[0] = uint8_t(0x80 | (voice.packet.bytes[0] & 15)); release.bytes[1] = voice.packet.bytes[1];
            if (!enqueue({now + voice.delay, voice.owner, voice.id, 0, voice.input, voice.output, release})) failed_outputs |= 1u << voice.output;
        }
        for (unsigned output = 0; output < outputs_.size(); ++output) if (failed_outputs & (1u << output)) {
            for (;;) {
                auto voice = std::find_if(voices_.begin(), voices_.end(), [&](const Voice &entry) { return entry.token == token && entry.output == output; });
                if (voice == voices_.end()) break;
                unsigned route = 0;
                if (voice->owner) route = owner_path(voice->owner)->route;
                fail_output(route, output);
            }
        }
    }
    void notify_release(uint64_t token, uint8_t velocity, uint64_t now) {
        for (;;) {
            auto pending = std::find_if(visits_.begin(), visits_.end(), [token](const Visit &entry) { return entry.token == token; });
            if (pending == visits_.end()) break;
            const Visit visit = *pending; visits_.erase(pending);
            Packet release{8, {uint8_t(0x80 | (visit.onset.bytes[0] & 15)), visit.onset.bytes[1], velocity}};
            Work work{release, now, 0, max_routes * (visit.step + 1) + visit.route, visit.owner};
            ProcessorOutput output(*this, visit.route, visit.step + 1, work, limits_.events);
            auto &processor = routes_[visit.route].steps[visit.step].processor;
            if (processor && !processor->push(release, visit.input, output)) { ++diagnostics_.failed; overload_route(visit.route); }
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
            int value = work_field(work, match.id);
            if (value == Parameter::unknown || std::find(match.values.begin(), match.values.end(), value) == match.values.end()) return false;
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
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(streams_numeric_fields)
#endif
    static bool numeric_fields(const Step &step, Work &work) {
        if (work.parameter.kind) {
            if (!matches(step, work)) return true;
            if ((step.kind == 2 || step.kind == 3) && work.parameter.kind == Parameter::key_pressure)
                return work.parameter.set(2, step.kind == 2 ? work.parameter.address + step.value : 2 * step.value - work.parameter.address);
            if (step.kind == 4) {
                int original = work_field(work, step.field);
                if (original == Parameter::unknown) return true;
                int low = step.input_low, high = step.input_high;
                if (low == -1 && high == -1) { low = work.parameter.minimum(); high = work.parameter.maximum(); }
                double fraction = double(std::clamp(original, low, high) - low) / (high - low);
                return work.parameter.set(step.field, int(std::floor(step.output_low + fraction * (step.output_high - step.output_low) + 0.5)));
            }
            if (step.kind == 5) {
                Parameter original = work.parameter;
                for (const auto &a : step.assignments) {
                    int value = a.value;
                    if (a.source != 255) {
                        int source = original.field(a.source, work.input);
                        if (source == Parameter::unknown) return true;
                        value += source;
                    }
                    if (!work.parameter.set(a.field, value)) return false;
                }
            }
            return true;
        }
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
    static int work_field(const Work &work, unsigned id) {
        if (work.parameter.kind) return work.parameter.field(id, work.input);
        int value = field(work.packet, id, work.input);
        return value < 0 ? Parameter::unknown : value;
    }
    bool owned_fields(Packet &packet, uint64_t owner, unsigned input, unsigned stop_route = max_routes, unsigned stop_step = 0) const {
        Work work{packet, 0, 0, input, owner};
        for (unsigned route = 0; route < routes_.size() && route <= stop_route; ++route) if (owns(owner, route)) {
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
        const auto position_in_queue = processing_active_ ? processing_position_ : processing_.end();
        if (!processing_.reserve(processing_.size() + processing_release_tokens_.size() + 1)) { ++diagnostics_.dropped; return; }
        if (!processing_.insert(position_in_queue, {route, position, work})) { ++diagnostics_.dropped; return; }
        if (processing_active_) return;
        processing_active_ = true;
        while (!processing_.empty()) {
            const Processing next = processing_.front();
            processing_.erase(processing_.begin());
            processing_position_ = processing_.begin();
            if (next.route == UINT32_MAX) release_note(next.work.token, next.work.due, next.work.packet.bytes[2], &next.work.packet);
            else process_one(next.route, next.position, next.work);
        }
        processing_active_ = false;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(streams_process_one)
#endif
    void process_one(unsigned route, unsigned position, Work work) {
        if (!active(route)) return;
        work.owner = extend_owner(work.owner, route);
        if (!work.owner) { ++diagnostics_.dropped; return; }
        auto &entry = routes_[route];
        for (unsigned i = position; i < entry.steps.size(); ++i) {
            auto &step = entry.steps[i];
            if (step.processor) {
                if (work.token && work.packet.bytes[0] >> 4 == 9) {
                    auto visit = std::find_if(visits_.begin(), visits_.end(), [&](const Visit &entry) { return entry.token == work.token && entry.route == route && entry.step == i; });
                    if (visit == visits_.end()) {
                        if (visits_.size() >= limits_.voices) { overload_route(route); return; }
                        if (!visits_.append({work.token, work.owner, route, i, work.input, work.packet})) { ++diagnostics_.dropped; return; }
                    }
                }
                ProcessorOutput output(*this, route, i + 1, work, limits_.events);
                if (!step.processor->push(work.packet, work.input, output)) { ++diagnostics_.failed; overload(work.owner); }
                return;
            }
            bool match = matches(step, work);
            if ((step.kind == 0 && !match) || (step.kind == 1 && match)) return;
            if ((step.kind == 2 || step.kind == 3) && field(work.packet, 2, work.input) >= 0) {
                if (work.parameter.kind) {
                    if (!numeric_fields(step, work)) return;
                    continue;
                }
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
                if (!captures_.append(capture)) ++diagnostics_.dropped;
                return;
            }
        }
        if (work.packet.bytes[0] >> 4 == 9 && !work.packet.bytes[2]) return;
        for (unsigned output = 0; output < outputs_.size(); ++output) if (entry.outputs & (1u << output)) {
            if (entry.failed_outputs & (1u << output)) continue;
            if (!outputs_[output].present || recovering(output)) { ++diagnostics_.dropped; continue; }
            if (work.parameter.kind) {
                const unsigned failures = memory_failures;
                if (!enqueue({work.due, work.owner, 0, 0, work.input, output, work.packet, work.parameter, work.lfo, work.sample})
                    && failures == memory_failures) fail_output(route, output);
                continue;
            }
            if (work.packet.cin == 4 && !cleanup_.reserve(cleanup_.size() + voices_.size() + holds_.size() + outputs_.size())) {
                ++diagnostics_.dropped; continue;
            }
            uint64_t voice = 0;
            if (work.packet.bytes[0] >> 4 == 9 && work.token) {
                if (voices_.size() >= limits_.voices || output_voices(output) >= output_voice_limit()
                    || events_.size() + voices_.size() + 2 >= limits_.events
                    || output_events(output) + output_voices(output) + 2 >= output_event_limit()) { fail_output(route, output); continue; }
                voice = ++serial_;
                if (!events_.reserve(events_.size() + voices_.size() + 3)
                    || !cleanup_.reserve(cleanup_.size() + voices_.size() + holds_.size() + outputs_.size() + 1)
                    || !voices_.append({voice, work.token, work.owner, work.due - current_time_, work.input, output, work.packet, false})) { ++diagnostics_.dropped; continue; }
            }
            const unsigned failures = memory_failures;
            if (!enqueue({work.due, work.owner, voice, 0, work.input, output, work.packet})) {
                if (failures == memory_failures) fail_output(route, output);
                else {
                    erase_if(voices_, [voice](const Voice &entry) { return voice && entry.id == voice; });
                    if ((sysex_outputs_ & (1u << output)) && same_owner(sysex_owners_[output], work.owner)
                        && sysex_sources_[output] == work.input) cleanup_packet(output, {5, {0xf7, 0, 0}});
                }
                continue;
            }
        }
        for (unsigned downstream = route + 1; downstream < routes_.size(); ++downstream) if (active(downstream))
            for (const auto &source : routes_[downstream].inputs) if (source.kind && source.index == route) { process(downstream, 0, work); break; }
    }
    uint64_t current_time_ = 0;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(streams_reverse_group)
#endif
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
        if (!finite && now - boundary >= window) { overload_route(route); return; }
        CheckedArray<TimelinePacket> sequence;
        struct Token { uint64_t old, fresh; };
        CheckedList<Token> tokens;
        unsigned selected = 0;
        auto eligible = [&](const Capture &capture) {
            if (capture.route != route || capture.step != step || (capture.frame && !capture.closed)) return false;
            const uint64_t time = capture.frame ? capture.end : capture.start;
            return time >= begin && (finite || time < boundary);
        };
        for (const auto &capture : captures_) if (eligible(capture)) ++selected;
        if (selected > UINT32_MAX / 2 || !sequence.reserve(selected * 2)
            || !tokens.reserve(selected) || !reverse_parameters_.reserve(selected)) { ++diagnostics_.dropped; return; }
        reverse_parameters_.clear();
        for (auto capture = captures_.begin(); capture != captures_.end(); ++capture) {
            if (!eligible(*capture)) continue;
            uint64_t time = capture->frame ? capture->end : capture->start;
            if (capture->work.parameter.kind) {
                Work reversed = capture->work;
                reversed.due = boundary + boundary - capture->start;
                if (!reverse_parameters_.append(reversed)) { ++diagnostics_.dropped; return; }
                continue;
            }
            TimelinePacket onset{time - begin, capture->work.packet, capture->work.input,
                capture->work.token, capture->work.owner};
            onset.order = sequence.size();
            if (!sequence.append(onset)) return;
            bool note = onset.packet.bytes[0] >> 4 == 9 && onset.token;
            if (note) {
                uint64_t end = capture->closed ? std::min(capture->end, boundary) : boundary;
                TimelinePacket release = onset; release.time = end - begin;
                release.packet.cin = 8;
                release.packet.bytes[0] = uint8_t(0x80 | (onset.packet.bytes[0] & 15));
                release.packet.bytes[2] = 0;
                release.order = sequence.size();
                if (!sequence.append(release)) return;
            }
        }
        std::sort(sequence.begin(), sequence.end(), [](const TimelinePacket &a, const TimelinePacket &b) {
            return a.time == b.time ? a.order < b.order : a.time < b.time;
        });
        if (!reverse_sequence(sequence, boundary - begin)) { ++diagnostics_.dropped; return; }
        for (auto capture = captures_.begin(); capture != captures_.end();) {
            if (!eligible(*capture)) { ++capture; continue; }
            bool note = capture->work.packet.bytes[0] >> 4 == 9 && capture->work.token;
            if (note && !finite && (!capture->closed || capture->end > boundary)) {
                capture->start = boundary; ++capture;
            } else capture = captures_.erase(capture);
        }
        for (const auto &value : reverse_parameters_) process(route, step + 1, value);
        for (const auto &event : sequence) {
            Work reversed{event.packet, boundary + event.time, event.token, event.source, event.owner};
            reversed.packet.source = 65535;
            unsigned kind = event.packet.bytes[0] >> 4;
            if (kind == 9 && event.token) {
                reversed.token = ++serial_;
                if (!tokens.append({event.token, reversed.token})) return;
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
                        && owns(capture.work.owner, owner_route)) owned = true;
                    if (owned) reverse_group(route, step, now, true);
                }
    }
private:
    void flush_reverse(uint64_t now) {
        for (unsigned route = 0; route < routes_.size(); ++route) {
            bool timed_out = false;
            for (const auto &capture : captures_) if (capture.route == route && capture.frame && !capture.closed) {
                uint64_t latest = capture.start;
                for (const auto &other : captures_) if (other.frame == capture.frame) latest = std::max(latest, other.start);
                if (now >= latest && now - latest >= 1000000) timed_out = true;
            }
            if (timed_out) overload_route(route);
        }
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
            if (!step.match.reserve(count)) return false;
            unsigned fields = 0;
            for (unsigned i = 0; i < count; ++i) {
                Field field; field.id = uint8_t(byte()); unsigned values = byte();
                if (field.id > 9 || !values || (fields & (1u << field.id))) return false;
                fields |= 1u << field.id;
                if (!field.values.reserve(values)) return false;
                for (unsigned j = 0; j < values; ++j) if (!field.values.append(int32_t(integer(4)))) return false;
                if (!step.match.append(std::move(field))) return false;
            }
            return ok;
        }
    };
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(streams_decode)
#endif
    bool decode(const uint8_t *data, unsigned size, Outputs outputs, unsigned nodes) {
        outputs_count_ = outputs.route_count();
        if (!data && !size) return true;
        if (!data || size < 12 || std::memcmp(data, "CSTR", 4)) return false;
        Reader reader{data + 4, data + size};
        if (reader.integer(4) != size) return false;
        unsigned count = unsigned(reader.integer(2)), flags = unsigned(reader.integer(2));
        if (count > max_routes || flags > 1) return false;
        if (!routes_.reserve(count)) return false;
        explicit_system_ = flags;
        for (unsigned i = 0; i < count; ++i) {
            Route route; route.scope = uint16_t(reader.integer(2)); unsigned inputs = unsigned(reader.integer(2));
            route.outputs = uint16_t(reader.integer(2)); unsigned steps = unsigned(reader.integer(2));
            if ((route.scope != system_scope && route.scope >= nodes) || (route.outputs & ~outputs.mask())) return false;
            if (!route.inputs.reserve(inputs) || !route.steps.reserve(steps)) return false;
            bool name_end = false;
            for (unsigned j = 0; j < 64; ++j) { unsigned c = reader.byte(); route.name[j] = char(c); if (!c) name_end = true; else if (name_end || c < 32 || c == '/') return false; }
            if (!name_end) return false;
            if (outputs.find(route.name)) return false;
            for (const auto &existing : routes_) if (existing.scope == route.scope && !std::strcmp(existing.name, route.name)) return false;
            for (unsigned j = 0; j < inputs; ++j) {
                Input input; input.kind = uint8_t(reader.byte()); input.index = uint16_t(reader.integer(2));
                if (input.kind > 1 || (input.kind ? input.index >= i : input.index >= 16 || !(outputs.mask(true) & (1u << input.index)))) return false;
                if (input.kind && routes_[input.index].scope != system_scope && routes_[input.index].scope != route.scope) return false;
                if (!route.inputs.append(input)) return false;
            }
            for (unsigned j = 0; j < steps; ++j) {
                Step step; step.kind = uint8_t(reader.byte()); unsigned reserved = reader.byte(), length = unsigned(reader.integer(2));
                if (!reader.ok || length > unsigned(reader.end - reader.cursor)
                    || (step.kind < 128 && (step.kind > 10 || reserved != (step.kind == 10 ? 1u : 0u)))) return false;
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
                    int minimum = step.field == 6 ? 1 : step.field == 5 ? -8192 : 0, maximum = step.field == 6 ? 128 : step.field == 5 ? 16383 : 127;
                    bool implicit = step.input_low == -1 && step.input_high == -1;
                    if ((step.field != 2 && step.field != 3 && step.field != 5 && step.field != 6 && step.field != 7)
                        || (!implicit && (step.input_low < minimum || step.input_high > maximum || step.input_low >= step.input_high))
                        || step.output_low < minimum || step.output_high < minimum || step.output_low > maximum || step.output_high > maximum) return false;
                } else if (step.kind == 5) {
                    if (!payload.match(step)) return false;
                    unsigned assignments = payload.byte();
                    if (!assignments || assignments > 8) return false;
                    if (!step.assignments.reserve(assignments)) return false;
                    for (unsigned k = 0; k < assignments; ++k) {
                        Assignment assignment; assignment.field = uint8_t(payload.byte()); assignment.source = uint8_t(payload.byte()); assignment.value = int32_t(payload.integer(4));
                        if (assignment.field > 9 || assignment.field == 8 || (assignment.source > 7 && assignment.source != 9 && assignment.source != 255)) return false;
                        if (assignment.value < -16383 || assignment.value > 16383) return false;
                        if (assignment.source != 255 && (!assignment.source || assignment.field == 0)) return false;
                        if (assignment.source == 255) {
                            int minimum = assignment.field == 1 || assignment.field == 6 ? 1 : assignment.field == 5 ? -8192 : 0;
                            int maximum = assignment.field == 1 ? 16 : assignment.field == 6 ? 128 : assignment.field == 5 || assignment.field == 9 ? 16383 : 127;
                            if (assignment.field == 0) {
                                int type = assignment.value;
                                if (!((type >= 8 && type <= 14) || type == 16 || type == 17 || type == 0xf1 || type == 0xf2 || type == 0xf3 || type == 0xf6 || type == 0xf8 || type == 0xfa || type == 0xfb || type == 0xfc || type == 0xfe || type == 0xff)) return false;
                            } else if (assignment.value < minimum || assignment.value > maximum) return false;
                        }
                        if (!step.assignments.append(assignment)) return false;
                    }
                } else if (step.kind == 6) {
                    step.time = payload.integer(8); step.repeats = unsigned(payload.integer(2)); uint32_t bits = uint32_t(payload.integer(4)); float feedback; std::memcpy(&feedback, &bits, 4); step.feedback = feedback;
                    if (!step.time || step.time > 3600000000ull || !step.repeats || step.repeats > 64 || !std::isfinite(feedback) || feedback < 0 || feedback > 1) return false;
                } else if (step.kind == 7) { step.time = payload.integer(8); if (step.time > 3600000000ull) return false; }
                else if (step.kind == 8) {
                    uint64_t bits = payload.integer(8); std::memcpy(&step.bpm, &bits, 8);
                    if (!std::isfinite(step.bpm) || step.bpm <= 0 || step.bpm > 1000 || j || inputs) return false;
                } else if (step.kind == 10) {
                    if (j || inputs) return false;
                    if (!route.lfo.create()) return false;
                    if (!route.lfo->config.decode(payload.cursor, length)) return false;
                    payload.cursor = payload.end;
                } else {
                    if (!step.parameters.resize(length)) return false;
                    std::memcpy(step.parameters.data(), payload.cursor, length); payload.cursor = payload.end;
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
                if (!route.steps.append(std::move(step))) return false;
            }
            if (!inputs && (route.steps.empty() || route.steps[0].kind < 8)) return false;
            if (!routes_.append(std::move(route))) return false;
        }
        if (!reader.ok || reader.cursor != reader.end || !binding_targets_.resize(count)
            || !binding_owners_.resize(count) || !downstream_reached_.resize(count)) return false;
        if (count && !build_owners(count, [&](unsigned i) -> const Route & { return routes_[i]; })) return false;
        if (!prepare_processing(processing_, count, [&](unsigned i) -> const Route & { return routes_[i]; })) return false;
        return validate_lfos();
    }
};
} // namespace cntrl::midi
