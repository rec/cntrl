#pragma once
#include "patch_executor.h"

namespace cntrl {
class Switches {
public:
    Switches(Hardware &hardware, PatchOutputs &outputs, PatchSources &sources)
        : hardware_(hardware), outputs_(outputs), sources_(sources), states_{{hardware, outputs, sources, 0},
          {hardware, outputs, sources, 1}, {hardware, outputs, sources, 2},
          {hardware, outputs, sources, 3}, {hardware, outputs, sources, 4}} {}
    ~Switches() { unpin(); }
    bool open() const { return open_; }
    bool closing() const { return closing_; }
    bool abandoned() const { return abandoned_; }
    bool owns(const PatchExecutor &executor) const {
        for (const auto &s : states_) if (&s.executor == &executor) return true;
        return false;
    }
    midi::Bridge::Delivery prepare_leave(PatchExecutor &invocation) {
        if (!open_) return midi::Bridge::Delivery::complete;
        closing_ = dirty_ = true;
        bool done = true;
        for (unsigned i = 0; i < count_; ++i) {
            auto &s = states_[i];
            if (&s.executor == &invocation) continue;
            s.requested = false;
            if (s.unknown) return midi::Bridge::Delivery::cancelled;
            if (s.executor.busy() || s.active || s.presses) done = false;
        }
        return done ? midi::Bridge::Delivery::complete : midi::Bridge::Delivery::pending;
    }
    void enter(const Document &document, uint8_t held) {
        for (auto &s : states_) s.executor.cancel();
        unpin(); sources_.begin(); pinned_ = true;
        document_ = document; count_ = document.switches(); held_ = disarmed_ = held;
        open_ = dirty_ = true; closing_ = abandoned_ = false;
        for (unsigned i = 0; i < count_; ++i) {
            State &s = states_[i]; s.executor.cancel();
            s.record = document_.switch_assignment(i);
            s.requested = s.active = s.unknown = s.failed = false; s.presses = 0;
            s.resource_count = 0;
            for (unsigned branch = 0; branch < (s.record[1] ? 2u : 1u); ++branch) {
                if (!s.executor.start(document_, u16(s.record + 2 + branch * 2), s.record[1] != 0)) {
                    s.executor.cancel(); open_ = false; unpin(); return;
                }
                unsigned count = s.executor.resource_count();
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
                        if ((s.record[1] || states_[j].record[1]) && s.resources[a].overlaps(states_[j].resources[b])) {
                            hardware_.error("Switches reserve overlapping MIDI controls"); open_ = false; unpin(); return;
                        }
            s.baseline_resources = s.resource_count;
        }
        render();
    }
    void button(unsigned button, bool down) {
        if (!open_ || button >= 6) return;
        unsigned bit = 1u << button;
        if (bool(held_ & bit) == down) return;
        held_ = down ? held_ | bit : held_ & ~bit;
        if (!down) disarmed_ &= ~bit;
        if (button == 5 && down) { close(); return; }
        if (closing_) return;
        for (unsigned i = 0; i < count_; ++i) {
            auto &s = states_[i]; if (s.record[0] != button || (disarmed_ & bit)) continue;
            if (s.record[1] == 0) { if (down) ++s.presses; }
            else if (s.record[1] == 2) { if (down) s.requested = !s.requested; }
            else s.requested = down;
            dirty_ = true;
        }
    }
    void close() {
        if (!open_) return;
        if (closing_) {
            for (unsigned i = 0; i < count_; ++i) states_[i].executor.cancel();
            open_ = false; abandoned_ = true; unpin(); return;
        }
        closing_ = dirty_ = true;
        for (auto &s : states_) s.requested = false;
    }
    void tick() {
        if (!open_) return;
        bool done = true;
        for (unsigned i = 0; i < count_; ++i) {
            auto &s = states_[i];
            bool unavailable = false;
            if (s.active || s.requested || s.executor.busy())
                for (unsigned r = 0; r < s.resource_count; ++r)
                    if (!outputs_.available(s.resources[r].output)) unavailable = true;
            if (unavailable) {
                s.executor.cancel(); s.unknown = true; s.failed = false;
                s.active = s.requested = false; s.presses = 0;
                disarmed_ |= held_ & (1u << s.record[0]); dirty_ = true;
            }
            auto before = s.executor.status(); s.executor.tick();
            auto status = s.executor.status();
            if (before != status) dirty_ = true;
            if (status == PatchExecutor::Status::completed && !s.active) s.resource_count = s.baseline_resources;
            if (status == PatchExecutor::Status::failed || status == PatchExecutor::Status::unknown) {
                s.executor.cancel(); s.unknown = true; s.active = s.requested = false; s.presses = 0;
                s.failed = status == PatchExecutor::Status::failed;
                disarmed_ |= held_ & (1u << s.record[0]); dirty_ = true;
            }
            if (!s.executor.busy()) {
                bool press = s.record[1] == 0;
                bool baseline = !press && s.unknown && s.requested;
                if (press ? s.presses != 0 : baseline || s.requested != s.active) {
                    unsigned node = u16(s.record + (press || (s.requested && !baseline) ? 2 : 4));
                    if (s.executor.start(document_, node, !press)) {
                        if (!reserve(i)) {
                            s.executor.cancel(); s.failed = s.unknown = true;
                            s.active = s.requested = false; s.presses = 0;
                        } else {
                            if (press) --s.presses; else s.active = !baseline && s.requested;
                            s.unknown = s.failed = false;
                        }
                    }
                    dirty_ = true;
                }
            }
            if (s.executor.busy() || s.active || s.requested || s.presses) done = false;
        }
        if (closing_ && done) {
            for (unsigned i = 0; i < count_; ++i) if (states_[i].unknown) abandoned_ = true;
            open_ = false; unpin(); return;
        }
        if (dirty_) render();
    }
private:
    struct State {
        State(Hardware &hardware, PatchOutputs &outputs, PatchSources &sources, unsigned owner)
            : executor(hardware, outputs, sources, owner) {}
        ~State() { std::free(resources); }
        PatchExecutor executor;
        PatchExecutor::Resource *resources = nullptr;
        unsigned resource_count = 0, baseline_resources = 0;
        const uint8_t *record = nullptr;
        unsigned presses = 0;
        bool requested = false, active = false, unknown = false, failed = false;
    };
    Hardware &hardware_; PatchOutputs &outputs_; PatchSources &sources_; Document document_;
    State states_[5];
    unsigned count_ = 0, held_ = 0, disarmed_ = 0;
    bool open_ = false, closing_ = false, dirty_ = false, abandoned_ = false, pinned_ = false;
    void unpin() { if (pinned_) { pinned_ = false; sources_.end(); } }
    bool reserve(unsigned index) {
        State &state = states_[index];
        for (unsigned i = 0; i < state.executor.resource_count(); ++i) {
            const auto &resource = state.executor.resources()[i];
            for (unsigned j = 0; j < count_; ++j) {
                if (j == index || (!state.record[1] && !states_[j].record[1])) continue;
                for (unsigned r = 0; r < states_[j].resource_count; ++r)
                    if (resource.overlaps(states_[j].resources[r])) {
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
    void render() {
        Pane panes[3];
        for (unsigned i = 0; i < count_; ++i) {
            const auto &s = states_[i]; Pane view; view.graphics = document_.graphics(1);
            const char *state = s.failed ? "Failed / unknown" : s.unknown ? "Unknown remote state" : s.executor.busy() || s.presses || s.requested != s.active
                ? "Pending" : s.record[1] == 0 ? "Completed locally" : s.active ? "On" : "Off";
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
