#pragma once
#include "ui/presentation.h"
#include "ui/prompts.h"
#include "patch_executor.h"
#include "switch_gestures.h"
#include "control_bindings.h"
#include "memory.h"
#include "checked_list.h"

namespace cntrl {
class Switches : public CheckedHeapAllocation {
public:
    static constexpr unsigned capacity = 8;
    Switches(Hardware &hardware, PatchOutputs &outputs, PatchSources &sources)
        : hardware_(hardware), outputs_(outputs), sources_(sources) {}
    ~Switches() { destroy_states(); unpin(); if (pending_invocation_) sources_.end(); }
    bool open() const { return open_ || pending_invocation_; }
    bool performance() const { return bool(bindings_); }
    unsigned revision() const { return revision_; }
    ExpressionAssignment expression(unsigned input) const { return bindings_.expression(input); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(switches_bind_controls)
#endif
    midi::Bridge::Delivery bind_controls(const Document &document, unsigned node, PatchExecutor &invocation) {
        if (pending_invocation_ && pending_invocation_ != &invocation) return midi::Bridge::Delivery::pending;
        if (!pending_invocation_ || pending_node_ != node || pending_source_ != document.data()) {
            prepared_controls_.reset();
            if (!pending_.resize(document.length())) return midi::Bridge::Delivery::cancelled;
            std::memcpy(pending_.data, document.data(), document.length());
            if (!pending_document_.load(pending_.data, pending_.size, hardware_)) { pending_.clear(); return midi::Bridge::Delivery::cancelled; }
            prepared_controls_.reset(new (std::nothrow) Switches(hardware_, outputs_, sources_));
            if (!prepared_controls_) { pending_.clear(); return midi::Bridge::Delivery::cancelled; }
            prepared_controls_->initialize(pending_document_, held_, ControlBindings{pending_document_.node(node) + 4});
            if (!prepared_controls_->open_) { prepared_controls_.reset(); pending_.clear(); return midi::Bridge::Delivery::cancelled; }
            if (!pending_invocation_) sources_.begin();
            pending_invocation_ = &invocation; pending_node_ = node; pending_source_ = document.data();
        }
        return prepare_leave(invocation);
    }
    std::optional<uint16_t> debounce_ms(unsigned button) const {
        if (!open_ || closing_) return std::nullopt;
        for (unsigned i = 0; i < count_; ++i)
            if (states_[i].record[0] == button && states_[i].settings) return u16(states_[i].settings + gesture_debounce_offset);
        return std::nullopt;
    }
    bool closing() const { return closing_; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(switches_shift)
#endif
    bool shift(bool enabled) {
        if (!open_) return false;
        enabled = enabled && !closing_;
        if (shifted_ == enabled) return true;
        shifted_ = enabled; dirty_ = true;
        for (unsigned i = 0; i < count_; ++i) {
            auto &s = states_[i];
            if (!s.shifted) continue;
            if (held_ & (1u << s.record[0])) continue;
            s.input_layer = shifted_ && s.shifted;
            s.settings = s.input_layer ? (u16(s.shifted + 4 + gesture_debounce_offset) ? s.shifted + 4 : nullptr)
                : gestures(s.record[0]);
            s.gesture.reset(s.settings);
        }
        return true;
    }
    bool abandoned() const { return abandoned_; }
    bool owns(const PatchExecutor &executor) const {
        for (unsigned i = 0; i < count_; ++i) if (&states_[i].executor == &executor) return true;
        return false;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(switches_prepare_leave)
#endif
    midi::Bridge::Delivery prepare_leave(PatchExecutor &invocation) {
        if (!open_) return midi::Bridge::Delivery::complete;
        const bool first = !closing_;
        closing_ = dirty_ = true; shifted_ = false;
        clear_events(); transition_ = 0;
        bool done = true;
        for (unsigned i = 0; i < count_; ++i) {
            auto &s = states_[i];
            if (&s.executor == &invocation) continue;
            s.requested = false;
            if (!s.owned) s.active = false;
            if (first) cancel_gestures(s);
            if (s.unknown) return midi::Bridge::Delivery::cancelled;
            if (s.executor.busy() || s.active || s.presses || s.queued) done = false;
        }
        return done ? midi::Bridge::Delivery::complete : midi::Bridge::Delivery::pending;
    }
    // Targets with flash placement keep page setup out of instruction RAM.
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(switches_enter)
#endif
    bool enter(const Document &document, uint32_t held, ControlBindings bindings = {}) {
        Switches candidate(hardware_, outputs_, sources_);
        candidate.initialize(document, held, bindings);
        if (!candidate.open_) return false;
        adopt(candidate); return true;
    }
private:
    void adopt(Switches &candidate) {
        destroy_states(); unpin();
        state_storage_.swap(candidate.state_storage_); states_ = candidate.states_; candidate.states_ = nullptr;
        count_ = candidate.count_; candidate.count_ = 0;
        document_ = candidate.document_; bindings_ = candidate.bindings_; ++revision_;
        pinned_ = candidate.pinned_; candidate.pinned_ = false;
        held_ = disarmed_ = candidate.held_; open_ = dirty_ = true; closing_ = abandoned_ = shifted_ = false;
        selected_ = -1; clear_events(); transition_ = 0; hooks_enabled_ = candidate.hooks_enabled_; exit_started_ = false;
        render();
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(switches_initialize)
#endif
    void initialize(const Document &document, uint32_t held, ControlBindings bindings) {
        const unsigned count = bindings ? bindings.count() : document.switches();
        if (!state_storage_.resize(count * sizeof(State))) { hardware_.error("Switch state memory exhausted"); return; }
        states_ = reinterpret_cast<State *>(state_storage_.data);
        for (unsigned i = 0; i < count; ++i) new (states_ + i) State(hardware_, outputs_, sources_, i);
        count_ = count;
        unpin(); sources_.begin(); pinned_ = true;
        document_ = document; bindings_ = bindings; ++revision_;
        count_ = bindings_ ? bindings_.count() : document.switches(); held_ = disarmed_ = held;
        open_ = dirty_ = true; closing_ = abandoned_ = false;
        shifted_ = false;
        selected_ = -1; clear_events(); transition_ = 0; hooks_enabled_ = false; exit_started_ = false;
        for (unsigned i = 0; i < count_; ++i) {
            State &s = states_[i]; s.executor.cancel();
            if (!s.executor.retain_owner()) { hardware_.error("Switch delivery memory unavailable"); open_ = false; unpin(); return; }
            s.record = bindings_ ? bindings_.record(i) : document_.switch_assignment(i);
            s.actions = s.record + 2; s.shifted = bindings_ ? (u16(s.record + binding_shifted_offset) != 65535 ? s.record + binding_shifted_offset : nullptr) : document_.switch_shifted(s.record[0]);
            s.layer = s.input_layer = false;
            s.settings = gestures(s.record[0]);
            s.hooks = bindings_ ? (u16(s.record + binding_hooks_offset) != 65535 || u16(s.record + binding_hooks_offset + 2) != 65535 ? s.record + binding_hooks_offset : nullptr) : document_.switch_hooks(s.record[0]);
            if (s.hooks && (u16(s.hooks) != 65535 || u16(s.hooks + 2) != 65535)) hooks_enabled_ = true;
            if (s.shifted) hooks_enabled_ = true;
            for (unsigned layer = 0; layer < 2; ++layer) {
                s.on[layer].count = s.off[layer].count = 0;
                s.on[layer].complete = s.off[layer].complete = false;
            }
            s.gesture.reset(s.settings); clear_queue(s);
            s.requested = s.active = s.unknown = s.failed = s.owned = false; s.presses = 0;
            s.resource_count = 0;
            constexpr unsigned layer_actions = 2 + gesture_event_count;
            for (unsigned branch = 0; branch < 2 * layer_actions + 2; ++branch) {
                unsigned layer = branch / layer_actions, part = branch % layer_actions, node = 65535;
                if (branch < 2 * layer_actions) {
                    const uint8_t *actions = layer ? s.shifted : s.record + 2;
                    const uint8_t *settings = layer ? (s.shifted && u16(s.shifted + 4 + gesture_debounce_offset) ? s.shifted + 4 : nullptr) : s.settings;
                    if (actions && part < 2) node = u16(actions + part * 2);
                    else if (settings) node = u16(settings + (part - 2) * 2);
                } else if (s.hooks) node = u16(s.hooks + (branch - 2 * layer_actions) * 2);
                if (node == 65535) continue;
                if (!s.executor.start(document_, node, s.record[1] != 0 || s.settings || s.hooks || s.shifted)) {
                    s.executor.cancel(); open_ = false; unpin(); return;
                }
                unsigned resources_count = s.executor.resource_count();
                if (branch < 2 * layer_actions && part < 2) {
                    Pattern &pattern = part ? s.off[layer] : s.on[layer];
                    if (!capture(pattern, s.executor)) {
                        s.executor.cancel(); hardware_.error("Switch state memory exhausted"); open_ = false; unpin(); return;
                    }
                }
                if (!s.resources.resize(s.resource_count + resources_count)) {
                    s.executor.cancel(); hardware_.error("Switch ownership memory exhausted"); open_ = false; unpin(); return;
                }
                if (resources_count) std::memcpy(s.resources.data() + s.resource_count, s.executor.resources(), resources_count * sizeof(PatchExecutor::Resource));
                s.resource_count += resources_count; s.executor.cancel();
            }
            for (unsigned j = 0; j < i; ++j)
                for (unsigned a = 0; a < s.resource_count; ++a)
                    for (unsigned b = 0; b < states_[j].resource_count; ++b)
                        if ((s.record[1] || states_[j].record[1]) && (s.resources[a].channel == 256 || states_[j].resources[b].channel == 256)
                            && s.resources[a].overlaps(states_[j].resources[b])) {
                            hardware_.error("Switches reserve overlapping MIDI controls"); open_ = false; unpin(); return;
                        }
            s.baseline_resources = s.resource_count;
            synchronize(s);
        }
    }
public:
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(switches_button)
#endif
    void button(unsigned button, bool down, uint32_t now = 0) {
        if (!open_ || button >= document_.inputs() || button >= 32) return;
        unsigned bit = 1u << button;
        if (bool(held_ & bit) == down) return;
        if (down) {
            for (unsigned i = 0; i < count_; ++i) {
                auto &state = states_[i];
                if (state.record[0] == button && (!state.record[1] || (!state.requested && !state.active))
                    && !outputs_.prepare_owner_cycle(state.executor)) { hardware_.error("Switch delivery memory unavailable"); return; }
            }
            // A tick can recognize three gesture actions. Reserve its release
            // bookkeeping before accepting the edge.
            if (hooks_enabled_ && !events_.reserve(event_count_ + 3)) { hardware_.error("Switch event memory unavailable"); return; }
            for (unsigned i = 0; i < count_; ++i) if (states_[i].record[0] == button
                && (hooks_enabled_ || states_[i].settings || states_[i].hooks || states_[i].shifted))
                    if (!states_[i].queue.reserve(states_[i].queued + 3)) { hardware_.error("Switch action memory unavailable"); return; }
        }
        held_ = down ? held_ | bit : held_ & ~bit;
        if (!down) disarmed_ &= ~bit;
        if (!bindings_ && button == hardware_.control_input(ControlRole::back) && down) { close(); return; }
        if (closing_) return;
        for (unsigned i = 0; i < count_; ++i) {
            auto &s = states_[i]; if (s.record[0] != button || (disarmed_ & bit)) continue;
            if (down && s.shifted) {
                bool layer = shifted_;
                if (s.input_layer != layer) {
                    s.input_layer = layer;
                    s.settings = layer ? (u16(s.shifted + 4 + gesture_debounce_offset) ? s.shifted + 4 : nullptr)
                        : gestures(button);
                    s.gesture.reset(s.settings);
                }
            }
            synchronize(s);
            if (s.settings) {
                if (!s.gesture.configured()) s.gesture.reset(s.settings);
                s.gesture.input(down, now); continue;
            }
            const uint8_t *actions = s.input_layer ? s.shifted : s.record + 2;
            if (s.record[1] == 0) { if (down) action(i, u16(actions), 0); }
            else if (s.record[1] == 2) { if (down) action(i, 0, 3); }
            else action(i, 0, down ? 1 : 2);
            dirty_ = true;
        }
    }
    void close() {
        if (!open_) return;
        if (closing_) {
            for (unsigned i = 0; i < count_; ++i) states_[i].executor.cancel();
            open_ = false; abandoned_ = true; unpin(); return;
        }
        if (transition_ == 1 && selected_ >= 0 && !states_[selected_].queued) selected_ = -1;
        else if (transition_ == 2 && event_count_)
            selected_ = states_[events_[0].index].queued ? -1 : int(events_[0].index);
        closing_ = dirty_ = true; shifted_ = false;
        clear_events(); transition_ = 0;
        for (unsigned i = 0; i < count_; ++i) {
            auto &s = states_[i];
            s.requested = false;
            if (!s.owned) s.active = false;
            cancel_gestures(s);
        }
    }
    void tick(uint32_t now = 0, std::optional<uint32_t> held = std::nullopt) {
        if (held) held_ = *held;
        if (!open_) { install_pending(); return; }
        if (counter_revision_ != hardware_.counters.revision()) {
            counter_revision_ = hardware_.counters.revision(); dirty_ = true;
        }
        if (const auto *state = outputs_.midi_state()) {
            if (state->revision() != state_revision_) { state_revision_ = state->revision(); dirty_ = true; }
        }
        if (!closing_ && event_count_) select();
        bool done = true;
        for (unsigned i = 0; i < count_; ++i) {
            auto &s = states_[i];
            synchronize(s);
            bool overflow = false;
            s.gesture.tick(now, !s.executor.busy() && !s.queued && !event_count_, [&](unsigned event) {
                const uint8_t *actions = s.input_layer ? s.shifted : s.record + 2;
                unsigned node = event ? u16(s.settings + (event - 1) * 2) : u16(actions);
                if (node == 65535) return;
                if (hooks_enabled_ ? event_count_ == events_.capacity() : s.queued == s.queue.capacity()) { overflow = true; return; }
                action(i, node, 0); dirty_ = true;
            });
            if (overflow) {
                s.gesture.reset(); clear_queue(s); s.failed = true; dirty_ = true;
                disarmed_ |= held_ & (1u << s.record[0]);
                hardware_.error("Switch gesture queue full");
            }
            bool unavailable = false;
            if (s.settings || s.active || s.requested || s.executor.busy() || s.queued)
                for (unsigned r = 0; r < s.resource_count; ++r)
                    if (!outputs_.available(s.resources[r].output)) unavailable = true;
            if (unavailable) {
                if (event_count_) { clear_events(); transition_ = 0; selected_ = -1; }
                s.executor.cancel(); s.unknown = true; s.failed = false;
                s.active = s.requested = s.owned = false; s.presses = 0;
                disarmed_ |= held_ & (1u << s.record[0]); dirty_ = true;
                s.gesture.reset(); clear_queue(s);
            }
            bool hook = s.hooks && (s.executor.root() == u16(s.hooks) || s.executor.root() == u16(s.hooks + 2));
            if (!hook && s.executor.delaying() && s.record[1] && s.active && !s.requested)
                s.executor.cancel();
            auto before = s.executor.status(); s.executor.tick(now);
            auto status = s.executor.status();
            if (before != status) dirty_ = true;
            if (status == PatchExecutor::Status::completed && !s.active && !s.settings) s.resource_count = s.baseline_resources;
            if (status == PatchExecutor::Status::failed || status == PatchExecutor::Status::unknown) {
                s.executor.cancel(); s.unknown = true; s.active = s.requested = s.owned = false; s.presses = 0;
                s.failed = status == PatchExecutor::Status::failed;
                disarmed_ |= held_ & (1u << s.record[0]); dirty_ = true;
                s.gesture.reset(); clear_queue(s);
            }
            if (!s.executor.busy()) {
                bool press = s.record[1] == 0;
                bool baseline = !press && s.unknown && s.requested;
                if (s.queued || (press ? s.presses != 0 : baseline || s.requested != s.active)) {
                    unsigned node = s.queued ? s.queue[0] : u16(s.actions + (press || (s.requested && !baseline) ? 0 : 2));
                    if (s.executor.start(document_, node, !press || s.settings || s.hooks || s.shifted)) {
                        bool captured = true;
                        if (node == u16(s.actions)) captured = capture(s.on[s.layer], s.executor);
                        if (node == u16(s.actions + 2)) captured = capture(s.off[s.layer], s.executor) && captured;
                        if (!captured) hardware_.error("Switch state memory exhausted");
                        if (!captured || !reserve(i)) {
                            s.executor.cancel(); s.failed = true;
                            s.requested = s.active; s.presses = 0;
                            s.gesture.reset(); clear_queue(s);
                        } else {
                            if (s.queued) {
                                --s.queued;
                                s.queue.erase(s.queue.begin());
                            } else if (press) --s.presses;
                            else s.owned = s.active = !baseline && s.requested;
                            s.unknown = s.failed = false;
                            s.executor.tick(now);
                        }
                    }
                    dirty_ = true;
                }
            }
            if (s.executor.busy() || s.active || s.requested || s.presses || s.queued) done = false;
        }
        if (closing_ && done) {
            if (!exit_started_ && selected_ >= 0) {
                exit_started_ = true;
                auto &s = states_[selected_];
                if (s.hooks && u16(s.hooks + 2) != 65535) {
                    queue_node(s, u16(s.hooks + 2)); dirty_ = true; return;
                }
            }
            for (unsigned i = 0; i < count_; ++i) if (states_[i].unknown) abandoned_ = true;
            open_ = false; unpin();
            if (bindings_) { bindings_ = {}; ++revision_; }
            install_pending(); return;
        }
        if (dirty_) render();
        install_pending();
    }
private:
    struct Pattern {
        CheckedArray<PatchExecutor::Resource> values;
        unsigned count = 0;
        bool complete = false;
    };
    struct State {
        State(Hardware &hardware, PatchOutputs &outputs, PatchSources &sources, unsigned owner)
            : executor(hardware, outputs, sources, owner) { executor.retain_counter_reservations(); }
        PatchExecutor executor;
        CheckedArray<PatchExecutor::Resource> resources;
        unsigned resource_count = 0, baseline_resources = 0;
        const uint8_t *record = nullptr;
        const uint8_t *actions = nullptr, *shifted = nullptr;
        bool layer = false, input_layer = false;
        unsigned presses = 0;
        Pattern on[2], off[2];
        const uint8_t *hooks = nullptr;
        SwitchGestures gesture;
        const uint8_t *settings = nullptr;
        CheckedList<uint16_t> queue;
        unsigned queued = 0;
        bool requested = false, active = false, unknown = false, failed = false, owned = false;
    };
    Hardware &hardware_; PatchOutputs &outputs_; PatchSources &sources_; Document document_;
    MemoryBuffer state_storage_;
    State *states_ = nullptr;
    void destroy_states() {
        for (unsigned i = count_; i--;) states_[i].~State();
        count_ = 0; states_ = nullptr; state_storage_.clear();
    }
    ControlBindings bindings_;
    MemoryBuffer bound_, pending_;
    std::unique_ptr<Switches> prepared_controls_;
    Document pending_document_;
    PatchExecutor *pending_invocation_ = nullptr;
    const uint8_t *pending_source_ = nullptr;
    unsigned pending_node_ = 0, revision_ = 0, counter_revision_ = 0;
    struct Event { unsigned index, node, kind; bool layer; };
    CheckedList<Event> events_;
    unsigned event_count_ = 0, transition_ = 0, state_revision_ = 0;
    int selected_ = -1;
    bool hooks_enabled_ = false, exit_started_ = false;
    unsigned count_ = 0, held_ = 0, disarmed_ = 0;
    bool open_ = false, closing_ = false, dirty_ = false, abandoned_ = false, pinned_ = false;
    bool shifted_ = false;
    void clear_events() { events_.clear(); event_count_ = 0; }
    static void clear_queue(State &state) { state.queue.clear(); state.queued = 0; }
    bool queue_node(State &state, unsigned node) {
        if (state.queue.size() == state.queue.capacity() || !state.queue.append(uint16_t(node))) {
            state.failed = true; hardware_.error("Switch action queue full"); return false;
        }
        ++state.queued; return true;
    }
    void cancel_gestures(State &s) {
        clear_queue(s); s.presses = 0;
        s.gesture.cancel([&](unsigned event) {
            unsigned node = u16(s.settings + (event - 1) * 2);
            if (node != 65535) queue_node(s, node);
        });
    }
    const uint8_t *gestures(unsigned input) const {
        if (!bindings_) return document_.switch_gestures(input);
        const auto *p = bindings_.find(input);
        return p && u16(p + 72 + gesture_debounce_offset) ? p + 72 : nullptr;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(switches_install_pending)
#endif
    void install_pending() {
        if (!pending_invocation_) { if (!open_) bound_.clear(); return; }
        if (pending_invocation_->busy()) return;
        bool completed = pending_invocation_->status() == PatchExecutor::Status::completed;
        pending_invocation_ = nullptr;
        if (completed) {
            // The invoking executor has returned, so replacing its state is safe.
            if (prepared_controls_) adopt(*prepared_controls_);
            bound_.swap(pending_);
            if (!open_) { bindings_ = {}; ++revision_; }
        }
        prepared_controls_.reset();
        pending_document_ = Document(); pending_.clear(); sources_.end();
        if (!open_) bound_.clear();
    }
    void unpin() { if (pinned_) { pinned_ = false; sources_.end(); } }
    bool capture(Pattern &pattern, const PatchExecutor &executor) {
        unsigned count = executor.resource_count();
        if (!pattern.values.resize(count)) return false;
        if (count) std::memcpy(pattern.values.data(), executor.resources(), count * sizeof(PatchExecutor::Resource));
        pattern.count = count; pattern.complete = executor.state_complete(); return true;
    }
    void perform(const Event &event) {
        auto &s = states_[event.index]; synchronize(s);
        if (event.kind == 0) {
            if (!queue_node(s, event.node)) return;
        } else if (event.kind == 3) s.requested = !s.requested;
        else s.requested = event.kind == 1;
        dirty_ = true;
    }
    void action(unsigned index, unsigned node, unsigned kind) {
        Event event{index, node, kind, states_[index].input_layer};
        if (!hooks_enabled_) {
            auto &s = states_[index];
            if (kind == 0 && !s.settings && node == u16(s.record + 2)) { ++s.presses; dirty_ = true; }
            else perform(event);
            return;
        }
        if (event_count_ == events_.capacity()) {
            hardware_.error("Switch selection queue full"); states_[index].failed = true; return;
        }
        if (!events_.append(event)) { hardware_.error("Switch event memory unavailable"); return; }
        ++event_count_; dirty_ = true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(switches_select)
#endif
    void select() {
        for (unsigned i = 0; i < count_; ++i)
            if (states_[i].executor.busy() || states_[i].queued || states_[i].presses
                || states_[i].requested != states_[i].active) return;
        const Event event = events_[0];
        auto &next = states_[event.index];
        if (next.shifted && next.layer != event.layer) {
            if (next.active && next.owned) { next.requested = false; dirty_ = true; return; }
            next.active = next.requested = next.owned = false;
            next.layer = event.layer;
            next.actions = next.layer ? next.shifted : next.record + 2;
            synchronize(next); dirty_ = true;
        }
        bool changed = event.kind != 2 && selected_ != int(event.index);
        if (changed && transition_ == 0) {
            transition_ = 1;
            if (selected_ >= 0) {
                auto &previous = states_[selected_];
                if (previous.hooks && u16(previous.hooks + 2) != 65535) {
                    previous.failed = previous.unknown = false;
                    queue_node(previous, u16(previous.hooks + 2)); return;
                }
            }
        }
        if (changed && transition_ == 1) {
            if (selected_ >= 0 && states_[selected_].hooks && u16(states_[selected_].hooks + 2) != 65535
                && (states_[selected_].failed || states_[selected_].unknown)) {
                clear_events(); transition_ = 0; selected_ = -1; dirty_ = true; return;
            }
            transition_ = 2;
            if (next.hooks && u16(next.hooks) != 65535) {
                next.failed = next.unknown = false;
                queue_node(next, u16(next.hooks)); return;
            }
        }
        if (changed && next.hooks && u16(next.hooks) != 65535 && (next.failed || next.unknown)) {
            clear_events(); transition_ = 0; selected_ = -1; dirty_ = true; return;
        }
        if (event.kind != 2) selected_ = int(event.index);
        transition_ = 0;
        bool override = changed && next.hooks && next.hooks[4];
        if (!override) perform(event);
        --event_count_;
        events_.erase(events_.begin());
        dirty_ = true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(switches_match)
#endif
    int match(const Pattern &pattern) const {
        const auto *state = outputs_.midi_state();
        if (!state || !pattern.complete) return -2;
        unsigned known = 0, matched = 0, total = 0;
        for (unsigned i = 0; i < pattern.count; ++i) {
            const auto &v = pattern.values[i]; if (v.value < 0) continue;
            const auto *connection = hardware_.midi_outputs().find(v.output);
            if (!connection) return -2;
            ++total;
            int value = outputs_.available(v.output) ? state->value(connection->route, v.channel, v.control) : -1;
            if (value >= 0) ++known;
            if (value == v.value) ++matched;
        }
        if (!total) return -2;
        if (matched == total) return 1;
        if (matched) return 2;
        return known == total ? 0 : -1;
    }
    void synchronize(State &s) {
        if (closing_ || !s.record[1] || s.executor.busy() || s.requested != s.active || s.unknown || s.queued) return;
        int observed = match(s.on[s.layer]);
        if (observed == -2) return;
        bool active = observed == 1;
        if (s.active != active) {
            s.active = s.requested = active;
            if (!active) s.owned = false;
            dirty_ = true;
        }
    }
    bool reserve(unsigned index) {
        State &state = states_[index];
        for (unsigned i = 0; i < state.executor.resource_count(); ++i) {
            const auto &resource = state.executor.resources()[i];
            for (unsigned j = 0; j < count_; ++j) {
                if (j == index || (!state.record[1] && !states_[j].record[1])) continue;
                for (unsigned r = 0; r < states_[j].resource_count; ++r)
                    if ((resource.channel == 256 || states_[j].resources[r].channel == 256) && resource.overlaps(states_[j].resources[r])) {
                        hardware_.error("Selected patch overlaps another switch's MIDI controls"); return false;
                    }
            }
        }
        for (unsigned i = 0; i < state.executor.resource_count(); ++i) {
            const auto &resource = state.executor.resources()[i]; bool known = false;
            for (unsigned j = 0; j < state.resource_count; ++j)
                if (resource.channel == state.resources[j].channel && resource.control == state.resources[j].control
                    && !std::strcmp(resource.output, state.resources[j].output)) known = true;
            if (known) continue;
            if (!state.resources.resize(state.resource_count + 1)) { hardware_.error("Switch ownership memory exhausted"); return false; }
            state.resources[state.resource_count++] = resource;
        }
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(switches_render)
#endif
    void render() {
        if (bindings_) {
            Pane panes[3];
            for (auto &pane : panes) pane.graphics = document_.graphics(1);
            std::snprintf(panes[1].lines[0].text, 64, "%s", bindings_.title());
            for (unsigned i = 0; i < count_; ++i) {
                const auto &s = states_[i];
                auto &pane = panes[i % 3];
                const unsigned row = 2 + i / 3;
                const char *state = s.unknown ? "Unknown" : s.executor.busy() || s.queued ? "Pending" : s.record[1] ? (s.active ? "On" : "Off") : "";
                std::snprintf(pane.lines[row].text, 64, "%s: %.40s%s%s", document_.input_label(s.record[0]), reinterpret_cast<const char *>(s.record + 6), state[0] ? " | " : "", state);
            }
            ui::present(hardware_, panes, 1); dirty_ = false; return;
        }
        ui::Prompt prompt(hardware_);
        using Role = ControlRole;
        Pane panes[3];
        for (unsigned i = 0; i < count_; ++i) {
            const auto &s = states_[i]; Pane view; view.graphics = document_.graphics(1);
            bool pending = false;
            for (unsigned e = 0; e < event_count_; ++e) if (events_[e].index == i) pending = true;
            int on = match(s.on[s.layer]), off = match(s.off[s.layer]);
            const char *state = s.failed ? "Failed / unknown" : s.unknown ? "Unknown remote state" : pending || s.executor.busy() || s.presses || s.queued || s.requested != s.active
                ? "Pending" : on == 1 ? "On" : off == 1 ? "Off" : on == 2 || off == 2 ? "Partial match"
                : on == -1 || off == -1 ? "Unknown remote state" : on == 0 ? "Other values"
                : s.record[1] == 0 ? "Completed locally" : s.active ? "On" : "Off";
            document_.page_record(view, u16(s.record + 70), 0, false, state, &hardware_.counters);
            unsigned pane = i % 3, group = i / 3;
            panes[pane].graphics = view.graphics;
            for (unsigned j = 0; j < 3; ++j) {
                panes[pane].lines[group * 3 + j] = view.lines[j];
                panes[pane].lines[group * 3 + j].y = int16_t(4 + group * 58 + j * 18);
            }
        }
        auto &back = panes[2].lines[15]; back.graphics.size = 8; back.graphics.line_height = 8;
        back.y = int16_t(hardware_.display_height() - 12);
        std::snprintf(back.text, 64, "%s%.52s", closing_ ? "Closing: " : shifted_ ? "Shifted | " : "", prompt.add(Role::back, closing_ ? "abandon" : "Back").text());
        if (!count_) std::snprintf(panes[1].lines[0].text, 64, "No switches");
        ui::present(hardware_, panes, 1);
        dirty_ = false;
    }
};
} // namespace cntrl
