#pragma once
#include <cstdint>
#include <limits>

namespace cntrl::ui {
enum class Operation : unsigned {
    menu_previous = 128, menu_next, menu_activate, menu_back, menu_open, performance_show,
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
};
class ControlRouter {
public:
    explicit ControlRouter(Controls controls) : controls_(controls) {}
    template<class Services>
    void route(Services &services, const uint8_t *events, uint64_t held, uint32_t now) const {
        for (unsigned i = 0; i < controls_.count; ++i) {
            const bool press = events[i] & 1, release = events[i] & 2;
            // Editors must receive release even when navigation locks input.
            if (services.switches_open() && release) services.switch_button(i, false, now);
            if (services.controls_locked()) continue;
            if (services.menu_open()) {
                if (press) {
                    if (i == controls_.previous) services.invoke(Operation::menu_previous);
                    else if (i == controls_.next) services.invoke(Operation::menu_next);
                    else if (i == controls_.confirm) services.invoke(Operation::menu_activate);
                    else if (i == controls_.back) services.invoke(Operation::menu_back);
                }
            } else if (services.switches_open()) {
                if (press) services.switch_button(i, true, now);
                if (release) services.switch_button(i, false, now);
            } else if (services.songs_open()) {
                if (press) {
                    if (i == controls_.previous) services.invoke(Operation::songs_previous);
                    else if (i == controls_.next) services.invoke(Operation::songs_next);
                    else if (i == controls_.confirm) services.invoke(Operation::songs_confirm);
                    else if (i == controls_.previous_section) services.invoke(Operation::songs_previous_section);
                    else if (i == controls_.next_section) services.invoke(Operation::songs_next_section);
                    else if (i == controls_.back) services.invoke(Operation::songs_back);
                }
            } else if (services.setup_open()) {
                if (press) {
                    if (i == controls_.previous) services.invoke(Operation::setup_previous);
                    else if (i == controls_.next) services.invoke(Operation::setup_next);
                    else if (i == controls_.confirm) services.invoke(Operation::setup_confirm);
                    else if (i == controls_.back) services.invoke(Operation::setup_back);
                }
            } else {
                if (i == controls_.next_page) {
                    if (press) services.browse(false);
                    continue;
                }
                if (i == controls_.reverse_page && controls_.next_page < 64
                    && (held & (uint64_t(1) << controls_.next_page))) {
                    if (press) services.browse(true);
                    continue;
                }
                if (services.midi_reserved() && i != controls_.confirm && i != controls_.back) continue;
                if ((press || release) && services.keyboard(i, press)) continue;
                if ((press || release) && services.monitor(i, press)) continue;
                if (press && services.expression(i)) continue;
                if (press && services.port(i)) continue;
                if (services.clock(i, press)) continue;
                if (services.has_controller()) {
                    if (press) services.controller(controls_.bindings[i].controller_button, true, now);
                    if (release) services.controller(controls_.bindings[i].controller_button, false, now);
                } else if (press && i == controls_.tap) services.tap(now);
            }
        }
    }
    template<class Services>
    void finish(Services &services, unsigned &pending, bool &returning, bool &locked) {
        if (switches_open_ && !services.switches_open()) {
            bool performance = services.leave_switches();
            services.invoke(pending == 10 || (performance && pending != 9) ? Operation::performance_show : Operation::menu_open);
            pending = 0; locked = true;
            if (services.abandoned()) services.error("Switch state unknown");
        }
        switches_open_ = services.switches_open();
        if ((pending || !services.patch_pending()) && !services.switches_open()) {
            if (pending) {
                services.invoke(pending == 9 ? Operation::menu_open : Operation::performance_show);
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
