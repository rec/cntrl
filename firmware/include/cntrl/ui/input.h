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
        application_.setups.poll(pressed); application_.song_ui.poll(pressed);
        if (!pressed) application_.controls_locked = false;
        if (auto *visit = services.visit()) { visit->poll(!pressed); consumed = consumed || visit->busy(); }
        const bool performing = application_.song_navigation_active();
        uint8_t assigned_pages = 0;
        const auto *setup = application_.layout.navigation();
        const bool song_gestures = performing && !setup && controls_.back < controls_.count;
        route_chords(services, events, pressed, performing, consumed, assigned_pages, setup);
        route_song_gestures(services.actions(), events, song_gestures && !consumed, assigned_pages);
        if (song_gestures && application_.song_boundary() && application_.binding_return_songs)
            for (auto &event : events) event &= release_event;
        if (!consumed) router_.route(services.actions(), events.data(), pressed, now, application_.utility_visible(), assigned_pages, song_gestures, performing);
    }
    template<class Actions>
    void finish(Actions &actions) { router_.finish(actions, application_.pending_operation, application_.return_to_switches, application_.controls_locked); }
private:
    template<class Actions>
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(input_navigation_gestures)
#endif
    void route_song_gestures(Actions &actions, std::array<uint8_t, 8> &events, bool enabled, uint8_t assigned_pages) {
        if (!enabled || application_.controls_locked) {
            navigation_pressed_ = navigation_tap_ = false;
            return;
        }
        auto &event = events[controls_.back];
        const auto &state = states_[controls_.back];
        const auto edge = event;
        event = 0;
        // A short release stays pending so a second press can become a hold.
        const bool second = navigation_tap_ && (edge & double_press_event);
        if (navigation_tap_ && !state.waiting_for_second) {
            if (!second) application_.request_song_step(false);
            navigation_tap_ = false;
        }
        if (edge & press_event) {
            navigation_pressed_ = true;
            navigation_second_ = second;
        }
        if (!navigation_pressed_) return;
        if (edge & hold_event) {
            if (!(assigned_pages & (navigation_second_ ? 2 : 1))) actions.browse(navigation_second_);
        }
        if (edge & release_event) {
            navigation_pressed_ = false;
            if (state.held) return;
            if (!navigation_second_) navigation_tap_ = true;
            else application_.request_song_step(true);
        }
    }

    template<class Services>
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(input_navigation_chords)
#endif
    void route_chords(Services &services, std::array<uint8_t, 8> &events, uint8_t pressed, bool performing,
                      bool consumed, uint8_t &assigned_pages, const uint8_t *setup) {
        if (!setup && !performing && !pending_members_) return;
        uint8_t chords[4] = {};
        if (setup) std::copy_n(setup, 4, chords);
        if (performing && !application_.song_boundary()) {
            for (unsigned i = 0; i < 2; ++i)
                if (auto mask = application_services_.scene_page_chord(i)) chords[2 + i] = mask;
        }
        const uint8_t back = controls_.back < 8 ? uint8_t(1u << controls_.back) : 0;
        if (performing && !application_.utility_visible()) {
            if (setup && !chords[0]) chords[0] = back;
            for (unsigned i = 2; i < 4; ++i) {
                if (!setup && (chords[i] & back)) chords[i] = 0;
                for (unsigned song = 0; song < 2; ++song)
                    if (chords[song] && (chords[i] & chords[song]) == chords[song]) chords[i] = 0;
            }
        } else {
            chords[0] = chords[1] = 0;
            if (!performing) for (unsigned i = 2; i < 4; ++i)
                if (chords[i] & back) chords[i] = 0;
        }
        assigned_pages = unsigned(chords[2] != 0) | unsigned(chords[3] != 0) << 1;
        if (!consumed && !application_.controls_locked) {
            uint8_t members = chords[0] | chords[1] | chords[2] | chords[3];
            for (unsigned i = 0; i < controls_.count; ++i)
                if ((members & (1u << i)) && (events[i] & press_event)) pending_members_ |= uint8_t(1u << i);
            if (!chord_used_) {
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
                else if (performing || i != controls_.back) events[i] = press_event | release_event;
            }
            if (!pending_members_) chord_used_ = 0;
        }
    }
    ButtonState *states_;
    Controls controls_;
    const Document &configuration_;
    Application &application_;
    ApplicationServices &application_services_;
    ControlRouter router_;
    bool navigation_pressed_ = false, navigation_tap_ = false, navigation_second_ = false;
    uint8_t pending_members_ = 0, chord_used_ = 0;
};
} // namespace cntrl::ui
