#pragma once
#include "patch_executor.h"
#include "switch_gestures.h"

namespace cntrl {
class Switches {
public:
    Switches(Hardware &hardware, PatchOutputs &outputs, PatchSources &sources)
        : hardware_(hardware), outputs_(outputs), sources_(sources), states_{{hardware, outputs, sources, 0},
          {hardware, outputs, sources, 1}, {hardware, outputs, sources, 2},
          {hardware, outputs, sources, 3}, {hardware, outputs, sources, 4}} {}
    ~Switches() { unpin(); }
    bool open() const { return open_; }
    std::optional<uint16_t> debounce_ms(unsigned button) const {
        if (!open_ || closing_) return std::nullopt;
        for (unsigned i = 0; i < count_; ++i)
            if (states_[i].record[0] == button && states_[i].settings) return u16(states_[i].settings + 12);
        return std::nullopt;
    }
    bool closing() const { return closing_; }
    bool abandoned() const { return abandoned_; }
    bool owns(const PatchExecutor &executor) const {
        for (const auto &s : states_) if (&s.executor == &executor) return true;
        return false;
    }
    midi::Bridge::Delivery prepare_leave(PatchExecutor &invocation) {
        if (!open_) return midi::Bridge::Delivery::complete;
        closing_ = dirty_ = true;
        event_count_ = transition_ = 0;
        bool done = true;
        for (unsigned i = 0; i < count_; ++i) {
            auto &s = states_[i];
            if (&s.executor == &invocation) continue;
            s.requested = false;
            if (!s.owned) s.active = false;
            s.gesture.reset(); s.queued = 0;
            if (s.unknown) return midi::Bridge::Delivery::cancelled;
            if (s.executor.busy() || s.active || s.presses) done = false;
        }
        return done ? midi::Bridge::Delivery::complete : midi::Bridge::Delivery::pending;
    }
    // Targets with flash placement keep page setup out of instruction RAM.
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(switches_enter)
#endif
    void enter(const Document &document, uint8_t held) {
        for (auto &s : states_) s.executor.cancel();
        unpin(); sources_.begin(); pinned_ = true;
        document_ = document; count_ = document.switches(); held_ = disarmed_ = held;
        open_ = dirty_ = true; closing_ = abandoned_ = false;
        selected_ = -1; event_count_ = transition_ = 0; hooks_enabled_ = false; exit_started_ = false;
        for (unsigned i = 0; i < count_; ++i) {
            State &s = states_[i]; s.executor.cancel();
            s.record = document_.switch_assignment(i);
            s.settings = document_.switch_gestures(s.record[0]);
            s.hooks = document_.switch_hooks(s.record[0]);
            if (s.hooks && (u16(s.hooks) != 65535 || u16(s.hooks + 2) != 65535)) hooks_enabled_ = true;
            s.on.count = s.off.count = 0; s.on.complete = s.off.complete = false;
            s.gesture.reset(s.settings); s.queued = 0;
            s.requested = s.active = s.unknown = s.failed = s.owned = false; s.presses = 0;
            s.resource_count = 0;
            for (unsigned branch = 0; branch < 10; ++branch) {
                unsigned node = branch < 2 ? u16(s.record + 2 + branch * 2)
                    : branch < 8 ? (s.settings ? u16(s.settings + (branch - 2) * 2) : 65535)
                    : s.hooks ? u16(s.hooks + (branch - 8) * 2) : 65535;
                if (node == 65535) continue;
                if (!s.executor.start(document_, node, s.record[1] != 0 || s.settings || s.hooks)) {
                    s.executor.cancel(); open_ = false; unpin(); return;
                }
                unsigned count = s.executor.resource_count();
                if (branch < 2) {
                    Pattern &pattern = branch ? s.off : s.on;
                    if (!capture(pattern, s.executor)) {
                        s.executor.cancel(); hardware_.error("Switch state memory exhausted"); open_ = false; unpin(); return;
                    }
                }
                void *memory = std::realloc(s.resources, (s.resource_count + count) * sizeof(PatchExecutor::Resource));
                if (count && !memory) {
                    s.executor.cancel(); hardware_.error("Switch ownership memory exhausted"); open_ = false; unpin(); return;
                }
                s.resources = static_cast<PatchExecutor::Resource *>(memory);
                if (count) std::memcpy(s.resources + s.resource_count, s.executor.resources(), count * sizeof(PatchExecutor::Resource));
                s.resource_count += count; s.executor.cancel();
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
        render();
    }
    void button(unsigned button, bool down, uint32_t now = 0) {
        if (!open_ || button >= 6) return;
        unsigned bit = 1u << button;
        if (bool(held_ & bit) == down) return;
        held_ = down ? held_ | bit : held_ & ~bit;
        if (!down) disarmed_ &= ~bit;
        if (button == 5 && down) { close(); return; }
        if (closing_) return;
        for (unsigned i = 0; i < count_; ++i) {
            auto &s = states_[i]; if (s.record[0] != button || (disarmed_ & bit)) continue;
            synchronize(s);
            if (s.settings) {
                if (!s.gesture.configured()) s.gesture.reset(s.settings);
                s.gesture.input(down, now); continue;
            }
            if (s.record[1] == 0) { if (down) action(i, u16(s.record + 2), 0); }
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
        closing_ = dirty_ = true;
        event_count_ = transition_ = 0;
        for (auto &s : states_) {
            s.requested = false;
            if (!s.owned) s.active = false;
            s.gesture.reset(); s.queued = 0;
        }
    }
    void tick(uint32_t now = 0) {
        if (!open_) return;
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
                unsigned node = event ? u16(s.settings + (event - 1) * 2) : u16(s.record + 2);
                if (node == 65535) return;
                if (s.queued == 16 || event_count_ == 16) { overflow = true; return; }
                action(i, node, 0); dirty_ = true;
            });
            if (overflow) {
                s.gesture.reset(); s.queued = 0; s.failed = true; dirty_ = true;
                disarmed_ |= held_ & (1u << s.record[0]);
                hardware_.error("Switch gesture queue full");
            }
            bool unavailable = false;
            if (s.settings || s.active || s.requested || s.executor.busy() || s.queued)
                for (unsigned r = 0; r < s.resource_count; ++r)
                    if (!outputs_.available(s.resources[r].output)) unavailable = true;
            if (unavailable) {
                if (event_count_) { event_count_ = transition_ = 0; selected_ = -1; }
                s.executor.cancel(); s.unknown = true; s.failed = false;
                s.active = s.requested = s.owned = false; s.presses = 0;
                disarmed_ |= held_ & (1u << s.record[0]); dirty_ = true;
                s.gesture.reset(); s.queued = 0;
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
                s.gesture.reset(); s.queued = 0;
            }
            if (!s.executor.busy()) {
                bool press = s.record[1] == 0;
                bool baseline = !press && s.unknown && s.requested;
                if (s.queued || (press ? s.presses != 0 : baseline || s.requested != s.active)) {
                    unsigned node = s.queued ? s.queue[0] : u16(s.record + (press || (s.requested && !baseline) ? 2 : 4));
                    if (s.executor.start(document_, node, !press || s.settings || s.hooks)) {
                        bool captured = true;
                        if (node == u16(s.record + 2)) captured = capture(s.on, s.executor);
                        if (node == u16(s.record + 4)) captured = capture(s.off, s.executor) && captured;
                        if (!captured) hardware_.error("Switch state memory exhausted");
                        if (!captured || !reserve(i)) {
                            s.executor.cancel(); s.failed = s.unknown = true;
                            s.active = s.requested = s.owned = false; s.presses = 0;
                            s.gesture.reset(); s.queued = 0;
                        } else {
                            if (s.queued) {
                                --s.queued;
                                for (unsigned q = 0; q < s.queued; ++q) s.queue[q] = s.queue[q + 1];
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
                    s.queue[s.queued++] = u16(s.hooks + 2); dirty_ = true; return;
                }
            }
            for (unsigned i = 0; i < count_; ++i) if (states_[i].unknown) abandoned_ = true;
            open_ = false; unpin(); return;
        }
        if (dirty_) render();
    }
private:
    struct Pattern {
        PatchExecutor::Resource *values = nullptr;
        unsigned count = 0;
        bool complete = false;
        ~Pattern() { std::free(values); }
    };
    struct State {
        State(Hardware &hardware, PatchOutputs &outputs, PatchSources &sources, unsigned owner)
            : executor(hardware, outputs, sources, owner) {}
        ~State() { std::free(resources); }
        PatchExecutor executor;
        PatchExecutor::Resource *resources = nullptr;
        unsigned resource_count = 0, baseline_resources = 0;
        const uint8_t *record = nullptr;
        unsigned presses = 0;
        Pattern on, off;
        const uint8_t *hooks = nullptr;
        SwitchGestures gesture;
        const uint8_t *settings = nullptr;
        uint16_t queue[16] = {};
        unsigned queued = 0;
        bool requested = false, active = false, unknown = false, failed = false, owned = false;
    };
    Hardware &hardware_; PatchOutputs &outputs_; PatchSources &sources_; Document document_;
    State states_[5];
    struct Event { unsigned index, node, kind; };
    Event events_[16];
    unsigned event_count_ = 0, transition_ = 0, state_revision_ = 0;
    int selected_ = -1;
    bool hooks_enabled_ = false, exit_started_ = false;
    unsigned count_ = 0, held_ = 0, disarmed_ = 0;
    bool open_ = false, closing_ = false, dirty_ = false, abandoned_ = false, pinned_ = false;
    void unpin() { if (pinned_) { pinned_ = false; sources_.end(); } }
    bool capture(Pattern &pattern, const PatchExecutor &executor) {
        unsigned count = executor.resource_count();
        void *memory = std::realloc(pattern.values, count * sizeof(PatchExecutor::Resource));
        if (count && !memory) return false;
        pattern.values = static_cast<PatchExecutor::Resource *>(memory);
        if (count) std::memcpy(pattern.values, executor.resources(), count * sizeof(PatchExecutor::Resource));
        pattern.count = count; pattern.complete = executor.state_complete(); return true;
    }
    void perform(const Event &event) {
        auto &s = states_[event.index]; synchronize(s);
        if (event.kind == 0) {
            if (s.queued == 16) { s.failed = true; hardware_.error("Switch action queue full"); return; }
            s.queue[s.queued++] = uint16_t(event.node);
        } else if (event.kind == 3) s.requested = !s.requested;
        else s.requested = event.kind == 1;
        dirty_ = true;
    }
    void action(unsigned index, unsigned node, unsigned kind) {
        Event event{index, node, kind};
        if (!hooks_enabled_) {
            auto &s = states_[index];
            if (kind == 0 && !s.settings && node == u16(s.record + 2)) { ++s.presses; dirty_ = true; }
            else perform(event);
            return;
        }
        if (event_count_ == 16) {
            hardware_.error("Switch selection queue full"); states_[index].failed = true; return;
        }
        events_[event_count_++] = event; dirty_ = true;
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
        bool changed = event.kind != 2 && selected_ != int(event.index);
        if (changed && transition_ == 0) {
            transition_ = 1;
            if (selected_ >= 0) {
                auto &previous = states_[selected_];
                if (previous.hooks && u16(previous.hooks + 2) != 65535) {
                    previous.failed = previous.unknown = false;
                    previous.queue[previous.queued++] = u16(previous.hooks + 2); return;
                }
            }
        }
        if (changed && transition_ == 1) {
            if (selected_ >= 0 && states_[selected_].hooks && u16(states_[selected_].hooks + 2) != 65535
                && (states_[selected_].failed || states_[selected_].unknown)) {
                event_count_ = transition_ = 0; selected_ = -1; dirty_ = true; return;
            }
            transition_ = 2;
            if (next.hooks && u16(next.hooks) != 65535) {
                next.failed = next.unknown = false;
                next.queue[next.queued++] = u16(next.hooks); return;
            }
        }
        if (changed && next.hooks && u16(next.hooks) != 65535 && (next.failed || next.unknown)) {
            event_count_ = transition_ = 0; selected_ = -1; dirty_ = true; return;
        }
        if (event.kind != 2) selected_ = int(event.index);
        transition_ = 0;
        bool override = changed && next.hooks && next.hooks[4];
        if (!override) perform(event);
        --event_count_;
        for (unsigned i = 0; i < event_count_; ++i) events_[i] = events_[i + 1];
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
            const auto *connection = midi::find_output(v.output);
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
        int observed = match(s.on);
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
            void *memory = std::realloc(state.resources, (state.resource_count + 1) * sizeof(PatchExecutor::Resource));
            if (!memory) { hardware_.error("Switch ownership memory exhausted"); return false; }
            state.resources = static_cast<PatchExecutor::Resource *>(memory);
            state.resources[state.resource_count++] = resource;
        }
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(switches_render)
#endif
    void render() {
        Pane panes[3];
        for (unsigned i = 0; i < count_; ++i) {
            const auto &s = states_[i]; Pane view; view.graphics = document_.graphics(1);
            bool pending = false;
            for (unsigned e = 0; e < event_count_; ++e) if (events_[e].index == i) pending = true;
            int on = match(s.on), off = match(s.off);
            const char *state = s.failed ? "Failed / unknown" : s.unknown ? "Unknown remote state" : pending || s.executor.busy() || s.presses || s.queued || s.requested != s.active
                ? "Pending" : on == 1 ? "On" : off == 1 ? "Off" : on == 2 || off == 2 ? "Partial match"
                : on == -1 || off == -1 ? "Unknown remote state" : on == 0 ? "Other values"
                : s.record[1] == 0 ? "Completed locally" : s.active ? "On" : "Off";
            document_.page_record(view, u16(s.record + 70), 0, false, state);
            unsigned pane = s.record[0] % 3, group = s.record[0] / 3;
            panes[pane].graphics = view.graphics;
            for (unsigned j = 0; j < 3; ++j) {
                panes[pane].lines[group * 3 + j] = view.lines[j];
                panes[pane].lines[group * 3 + j].y = int16_t(4 + group * 58 + j * 18);
            }
        }
        auto &back = panes[2].lines[15]; back.graphics.size = back.graphics.line_height = 8;
        back.y = int16_t(hardware_.display_height() - 12);
        std::snprintf(back.text, 64, "%s", closing_ ? "Closing: F abandon" : "F Back");
        if (!count_) std::snprintf(panes[1].lines[0].text, 64, "No switches");
        for (unsigned i = 0; i < 3; ++i) hardware_.show(i, panes[i]);
        dirty_ = false;
    }
};
} // namespace cntrl
