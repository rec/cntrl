#pragma once
#include "messages.h"
#include <cstdint>
#include <limits>

namespace cntrl::ui {
enum class Operation : unsigned {
    performance_show = 133,
    setup_previous, setup_next, setup_confirm, setup_back,
    songs_previous, songs_next, songs_confirm, songs_previous_section, songs_next_section, songs_back,
    switches_return = 153,
};
struct ControlBinding { unsigned controller_button; const char *label; };
struct Controls {
    static constexpr unsigned absent = std::numeric_limits<unsigned>::max();
    unsigned previous = absent, next = absent, confirm = absent, previous_section = absent, next_section = absent, back = absent;
    unsigned next_page = absent, reverse_page = absent, tap = absent;
    const ControlBinding *bindings = nullptr;
    unsigned count = 0;
    unsigned previous_page = absent;
};
class ControlRouter {
public:
    explicit ControlRouter(Controls controls) : controls_(controls) {}
    template<class Services>
    void route(Services &services, const uint8_t *events, uint64_t held, uint32_t now,
               bool utility = false, uint8_t assigned_pages = 0, bool song_gestures = false, bool performing = false) const {
        for (unsigned i = 0; i < controls_.count; ++i) {
            const bool press = events[i] & 1, release = events[i] & 2;
            // Editors must receive release even when navigation locks input.
            if (services.switches_open() && release) services.switch_button(i, false, now);
            if (services.controls_locked()) continue;
            if (!utility && services.switches_open() && !services.songs_open() && !services.setup_open()) {
                if (press) services.switch_button(i, true, now);
                if (release) services.switch_button(i, false, now);
            } else if (!utility && services.songs_open()) {
                if (press) {
                    if (i == controls_.previous) services.invoke(Operation::songs_previous);
                    else if (i == controls_.next) services.invoke(Operation::songs_next);
                    else if (i == controls_.confirm) services.invoke(Operation::songs_confirm);
                    else if (i == controls_.previous_section) services.invoke(Operation::songs_previous_section);
                    else if (i == controls_.next_section) services.invoke(Operation::songs_next_section);
                    else if (i == controls_.back) services.invoke(Operation::songs_back);
                }
            } else if (!utility && services.setup_open()) {
                if (press) {
                    if (i == controls_.previous) services.invoke(Operation::setup_previous);
                    else if (i == controls_.next) services.invoke(Operation::setup_next);
                    else if (i == controls_.confirm) services.invoke(Operation::setup_confirm);
                    else if (i == controls_.back) services.invoke(Operation::setup_back);
                }
            } else {
                if (!services.midi_reserved() && (press || release) && services.looper(i, press)) continue;
                if (song_gestures && i == controls_.back) continue;
                if ((!performing && i == controls_.back) || (i == controls_.next_page && !(assigned_pages & 1))) {
                    if (press) services.browse(!(assigned_pages & 2) && controls_.reverse_page < 64
                        && (held & (uint64_t(1) << controls_.reverse_page)));
                    continue;
                }
                if (i == controls_.previous_page && !(assigned_pages & 2)) {
                    if (press) services.browse(true);
                    continue;
                }
                if (!(assigned_pages & 2) && !(song_gestures && controls_.back == controls_.next_page)
                    && i == controls_.reverse_page && controls_.next_page < 64
                    && (held & (uint64_t(1) << controls_.next_page))) {
                    if (press) services.browse(true);
                    continue;
                }
                if (services.midi_reserved() && i != controls_.confirm && i != controls_.back) continue;
                if (services.time(i, events[i], now)) continue;
                if ((press || release) && services.monitor(i, press)) continue;
                if (!utility && services.has_controller()) {
                    if (press) services.controller(controls_.bindings[i].controller_button, true, now);
                    if (release) services.controller(controls_.bindings[i].controller_button, false, now);
                } else if (press && i == controls_.tap) services.tap(now);
            }
        }
    }
    template<class Services>
    void finish(Services &services, unsigned &pending, bool &returning, bool &locked) {
        if (switches_open_ && !services.switches_open()) {
            services.leave_switches();
            services.invoke(Operation::performance_show);
            pending = 0; locked = true;
            if (services.abandoned()) services.error(i18n::text(i18n::Message::switch_state_unknown));
        }
        switches_open_ = services.switches_open();
        if ((pending || !services.patch_pending()) && !services.switches_open()) {
            if (pending) {
                services.invoke(Operation::performance_show);
                pending = 0; returning = false; locked = true;
            } else if (returning) {
                returning = false; services.invoke(Operation::switches_return);
            }
        }
    }
private:
    bool switches_open_ = false;
    Controls controls_;
};
} // namespace cntrl::ui
