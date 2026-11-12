#pragma once
#include <algorithm>
#include <cstdint>
#include "checked_list.h"

namespace cntrl {
// Values belong to a setup's compiled identity, shared by every invocation.
class Counters {
public:
    struct State { uint32_t setup; unsigned index; uint8_t value; bool sent, reversed; unsigned users = 0; };
    struct Step { uint8_t value; bool reversed; };
    unsigned revision() const { return revision_; }
    uint64_t generation() const { return generation_; }
    void reset() { states_.release(); ++generation_; ++revision_; }
    uint8_t value(uint32_t setup, unsigned index, uint8_t initial) const {
        for (const auto &s : states_) if (s.setup == setup && s.index == index) return s.value;
        return initial;
    }
    State *state(uint32_t setup, unsigned index) {
        for (auto &s : states_) if (s.setup == setup && s.index == index) return &s;
        return nullptr;
    }
    State *acquire(uint32_t setup, unsigned index, uint8_t initial) {
        auto *value = state(setup, index);
        if (!value) value = states_.append({setup, index, initial, false, false});
        if (value) ++value->users;
        return value;
    }
    void release(State *value) {
        for (auto i = states_.begin(); i != states_.end(); ++i) if (&*i == value) {
            if (i->users) --i->users;
            if (!i->users && !i->sent) { states_.erase(i); states_.trim(1); }
            return;
        }
    }
    static Step next(const State &state, const uint8_t *definition, bool backward) {
        if (!state.sent) return {state.value, state.reversed};
        int step = int16_t(unsigned(definition[101]) | (unsigned(definition[102]) << 8));
        if (backward != state.reversed) step = -step;
        int minimum = definition[99], maximum = definition[100];
        if (minimum == maximum) return {uint8_t(minimum), state.reversed};
        bool reversed = state.reversed;
        if (definition[103]) {
            if ((step > 0 && state.value == maximum) || (step < 0 && state.value == minimum)) {
                step = -step; reversed = !reversed;
            }
            int value = state.value + step;
            if (value <= minimum || value >= maximum) reversed = !reversed;
            return {uint8_t(std::clamp(value, minimum, maximum)), reversed};
        }
        int value = state.value + step;
        return {uint8_t(value > maximum ? minimum : value < minimum ? maximum : value), reversed};
    }
    void accepted(State &state, Step step) { state.value = step.value; state.reversed = step.reversed; state.sent = true; ++revision_; }
private:
    CheckedList<State> states_;
    unsigned revision_ = 0;
    uint64_t generation_ = 0;
};
} // namespace cntrl
