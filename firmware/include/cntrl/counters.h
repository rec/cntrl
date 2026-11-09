#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace cntrl {
// Values belong to a setup's compiled identity, shared by every invocation.
class Counters {
public:
    struct State { uint32_t setup; unsigned index; uint8_t value; bool sent, reversed; };
    struct Step { uint8_t value; bool reversed; };
    unsigned revision() const { return revision_; }
    void reset() { states_.clear(); ++revision_; }
    uint8_t value(uint32_t setup, unsigned index, uint8_t initial) const {
        for (const auto &s : states_) if (s.setup == setup && s.index == index) return s.value;
        return initial;
    }
    State &state(uint32_t setup, unsigned index, uint8_t initial) {
        for (auto &s : states_) if (s.setup == setup && s.index == index) return s;
        states_.push_back({setup, index, initial, false, false}); return states_.back();
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
    std::vector<State> states_;
    unsigned revision_ = 0;
};
} // namespace cntrl
