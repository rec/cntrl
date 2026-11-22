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
        const bool performing = application_.song_navigation_active();
        uint8_t assigned_pages = 0;
        const bool configured = route_chords(services, events, pressed, performing, consumed, assigned_pages);
        if (!configured && !consumed && performing && !application_.controls_locked
            && controls_.back < controls_.count && controls_.confirm < controls_.count) {
            const unsigned next = controls_.back, modifier = controls_.confirm;
            if (events[modifier] & press_event) modifier_used_ = false;
            if (events[next] & press_event) next_step_held_ = true;
            if (next_step_held_ && (pressed & (1u << modifier))) previous_step_ = modifier_used_ = true;
            if ((events[modifier] & hold_event) && !modifier_used_ && pressed == (1u << modifier)) {
                application_.pending_operation = 9;
                application_.controls_locked = true;
                next_step_held_ = previous_step_ = false;
            }
            if (next_step_held_ && (events[next] & release_event)) {
                application_.request_song_step(previous_step_);
                next_step_held_ = previous_step_ = false;
            }
            events[next] = events[modifier] = 0;
            if (application_.song_boundary() && application_.binding_return_songs)
                for (auto &event : events) event &= release_event;
        } else next_step_held_ = previous_step_ = false;
        if (!consumed) router_.route(services.actions(), events.data(), pressed, now, application_.utility_visible(), assigned_pages);
    }
    template<class Actions>
    void finish(Actions &actions) { router_.finish(actions, application_.pending_operation, application_.return_to_switches, application_.controls_locked); }
private:
    template<class Services>
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(input_navigation_chords)
#endif
    bool route_chords(Services &services, std::array<uint8_t, 8> &events, uint8_t pressed, bool performing, bool consumed, uint8_t &assigned_pages) {
        const auto *setup = application_.layout.navigation();
        if (!setup && !performing && !pending_members_) return false;
        uint8_t chords[4] = {};
        if (setup) std::copy_n(setup, 4, chords);
        if (performing && !application_.song_boundary()) {
            for (unsigned i = 0; i < 2; ++i)
                if (auto mask = application_services_.scene_page_chord(i)) chords[2 + i] = mask;
        }
        const bool configured = setup || chords[2] || chords[3];
        assigned_pages = unsigned(chords[2] != 0) | unsigned(chords[3] != 0) << 1;
        if (!consumed && configured && !application_.controls_locked) {
            if (performing) {
                const auto bit = [](unsigned input) -> uint8_t { return input < 8 ? uint8_t(1u << input) : 0; };
                if (!chords[0]) chords[0] = bit(controls_.back);
                if (!chords[1]) chords[1] = bit(controls_.back) | bit(controls_.confirm);
                for (unsigned i = 2; i < 4; ++i)
                    for (unsigned song = 0; song < 2; ++song)
                        if (chords[song] && (chords[i] & chords[song]) == chords[song]) chords[i] = 0;
            } else chords[0] = chords[1] = 0;
            uint8_t members = chords[0] | chords[1] | chords[2] | chords[3];
            for (unsigned i = 0; i < controls_.count; ++i)
                if ((members & (1u << i)) && (events[i] & press_event)) pending_members_ |= uint8_t(1u << i);
            if (!chord_used_) {
                if (performing && controls_.confirm < controls_.count && pressed == (1u << controls_.confirm)
                    && (pending_members_ & pressed) && (events[controls_.confirm] & hold_event)) {
                    application_.pending_operation = 9;
                    application_.controls_locked = true;
                    chord_used_ = pressed;
                }
                for (unsigned i = 0; i < 4; ++i) {
                    if (!chords[i] || pressed != chords[i] || (pressed & (pressed - 1)) == 0) continue;
                    if (i < 2) application_.request_song_step(i == 1);
                    else services.actions().browse(i == 3);
                    chord_used_ = pressed; break;
                }
            }
        }
        if (pending_members_) {
            for (unsigned i = 0; i < controls_.count; ++i) {
                const uint8_t bit = uint8_t(1u << i);
                if (!(pending_members_ & bit)) continue;
                const bool released = events[i] & release_event;
                events[i] = 0;
                if (!released) continue;
                pending_members_ &= ~bit;
                if (consumed || (chord_used_ & bit) || application_.controls_locked) continue;
                unsigned action = 0;
                while (action < 4 && chords[action] != bit) ++action;
                if (action < 2) application_.request_song_step(action == 1);
                else if (action < 4) services.actions().browse(action == 3);
                else events[i] = press_event | release_event;
            }
            if (!pending_members_) chord_used_ = 0;
        }
        return configured;
    }
    ButtonState *states_;
    Controls controls_;
    const Document &configuration_;
    Application &application_;
    ApplicationServices &application_services_;
    ControlRouter router_;
    bool next_step_held_ = false, previous_step_ = false;
    bool modifier_used_ = false;
    uint8_t pending_members_ = 0, chord_used_ = 0;
};
} // namespace cntrl::ui
