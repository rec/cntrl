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
    void scan(uint8_t sample, uint32_t now, Services &services) {
        std::array<uint8_t, 8> events{};
        uint8_t pressed = 0;
        const bool fast = !configuration_.valid() || configuration_.fast_switches();
        for (unsigned i = 0; i < controls_.count; ++i) {
            events[i] = states_[i].update(sample & (1u << i), now, services.debounce(i).value_or(15), fast);
            if (states_[i].pressed) pressed |= uint8_t(1u << i);
        }
        application_.held = pressed;
        if (controls_.confirm < controls_.count && (events[controls_.confirm] & release_event)) {
            services.keyboard_release(controls_.confirm);
            if (services.panic_open()) services.panic_release(now);
        }
        const bool replay = !application_.feature_open(application_services_) && controls_.previous < controls_.count && pressed == (1u << controls_.previous);
        bool consumed = services.animation_controls(pressed, now, replay);
        consumed = services.diagnostic_controls(events.data(), pressed, now) || consumed;
        if (consumed) application_.controller().cancel_gesture();
        application_.setups.poll(pressed); application_.menu.poll(pressed); application_.song_ui.poll(pressed);
        if (!pressed) application_.controls_locked = false;
        if (auto *visit = services.visit()) { visit->poll(!pressed); consumed = consumed || visit->busy(); }
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
};
} // namespace cntrl::ui
