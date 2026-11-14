#pragma once
#include "midi_routing_types.h"
#include "midi_file.h"
#include "midi_stream_timeline.h"
#include "midi_lfo.h"
#include "numeric.h"
#include <cmath>
namespace cntrl::midi {
class AdvancedProcessing {
public:
    static constexpr bool advanced = true;
    using Factory = std::unique_ptr<Processor> (*)(const uint8_t *, unsigned);
    struct Registration { unsigned opcode, version; Factory factory; bool generator; };
    static CheckedList<Registration> &registry() { static CheckedList<Registration> result; return result; }
    static bool register_processor(unsigned opcode, unsigned version, Factory factory, bool generator = false) {
        if (opcode < 128 || opcode > 255 || !version || version > 255 || !factory) return false;
        for (const auto &entry : registry()) if (entry.opcode == opcode) return false;
        return registry().append({opcode, version, factory, generator}) != nullptr;
    }
    struct Step {
        uint8_t kind = 0, field = 0;
        CheckedArray<Field> match;
        CheckedArray<Assignment> assignments;
        int32_t value = 0, input_low = 0, input_high = 127, output_low = 0, output_high = 127;
        uint64_t time = 0;
        unsigned repeats = 1;
        float feedback = 1;
        unsigned bpm_milli = 120000;
        CheckedArray<uint8_t> parameters;
        std::unique_ptr<Processor> processor;
    };
    struct Route {
        char name[64] = {};
        uint16_t scope = 65535, outputs = 0;
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
        uint64_t preparation_epoch = 0;
        uint64_t file_finished_at = UINT64_MAX;
        CheckedObject<Lfo> lfo;
    };
#if defined(__arm__) && UINTPTR_MAX == UINT32_MAX
    static_assert(sizeof(Step) == 88);
#endif
    template<class Core> struct State {
        using Work = typename Core::Work;
        CheckedList<ParameterDelivery> parameters_;
        CheckedList<ParameterBinding> bindings_;
        CheckedArray<Parameter> binding_targets_;
        CheckedArray<uint64_t> binding_owners_;
        mutable CheckedArray<uint8_t> downstream_reached_;
        BeatPosition beat_;
        CheckedArray<unsigned> parameter_cursor_;
        CheckedList<Capture> captures_;
        CheckedList<Work> reverse_parameters_;
        CheckedList<Visit> visits_;
        CheckedList<Visit>::iterator release_visit_cursor_;
        CheckedList<Capture>::iterator release_capture_cursor_;
        CheckedList<Capture>::iterator pressure_capture_cursor_;
        StreamFileProvider *file_provider_ = nullptr;
    };
    template<class Core> static size_t retained_bytes(const Core &core) {
        const auto &state = core.extension_;
        size_t bytes = state.captures_.memory_bytes() + state.reverse_parameters_.memory_bytes()
            + state.visits_.memory_bytes() + state.parameters_.memory_bytes() + state.bindings_.memory_bytes()
            + state.binding_targets_.capacity() * sizeof(Parameter)
            + state.binding_owners_.capacity() * sizeof(uint64_t) + state.downstream_reached_.capacity()
            + state.parameter_cursor_.capacity() * sizeof(unsigned);
        for (const auto &route : core.routes_) if (route.lfo) bytes += sizeof(Lfo);
        return bytes;
    }
    template<class Core> struct GeneratedSink : Sink {
        using Work = typename Core::Work;
        static constexpr unsigned max_routes = Core::max_routes;
        Core &streams; unsigned route; uint64_t now;
        GeneratedSink(Core &streams, unsigned route, uint64_t now) : streams(streams), route(route), now(now) {}
        bool write_packet(unsigned, const Packet &packet) override { streams.generated(route, packet, now); return true; }
    };
    template<class Core> struct ProcessorOutput : Emit {
        using Work = typename Core::Work;
        static constexpr unsigned max_routes = Core::max_routes;
        Core &engine; unsigned route, position; Work work; unsigned credits;
        ProcessorOutput(Core &engine, unsigned route, unsigned position, Work work, unsigned credits)
            : engine(engine), route(route), position(position), work(work), credits(credits) {}
        bool emit(const Packet &packet, uint64_t delay = 0) override {
            Work transformed = work; transformed.packet = packet; transformed.due += delay; transformed.delay += delay;
            transformed.packet.source = work.packet.source;
            const unsigned destination = max_routes * position + route;
            unsigned kind = packet.bytes[0] >> 4;
            if (!work.token && (kind == 8 || (kind == 9 && !packet.bytes[2]))) {
                engine.release_input_note(work.input, transformed.packet, transformed.due, destination); return true;
            }
            if (Core::is_hold(packet) && packet.bytes[2] < 64)
                return engine.queue_input({route, position, transformed}, destination);
            if (!credits || engine.queued_events() + engine.voices_.size() >= engine.limits_.events) {
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
            if (Core::is_hold(packet)) return engine.queue_input({route, position, transformed}, destination);
            return engine.prepare({route, position, transformed}, true);
        }
    };
    template<class Core> static typename Core::LfoStatus lfo_status(const Core &core) {
        using LfoStatus = typename Core::LfoStatus;

        LfoStatus result;
        for (unsigned i = 0; i < core.routes_.size(); ++i) if (core.routes_[i].lfo) {
            if (core.routes_[i].failed) { ++result.failed_count; if (i < 64) result.failed |= uint64_t(1) << i; }
            if (core.active(i) && core.routes_[i].started && !core.routes_[i].lfo->ended) {
                if (core.routes_[i].lfo->waiting) ++result.waiting;
                else ++result.running;
            }
        }
        for (const auto &delivery : core.extension_.parameters_) {
            if (!delivery.finished && !delivery.initial) ++result.pending;
            if (!delivery.finished && delivery.stopping) ++result.stopping;
            if (!core.outputs_[delivery.output].present) ++result.missing;
        }
        return result;
    }
    template<class Core> static bool parameter_write(Core &core, unsigned output, const uint8_t *bytes, unsigned size, const Parameter *logical, uint64_t origin) {

        bool blocked = false;
        for (auto &delivery : core.extension_.parameters_) if (delivery.output == output && !delivery.finished && (!origin || delivery.lfo != origin)
            && (logical ? delivery.target.overlaps(*logical) || (logical->kind == Parameter::program && delivery.target.channel == logical->channel)
                : delivery.target.conflicts(bytes, size))) {
            bool bank = size >= 3 && bytes[0] >> 4 == 11 && (bytes[1] == 0 || bytes[1] == 32);
            core.stop_parameter(delivery, false);
            if (bank && delivery.target.kind == Parameter::program) delivery.off = 0;
            if (delivery.touched || delivery.flight.position) blocked = true;
        }
        return !blocked;
    }
    template<class Core> static unsigned clock_outputs(const Core &core) {

        unsigned mask = 0;
        for (unsigned i = 0; i < core.routes_.size(); ++i) if (core.active(i) && !core.routes_[i].steps.empty()) {
            const auto &step = core.routes_[i].steps.front();
            if (step.kind == 8 || (core.routes_[i].file && core.routes_[i].file->clock())) mask |= core.downstream_outputs(i);
        }
        return mask;
    }
    template<class Core> static bool parameter_step(const typename Core::Step &step, Parameter &target) {

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
    template<class Core> static bool parameter_bindings(Core &core, unsigned root) {

        core.extension_.bindings_.clear();
        std::fill(core.extension_.binding_targets_.begin(), core.extension_.binding_targets_.end(), Parameter{});
        std::fill(core.extension_.binding_owners_.begin(), core.extension_.binding_owners_.end(), uint64_t(0));
        auto &targets = core.extension_.binding_targets_;
        auto &owners = core.extension_.binding_owners_;
        targets[root] = core.routes_[root].lfo->config.target;
        for (unsigned i = root; i < core.routes_.size(); ++i) if (core.active(i)) {
            unsigned sources = 0;
            if (i == root) owners[i] = core.extend_owner(0, root);
            else for (const auto &input : core.routes_[i].inputs) if (input.kind && owners[input.index]) {
                if (++sources > 1) return false;
                targets[i] = targets[input.index]; owners[i] = core.extend_owner(owners[input.index], i);
            }
            if (!owners[i]) continue;
            unsigned begin = i == root ? 1 : 0;
            for (unsigned j = begin; j < core.routes_[i].steps.size(); ++j)
                if (!core.parameter_step(core.routes_[i].steps[j], targets[i])) return false;
            for (unsigned output = 0; output < core.outputs_count_; ++output) if (core.routes_[i].outputs & (1u << output)) {
                if (!core.extension_.bindings_.append({output, owners[i], targets[i]})) return false;
            }
        }
        return true;
    }
    template<class Core> static bool validate_lfos(Core &core) {
        using ParameterBinding = typename Core::ParameterBinding;
        constexpr auto system_scope = Core::system_scope;

        unsigned generators = 0;
        for (const auto &route : core.routes_) generators += bool(route.lfo);
        (void)generators;
        CheckedList<ParameterBinding> context_bindings;
        for (unsigned context = 0; context <= core.routes_.size(); ++context) {
            core.selected_ = context == core.routes_.size() ? system_scope : core.routes_[context].scope;
            context_bindings.clear();
            for (unsigned root = 0; root < core.routes_.size(); ++root) if (core.routes_[root].lfo && core.active(root)) {
                if (!core.parameter_bindings(root)) return false;
                for (const auto &binding : core.extension_.bindings_) {
                    if (core.routes_[root].lfo->config.off == 1 && (core.routes_[root].lfo->config.final_value < binding.target.minimum()
                        || core.routes_[root].lfo->config.final_value > binding.target.maximum())) return false;
                    for (const auto &other : context_bindings) if (binding.output == other.output && binding.target.overlaps(other.target)) return false;
                    if (!context_bindings.append(binding)) return false;
                }
            }
        }
        core.selected_ = system_scope; core.extension_.bindings_.clear(); return true;
    }
    template<class Core> static bool bind_lfo(Core &core, unsigned root, uint64_t id, bool fresh) {
        using ParameterDelivery = typename Core::ParameterDelivery;

        auto &route = core.routes_[root];
        if (!core.parameter_bindings(root)) { route.failed = true; ++core.diagnostics_.failed; return false; }
        unsigned needed = 0;
        for (const auto &binding : core.extension_.bindings_) {
            bool existing = false;
            for (const auto &delivery : core.extension_.parameters_) if (delivery.lfo == id && delivery.output == binding.output
                && core.same_owner(delivery.owner, binding.owner) && delivery.target.same(binding.target)) {
                if (!delivery.finished || (!fresh && !core.owner_activated(binding.owner))) existing = true;
            }
            if (existing || (!fresh && !core.owner_activated(binding.owner))) continue;
            ++needed;
            for (const auto &delivery : core.extension_.parameters_) if (!delivery.finished && delivery.output == binding.output && delivery.target.overlaps(binding.target)) {
                if (!delivery.stopping) { route.failed = true; ++core.diagnostics_.failed; }
                return false;
            }
            if (core.outputs_[binding.output].present && route.lfo->config.off == 2 && core.sink_->parameter_value(binding.output, binding.target) == Parameter::unknown) {
                route.failed = true; ++core.diagnostics_.failed; return false;
            }
        }
        if (!core.extension_.parameters_.reserve(core.extension_.parameters_.size() + needed)) { ++core.diagnostics_.overloaded; return false; }
        for (const auto &binding : core.extension_.bindings_) {
            bool existing = false;
            for (const auto &delivery : core.extension_.parameters_) if (delivery.lfo == id && delivery.output == binding.output
                && core.same_owner(delivery.owner, binding.owner) && delivery.target.same(binding.target)
                && (!delivery.finished || (!fresh && !core.owner_activated(binding.owner)))) existing = true;
            if (existing || (!fresh && !core.owner_activated(binding.owner))) continue;
            ParameterDelivery delivery{binding.output, binding.owner, id, core.outputs_[binding.output].identity, binding.target};
            delivery.off = route.lfo->config.off; delivery.final_value = route.lfo->config.final_value;
            delivery.snapshot = core.sink_->parameter_value(binding.output, binding.target);
            delivery.finished = !core.outputs_[binding.output].present;
            if (!core.extension_.parameters_.append(delivery)) return false;
        }
        return true;
    }
    template<class Core> static void service_lfos(Core &core, uint64_t now, unsigned &used, unsigned budget) {
        using Work = typename Core::Work;
        using Event = typename Core::Event;
        using Processing = typename Core::Processing;
        using Capture = typename Core::Capture;
        constexpr auto system_scope = Core::system_scope;

        bool synchronized = true;
        for (unsigned root = 0; root < core.routes_.size() && used < budget; ++root) {
            auto &route = core.routes_[root];
            if (!route.lfo || (route.scope != system_scope && (route.scope != core.selected_ || route.foreign != core.external_selected_))) continue;
            auto &lfo = *route.lfo;
            lfo.advance(now, core.extension_.beat_, core.active(root) && route.started);
            if (!core.active(root)) continue;
            if (!route.started) {
                uint64_t id = core.serial_ + 1;
                if (!core.bind_lfo(root, id, true)) { synchronized = false; continue; }
                ++core.serial_; lfo.enable(now, core.extension_.beat_, id); route.started = true;
            } else if (core.activation_pending_ && !core.bind_lfo(root, lfo.id, false)) { synchronized = false; continue; }
            if (lfo.ended || (lfo.generated && (now < lfo.next || lfo.waiting))) continue;
            bool terminal = lfo.generated && lfo.complete();
            uint8_t sample = !lfo.generated ? 0 : terminal ? 2 : 1;
            if (terminal) {
                core.erase_if(core.events_, [&](const Event &event) { return event.lfo == lfo.id && event.sample == 1; });
                core.erase_if(core.pending_events_, [&](const Event &event) { return event.lfo == lfo.id && event.sample == 1; });
                core.erase_if(core.extension_.captures_, [&](const Capture &capture) { return capture.work.lfo == lfo.id && capture.work.sample == 1; });
                core.erase_if(core.processing_, [&](const Processing &work) { return work.work.lfo == lfo.id && work.work.sample == 1; });
            }
            if (lfo.generated && now > lfo.next) core.diagnostics_.skipped += uint32_t((now - lfo.next) / lfo.config.interval);
            Parameter value = lfo.config.target; value.value = lfo.sample(terminal);
            uint8_t bytes[3] = {}; value.message(0, bytes);
            Work work{{uint8_t(bytes[0] >> 4), {bytes[0], bytes[1], bytes[2]}}, now, 0, Core::max_routes + root, 0, value, lfo.id, sample};
            core.process(root, 1, work); ++used;
            lfo.generated = true; lfo.ended = terminal; lfo.next = now + lfo.config.interval;
        }
        if (synchronized) { core.activation_pending_ = false; for (auto &route : core.routes_) route.activated = false; }
    }
    template<class Core> static void stop_parameter(Core &core, typename Core::ParameterDelivery &delivery, bool terminal) {
        (void)core;

        if (delivery.finished) return;
        delivery.stopping = true;
        delivery.values[1] = Parameter::unknown;
        if (!terminal) delivery.values[0] = delivery.values[2] = Parameter::unknown;
        if (!delivery.flight.position && (!terminal || delivery.sample == 1)) delivery.flight = {};
    }
    template<class Core> static void accept_parameter(Core &core, const typename Core::Event &event) {

        for (auto &delivery : core.extension_.parameters_) if (delivery.lfo == event.lfo && core.same_owner(delivery.owner, event.owner)
            && delivery.output == event.output && delivery.target.same(event.parameter)) {
            if (delivery.finished || delivery.stopping) continue;
            unsigned slot = event.sample;
            if (slot == 1 && delivery.flight.token && !delivery.flight.position && delivery.sample == 1) {
                delivery.flight.parameter.value = event.parameter.value;
                ++core.diagnostics_.coalesced; return;
            }
            if (!delivery.initial && delivery.values[0] == Parameter::unknown && slot == 1
                && !(delivery.flight.token && delivery.sample == 0)) slot = 0;
            if (slot == 1 && delivery.values[slot] != Parameter::unknown) ++core.diagnostics_.coalesced;
            if (slot != 1 && delivery.values[slot] != Parameter::unknown) return;
            delivery.values[slot] = event.parameter.value; return;
        }
    }
    template<class Core> static void service_parameters(Core &core, Sink &sink, unsigned available, unsigned busy, unsigned exclusive, unsigned &used, unsigned budget, unsigned delivered) {

        if (core.extension_.parameters_.empty()) return;
        for (unsigned output = 0; output < core.outputs_.size() && used < budget; ++output) {
            unsigned bit = 1u << output;
            if ((delivered & bit) || !(available & bit) || !core.outputs_[output].present || ((exclusive | core.sysex_outputs_) & bit)) continue;
            uint64_t lock = 0;
            for (const auto &d : core.extension_.parameters_) if (d.output == output && d.flight.position) lock = d.flight.token;
            for (unsigned scan = 0; scan < core.extension_.parameters_.size(); ++scan) {
                unsigned index = (core.extension_.parameter_cursor_[output] + scan) % unsigned(core.extension_.parameters_.size());
                auto &delivery = core.extension_.parameters_[index];
                if (delivery.output != output || delivery.finished || core.outputs_[output].identity != delivery.identity
                    || (lock && delivery.flight.token != lock)) continue;
                if (!delivery.flight.token) {
                    if (!delivery.stopping && (busy & bit)) continue;
                    int value = Parameter::unknown;
                    unsigned slot = 0;
                    if (delivery.values[0] != Parameter::unknown) { value = delivery.values[0]; slot = 0; }
                    else if (delivery.values[2] != Parameter::unknown) {
                        bool initial_pending = false;
                        for (const auto &event : core.events_) if (event.lfo == delivery.lfo && event.sample == 0 && event.output == delivery.output) initial_pending = true;
                        for (const auto &capture : core.extension_.captures_) if (capture.work.lfo == delivery.lfo && capture.work.sample == 0) initial_pending = true;
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
                    delivery.flight = {delivery.target, ++core.serial_, 0, delivery.lfo}; delivery.flight.parameter.value = value;
                }
                ++used;
                core.extension_.parameter_cursor_[output] = (index + 1) % unsigned(core.extension_.parameters_.size());
                if (!sink.submit_parameter(delivery.output, delivery.flight)) break;
                ++core.diagnostics_.transmitted; delivered |= bit; delivery.touched = true;
                if (delivery.flight.position == delivery.flight.parameter.messages()) {
                    delivery.last = delivery.flight.parameter.value; delivery.flight = {};
                    if (delivery.sample == 0) delivery.initial = true;
                    if (delivery.sample == 3) delivery.finished = true;
                }
                break;
            }
        }
    }
    template<class Core> static void generated(Core &core, unsigned route, const Packet &packet, uint64_t now) {

        unsigned input = Core::max_routes + route;
        unsigned kind = packet.bytes[0] >> 4;
        uint64_t token = 0;
        if (kind == 9 && packet.bytes[2]) {
            if (!core.admit_note(input, packet, token)) return;
        } else if (kind == 8 || (kind == 9 && !packet.bytes[2])) {
            core.release_input_note(input, packet, now); return;
        }
        core.queue_input({route, 1, {packet, now, token, input, 0}});
    }
    template<class Core> static void notify_release(Core &core, uint64_t token, uint8_t velocity, uint64_t now) {
        using Work = typename Core::Work;
        using Visit = typename Core::Visit;
        for (;;) {
            auto pending = std::find_if(core.extension_.visits_.begin(), core.extension_.visits_.end(), [token](const Visit &entry) { return entry.token == token; });
            if (pending == core.extension_.visits_.end()) break;
            const Visit visit = *pending;
            if (pending == core.extension_.release_visit_cursor_) ++core.extension_.release_visit_cursor_;
            core.extension_.visits_.erase(pending);
            Packet release{8, {uint8_t(0x80 | (visit.onset.bytes[0] & 15)), visit.onset.bytes[1], velocity}};
            Work work{release, now, 0, Core::max_routes * (visit.step + 1) + visit.route, visit.owner};
            ProcessorOutput<Core> output(core, visit.route, visit.step + 1, work, core.preparation_emit_budget_);
            auto &processor = core.routes_[visit.route].steps[visit.step].processor;
            if (processor && !processor->push(release, visit.input, output)) { ++core.diagnostics_.failed; core.overload_route(visit.route); }
        }
    }
    template<class Core> static void reverse_group(Core &core, unsigned route, unsigned step, uint64_t now, bool finite) {
        using Work = typename Core::Work;
        using Preparation = typename Core::Preparation;
        using Capture = typename Core::Capture;

        uint64_t window = core.routes_[route].steps[step].time;
        uint64_t begin = finite ? core.routes_[route].next : UINT64_MAX;
        for (const auto &capture : core.extension_.captures_) if (capture.route == route && capture.step == step) {
            uint64_t time = capture.frame ? capture.end : capture.start;
            if (capture.frame && !capture.closed) continue;
            if (!finite) begin = std::min(begin, time / window * window);
        }
        if (begin == UINT64_MAX || now < begin) return;
        uint64_t boundary = finite ? now : begin + window;
        if (now < boundary) return;
        // A service stall cannot turn into an unbounded burst of old windows.
        if (!finite && now - boundary >= window) { core.overload_route(route); return; }
        CheckedArray<TimelinePacket> sequence;
        struct Token { uint64_t old, fresh; };
        CheckedList<Token> tokens;
        unsigned selected = 0;
        auto eligible = [&](const Capture &capture) {
            if (capture.route != route || capture.step != step || (capture.frame && !capture.closed)) return false;
            const uint64_t time = capture.frame ? capture.end : capture.start;
            return time >= begin && (finite || time < boundary);
        };
        for (const auto &capture : core.extension_.captures_) if (eligible(capture)) ++selected;
        if (selected > UINT32_MAX / 2 || !sequence.reserve(selected * 2)
            || !tokens.reserve(selected) || !core.extension_.reverse_parameters_.reserve(selected)) { ++core.diagnostics_.dropped; return; }
        core.extension_.reverse_parameters_.clear();
        for (auto capture = core.extension_.captures_.begin(); capture != core.extension_.captures_.end(); ++capture) {
            if (!eligible(*capture)) continue;
            uint64_t time = capture->frame ? capture->end : capture->start;
            if (capture->work.parameter.kind) {
                Work reversed = capture->work;
                reversed.due = boundary + boundary - capture->start;
                reversed.delay = 0;
                if (!core.extension_.reverse_parameters_.append(reversed)) { ++core.diagnostics_.dropped; return; }
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
        if (!reverse_sequence(sequence, boundary - begin)) { ++core.diagnostics_.dropped; return; }
        if (!core.processing_.reserve(core.processing_.size() + core.terminal_credits() + sequence.size() + core.extension_.reverse_parameters_.size())) {
            ++core.diagnostics_.dropped; return;
        }
        for (auto capture = core.extension_.captures_.begin(); capture != core.extension_.captures_.end();) {
            if (!eligible(*capture)) { ++capture; continue; }
            bool note = capture->work.packet.bytes[0] >> 4 == 9 && capture->work.token;
            // Carry only the last held value into the next capture window.
            bool pedal = core.is_hold(capture->work.packet) && capture->work.packet.bytes[2] >= 64;
            bool following = false;
            if (pedal) for (const auto &later : core.extension_.captures_) {
                if (&later == &*capture) { following = true; continue; }
                if (eligible(later) && core.is_hold(later.work.packet)
                    && later.work.input == capture->work.input
                    && core.same_owner(later.work.owner, capture->work.owner)
                    && later.work.packet.source == capture->work.packet.source
                    && later.work.packet.bytes[0] == capture->work.packet.bytes[0]
                    && later.work.packet.bytes[1] == capture->work.packet.bytes[1]
                    && (later.start > capture->start || (later.start == capture->start && following))) {
                    pedal = false; break;
                }
            }
            if (!finite && ((note && (!capture->closed || capture->end > boundary)) || pedal)) {
                capture->start = boundary; ++capture;
            } else {
                if (capture == core.extension_.release_capture_cursor_) ++core.extension_.release_capture_cursor_;
                if (capture == core.extension_.pressure_capture_cursor_) ++core.extension_.pressure_capture_cursor_;
                capture = core.extension_.captures_.erase(capture);
            }
        }
        for (const auto &value : core.extension_.reverse_parameters_) core.process(route, step + 1, value);
        for (const auto &event : sequence) {
            Work reversed{event.packet, boundary + event.time, event.token, event.source, event.owner};
            reversed.packet.source = 65535;
            unsigned kind = event.packet.bytes[0] >> 4;
            if (kind == 9 && event.token) {
                reversed.token = ++core.serial_;
                if (!tokens.append({event.token, reversed.token})) return;
            } else if (kind == 8 && event.token) {
                auto token = std::find_if(tokens.begin(), tokens.end(), [&](const Token &entry) { return entry.old == event.token; });
                if (token == tokens.end()) continue;
                reversed.token = token->fresh;
                core.prepare({0, 0, reversed, Preparation::release});
                tokens.erase(token); continue;
            }
            auto previous = core.current_time_; core.current_time_ = reversed.due;
            core.process(route, step + 1, reversed);
            core.current_time_ = previous;
        }
    }
    template<class Core> static void finish_reverse(Core &core, unsigned owner_route, uint64_t now) {

        for (unsigned route = 0; route < core.routes_.size(); ++route)
            for (unsigned step = 0; step < core.routes_[route].steps.size(); ++step)
                if (core.routes_[route].steps[step].kind == 7 && !core.routes_[route].steps[step].time) {
                    bool owned = false;
                    for (const auto &capture : core.extension_.captures_) if (capture.route == route && capture.step == step
                        && core.owns(capture.work.owner, owner_route)) owned = true;
                    if (owned) core.reverse_group(route, step, now, true);
                }
    }
    template<class Core> static void flush_reverse(Core &core, uint64_t now) {

        for (unsigned route = 0; route < core.routes_.size(); ++route) {
            bool timed_out = false;
            for (const auto &capture : core.extension_.captures_) if (capture.route == route && capture.frame && !capture.closed) {
                uint64_t latest = capture.start;
                for (const auto &other : core.extension_.captures_) if (other.frame == capture.frame) latest = std::max(latest, other.start);
                if (now >= latest && now - latest >= 1000000) timed_out = true;
            }
            if (timed_out) core.overload_route(route);
        }
        for (unsigned route = 0; route < core.routes_.size(); ++route)
            for (unsigned step = 0; step < core.routes_[route].steps.size(); ++step)
                if (core.routes_[route].steps[step].kind == 7 && core.routes_[route].steps[step].time)
                    core.reverse_group(route, step, now, false);
    }
    template<class Core> static bool numeric_fields(const typename Core::Step &step, typename Core::Work &work) {
        if (step.kind == 5 && std::all_of(step.assignments.begin(), step.assignments.end(),
            [](const Assignment &assignment) { return assignment.field == 1 && assignment.source == 255; }))
            return BasicProcessing::template numeric_fields<Core>(step, work);
        if (work.parameter.kind) {
            if (!Core::matches(step, work)) return true;
            if ((step.kind == 2 || step.kind == 3) && work.parameter.kind == Parameter::key_pressure)
                return work.parameter.set(2, step.kind == 2 ? work.parameter.address + step.value : 2 * step.value - work.parameter.address);
            if (step.kind == 4) {
                int original = Core::work_field(work, step.field);
                if (original == Parameter::unknown) return true;
                int low = step.input_low, high = step.input_high;
                if (low == -1 && high == -1) { low = work.parameter.minimum(); high = work.parameter.maximum(); }
                return work.parameter.set(step.field, scale_integer(std::clamp(original,low,high),low,high,step.output_low,step.output_high));
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
        if ((step.kind == 2 || step.kind == 3) && Core::field(work.packet, 2, work.input) >= 0) {
            int pitch = work.packet.bytes[1]; pitch = step.kind == 2 ? pitch + step.value : 2 * step.value - pitch;
            if (pitch < 0 || pitch > 127) return false;
            work.packet.bytes[1] = uint8_t(pitch); return true;
        }
        if (!Core::matches(step, work)) return true;
        if (step.kind == 4) {
            int original = Core::field(work.packet, step.field, work.input);
            if (original < 0) return true;
            int minimum = step.field == 1 || step.field == 6 ? 1 : 0;
            int maximum = step.field == 1 ? 16 : step.field == 6 ? 128 : step.field == 5 && (work.packet.bytes[0] >> 4 == 14 || work.packet.bytes[0] == 0xf2) ? 16383 : 127;
            int low = step.input_low, high = step.input_high;
            if (low == -1 && high == -1) { low = minimum; high = maximum; }
            int value = scale_integer(std::clamp(original,low,high),low,high,step.output_low,step.output_high);
            return Core::set(work.packet, step.field, std::clamp(value, minimum, maximum));
        }
        if (step.kind == 5) {
            Packet original = work.packet, mapped = original;
            for (const auto &assignment : step.assignments) {
                if (assignment.field && Core::field(mapped, assignment.field, work.input) < 0) return true;
                int value = assignment.value;
                if (assignment.source != 255) { int source = Core::field(original, assignment.source, work.input); if (source < 0) return true; value += source; }
                if (!Core::set(mapped, assignment.field, value)) return false;
            }
            work.packet = mapped;
        }
        return true;
    }

    template<class Core> static bool modulation(const Core &core) {

        bool modulation = false;
        for (unsigned i = 0; i < core.routes_.size(); ++i) if (core.routes_[i].lfo && core.active(i)) modulation = true;
        for (const auto &delivery : core.extension_.parameters_) if (!delivery.finished) modulation = true;
        return modulation;
    }

    template<class Core> static void service_generators(Core &core, uint64_t now, unsigned &used, unsigned work_budget) {
        using Work = typename Core::Work;
        using Reader = typename Core::Reader;
        constexpr auto system_scope = Core::system_scope;
        for (unsigned i = 0; i < core.routes_.size(); ++i) if (core.active(i) && !core.routes_[i].steps.empty() && core.routes_[i].steps[0].kind == 8) {
            auto &route = core.routes_[i];
            if (!route.started) { route.clock.start(now, route.steps[0].bpm_milli); route.started = true; }
            if (route.clock.poll(now) && used < work_budget) {
                Work work{{15, {0xf8, 0, 0}}, now, 0, system_scope, 0};
                core.process(i, 1, work); ++used;
            }
        }
        for (unsigned i = 0; i < core.routes_.size() && used < work_budget; ++i) if (core.active(i) && !core.routes_[i].steps.empty() && core.routes_[i].steps[0].kind == 9) {
            auto &route = core.routes_[i];
            if (!route.started) {
                route.started = true;
                route.file_finished_at = UINT64_MAX;
                const auto &bytes = route.steps[0].parameters;
                PlayMidiFilePatch settings;
                std::memcpy(settings.filename, bytes.data(), 96);
                Reader reader{bytes.data() + 96, bytes.data() + bytes.size()};
                settings.loops = uint32_t(reader.integer(4));
                settings.pre_delay = uint32_t(reader.integer(8) / 1000000); settings.post_delay = uint32_t(reader.integer(8) / 1000000);
                uint64_t speed = reader.integer(8); std::memcpy(&settings.speed, &speed, 8);
                settings.reverse = reader.byte() != 0; settings.transpose = int8_t(reader.byte()); settings.outputs = 1;
                if (core.extension_.file_provider_) route.file = core.extension_.file_provider_->open_stream(settings, i);
                if (!route.file) { route.failed = true; ++core.diagnostics_.failed; continue; }
            }
            if (route.file) {
                GeneratedSink<Core> output(core, i, now);
                if (!route.file->finished()) route.file->poll(uint32_t(now), output);
                ++used;
                if (route.file->failed()) { core.retire(i); route.failed = true; ++core.diagnostics_.failed; }
                else if (route.file->finished()) {
                    if (route.file_finished_at == UINT64_MAX) route.file_finished_at = now;
                    bool preparing = false;
                    for (const auto &work : core.processing_) if (work.work.input == Core::max_routes + i) preparing = true;
                    if (!preparing) { core.finish_reverse(i, route.file_finished_at); route.file.reset(); }
                }
            }
        }
        for (unsigned i = 0; i < core.routes_.size() && used < work_budget; ++i) if (core.active(i)) {
            for (unsigned position = 0; position < core.routes_[i].steps.size() && used < work_budget; ++position) {
                auto &step = core.routes_[i].steps[position];
                if (!step.processor) continue;
                Work work{{}, now, 0, Core::max_routes * (position + 1) + i, core.extend_owner(0, i)};
                ProcessorOutput<Core> output(core, i, position + 1, work, work_budget - used);
                step.processor->service(now, work_budget - used, output); ++used;
            }
        }
        core.flush_reverse(now);
    }

    template<class Core> static void finish_parameters(Core &core) {
        using ParameterDelivery = typename Core::ParameterDelivery;

        for (unsigned i = 0; i < core.routes_.size(); ++i) if (core.routes_[i].lfo && core.routes_[i].lfo->ended && !core.routes_[i].lfo->finishing) {
            auto &lfo = *core.routes_[i].lfo;
            bool pending = false;
            for (const auto &event : core.events_) if (event.lfo == lfo.id && event.sample != 1) pending = true;
            for (const auto &capture : core.extension_.captures_) if (capture.work.lfo == lfo.id && capture.work.sample != 1) pending = true;
            for (const auto &work : core.processing_) if (work.work.lfo == lfo.id && work.work.sample != 1) pending = true;
            for (const auto &event : core.pending_events_) if (event.lfo == lfo.id && event.sample != 1) pending = true;
            if (!pending) {
                lfo.finishing = true; core.routes_[i].enabled = false;
                for (auto &delivery : core.extension_.parameters_) if (delivery.lfo == lfo.id) core.stop_parameter(delivery, true);
            }
        }
        core.erase_if(core.extension_.parameters_, [&](const ParameterDelivery &delivery) {
            if (!delivery.finished) return false;
            for (unsigned i = 0; i < core.routes_.size(); ++i) if (core.owns(delivery.owner, i) && !core.active(i)) return true;
            for (const auto &route : core.routes_) if (route.lfo && route.lfo->id == delivery.lfo) return false;
            return true;
        });
    }

    template<class Core> static void process_step(Core &core, typename Core::Processing next) {
        using Work = typename Core::Work;
        using Preparation = typename Core::Preparation;
        using Capture = typename Core::Capture;
        using Visit = typename Core::Visit;

        const unsigned route = next.route, position = next.position;
        Work work = next.work;
        auto &entry = core.routes_[route];
        if (next.phase == Preparation::echo) {
            const auto &step = entry.steps[position];
            const unsigned copy = next.cursor;
            if (copy > step.repeats) return;
            ++next.cursor;
            Work echo = work; echo.due += step.time * copy; echo.delay += step.time * copy;
            if (echo.packet.bytes[0] >> 4 == 9) {
                const int velocity = int(std::lround(echo.packet.bytes[2] * std::pow(step.feedback, float(copy))));
                if (velocity <= 0) { core.prepare(next); return; }
                echo.packet.bytes[2] = uint8_t(std::min(127, velocity));
            }
            core.process(route, position + 1, echo); core.prepare(next); return;
        }
        if (next.phase == Preparation::steps && position < entry.steps.size()) {
            const unsigned i = position;
            auto &step = entry.steps[i];
            if (step.processor) {
                if (work.token && work.packet.bytes[0] >> 4 == 9) {
                    auto visit = std::find_if(core.extension_.visits_.begin(), core.extension_.visits_.end(), [&](const Visit &entry) { return entry.token == work.token && entry.route == route && entry.step == i; });
                    if (visit == core.extension_.visits_.end()) {
                        if (core.extension_.visits_.size() >= core.limits_.voices) { core.overload_route(route); return; }
                        if (!core.extension_.visits_.append({work.token, work.owner, route, i, work.input, work.packet})) { ++core.diagnostics_.dropped; return; }
                    }
                }
                ProcessorOutput<Core> output(core, route, i + 1, work, core.preparation_emit_budget_);
                if (!step.processor->push(work.packet, work.input, output)) { ++core.diagnostics_.failed; core.overload(work.owner); }
                return;
            }
            bool match = core.matches(step, work);
            if ((step.kind == 0 && !match) || (step.kind == 1 && match)) return;
            if ((step.kind == 2 || step.kind == 3) && core.field(work.packet, 2, work.input) >= 0) {
                if (work.parameter.kind) {
                    if (!core.numeric_fields(step, work)) return;
                    core.process(route, i + 1, work); return;
                }
                int pitch = work.packet.bytes[1];
                pitch = step.kind == 2 ? pitch + step.value : 2 * step.value - pitch;
                if (pitch < 0 || pitch > 127) return;
                work.packet.bytes[1] = uint8_t(pitch);
            } else if (step.kind == 4 || step.kind == 5) {
                if (!core.numeric_fields(step, work)) return;
            } else if (step.kind == 6) {
                core.prepare({route, i, work, Preparation::echo, 1});
                return;
            } else if (step.kind == 7) {
                if (core.extension_.captures_.size() >= core.limits_.captures) { core.overload(work.owner); return; }
                Capture capture{work, route, i, work.due};
                capture.delay = work.delay;
                bool note = work.packet.bytes[0] >> 4 == 9 && work.token;
                unsigned cin = work.packet.cin;
                bool sysex = cin == 4 || (cin >= 5 && cin <= 7
                    && work.packet.bytes[work.packet.size() - 1] == 0xf7);
                capture.closed = !note && !sysex;
                capture.end = capture.closed ? work.due : 0;
                if (sysex) {
                    for (const auto &previous : core.extension_.captures_)
                        if (previous.route == route && previous.step == i
                            && previous.work.input == work.input && previous.frame && !previous.closed)
                            capture.frame = previous.frame;
                    if (work.packet.bytes[0] == 0xf0) {
                        if (capture.frame) { core.overload(work.owner); return; }
                        capture.frame = ++core.serial_;
                    }
                    if (!capture.frame) { ++core.diagnostics_.dropped; return; }
                    if (cin != 4) {
                        capture.closed = true; capture.end = work.due;
                        for (auto &previous : core.extension_.captures_) if (previous.frame == capture.frame) {
                            previous.closed = true; previous.end = work.due;
                        }
                    }
                }
                if (!core.extension_.captures_.append(capture)) ++core.diagnostics_.dropped;
                return;
            }
            core.process(route, i + 1, work); return;
        }
    }

    template<class Core> static bool decode_step(Core &, typename Core::Route &route, typename Core::Step &step, typename Core::Reader &payload, unsigned length, unsigned reserved, unsigned j, unsigned inputs) {
        using Reader = typename Core::Reader;

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
            uint64_t bits = payload.integer(8);
            if (!positive_finite_binary64(bits) || bits > 0x408f400000000000ull /* 1000 BPM */ || j || inputs) return false;
            step.bpm_milli = unsigned(scale_binary64(1000,bits,true));
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
            if (pre % 1000000 || post % 1000000 || pre / 1000000 > UINT32_MAX || post / 1000000 > UINT32_MAX || !positive_finite_binary64(bits) || settings.byte() > 1) return false;
            int transpose = int8_t(settings.byte()); if (transpose == -128) return false;
        }
        return true;
    }
};
} // namespace cntrl::midi
#include "midi_routing.h"
namespace cntrl::midi { using Streams = Routing<AdvancedProcessing>; }
