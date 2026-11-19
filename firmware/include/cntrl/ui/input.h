#pragma once
#include "firmware_visit.h"
#include "controls.h"
#include <cntrl/buttons.h>
#include <array>

namespace cntrl::ui {
// Input samples are already logical active-high bits. The platform owns GPIO
// polarity and bus access; this coordinator owns debounce and UI focus policy.
class InputCoordinator {
public:
    InputCoordinator(ButtonState *states, Controls controls, const Document &configuration,
                     Application &application, ApplicationServices &application_services)
        : states_(states), controls_(controls), configuration_(configuration), application_(application),
          application_services_(application_services), router_(controls) {}
    template<class Services>
    void scan(uint8_t sample, uint32_t now, Services &services, uint32_t auxiliary_held = 0) {
        std::array<uint8_t, 8> events{};
        uint8_t pressed = 0;
        const bool fast = !configuration_.valid() || configuration_.fast_switches();
        for (unsigned i = 0; i < controls_.count; ++i) {
            events[i] = states_[i].update(sample & (1u << i), now, services.debounce(i).value_or(15), fast);
            if (states_[i].pressed) pressed |= uint8_t(1u << i);
        }
        application_.held = pressed | auxiliary_held;
        if (controls_.confirm < controls_.count && (events[controls_.confirm] & release_event)) {
            if (services.panic_open()) services.panic_release(now);
        }
        const bool replay = !application_.feature_open(application_services_) && controls_.previous < controls_.count && pressed == (1u << controls_.previous);
        bool consumed = services.animation_controls(pressed, now, replay);
        consumed = services.diagnostic_controls(events.data(), pressed, now) || consumed;
        if (consumed) application_.controller().cancel_gesture();
        application_.setups.poll(pressed); application_.menu.poll(pressed); application_.song_ui.poll(pressed);
        if (!pressed) application_.controls_locked = false;
        if (auto *visit = services.visit()) { visit->poll(!pressed); consumed = consumed || visit->busy(); }
        if (!consumed && application_.song_navigation_active() && !application_.controls_locked
            && controls_.back < controls_.count && controls_.confirm < controls_.count) {
            const unsigned next = controls_.back, modifier = controls_.confirm;
            if (events[next] & press_event) next_step_held_ = true;
            if (next_step_held_ && (pressed & (1u << modifier))) previous_step_ = true;
            if (next_step_held_ && (events[next] & release_event)) {
                application_.request_song_step(previous_step_);
                next_step_held_ = previous_step_ = false;
            }
            events[next] = events[modifier] = 0;
            if (application_.song_last_step() && application_.binding_return_songs)
                for (auto &event : events) event &= release_event;
        } else next_step_held_ = previous_step_ = false;
        if (!consumed) router_.route(services.actions(), events.data(), pressed, now);
    }
    template<class Actions>
    void finish(Actions &actions) { router_.finish(actions, application_.pending_operation, application_.return_to_switches, application_.controls_locked); }
private:
    ButtonState *states_;
    Controls controls_;
    const Document &configuration_;
    Application &application_;
    ApplicationServices &application_services_;
    ControlRouter router_;
    bool next_step_held_ = false, previous_step_ = false;
};
} // namespace cntrl::ui
