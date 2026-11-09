#pragma once
#include "document.h"
#include "buttons.h"
#include <array>
#include <algorithm>

namespace cntrl {
// Target adapters provide active-low contact readings. Independent contacts
// use the normal switch debounce; a combined third switch settles as a pair.
class AuxiliarySwitches {
public:
    uint32_t held() const { return held_; }
    uint8_t event(unsigned input) const { return events_[input]; }
    template<class Read, class Debounce>
    void scan(const Hardware &hardware, const Document &document, uint32_t now, Read read, Debounce debounce) {
        events_.fill(0); held_ = 0;
        unsigned base = hardware.input_count();
        for (unsigned port = 0; port < states_.size(); ++port) {
            if (!hardware.supports_trs_mode(port, midi::TrsMode::aux)) continue;
            bool declared = false;
            for (const auto &c : hardware.midi_outputs()) if (c.port == port) declared = true;
            if (!declared) continue;
            auto &state = states_[port];
            bool enabled = document.valid() && document.inputs() >= base + 3 && document.trs_modes()[port] == unsigned(midi::TrsMode::aux);
            bool combined = enabled && document.auxiliary_combined(port);
            unsigned sample = enabled ? read(port) & 3u : 0;
            unsigned previous = state.logical;
            if (!enabled) {
                state = State();
            } else if (!state.enabled || state.combined != combined) {
                state.enabled = true; state.combined = combined;
                state.arming = combined && sample;
                state.candidate = sample; state.since = now;
                state.logical = combined ? (sample == 3 ? 4 : sample) : sample;
                for (unsigned contact = 0; contact < 2; ++contact) {
                    auto &button = state.buttons[contact]; button = ButtonState();
                    button.candidate = button.pressed = sample & (1u << contact); button.since = now;
                }
                // Existing held inputs are visible for entry rearming, without
                // inventing a press when a setup enables the socket.
                previous = state.logical;
            } else if (combined) {
                unsigned delay = 20;
                for (unsigned contact = 0; contact < 3; ++contact) delay = std::max<unsigned>(delay, debounce(base + contact).value_or(20));
                if (sample != state.candidate) { state.candidate = sample; state.since = now; }
                if (uint32_t(now - state.since) >= delay) {
                    state.logical = sample == 3 ? 4 : sample;
                    if (state.arming) { previous = state.logical; state.arming = false; }
                }
            } else {
                for (unsigned contact = 0; contact < 2; ++contact) {
                    auto &button = state.buttons[contact];
                    button.update(sample & (1u << contact), now, debounce(base + contact).value_or(20), document.fast_switches());
                    if (button.pressed) state.logical |= 1u << contact;
                    else state.logical &= ~(1u << contact);
                }
            }
            for (unsigned contact = 0; contact < 3; ++contact) {
                unsigned bit = 1u << contact;
                if ((state.logical ^ previous) & bit) events_[base + contact] = state.logical & bit ? press_event : release_event;
                if (state.logical & bit) held_ |= uint32_t(1) << (base + contact);
            }
            base += 3;
        }
    }
private:
    struct State {
        ButtonState buttons[2];
        unsigned candidate = 0, logical = 0;
        uint32_t since = 0;
        bool enabled = false, combined = false, arming = false;
    };
    std::array<State, 4> states_;
    std::array<uint8_t, 32> events_{};
    uint32_t held_ = 0;
};
} // namespace cntrl
