#pragma once
#include "application.h"
#include "pages.h"
#include "renderer.h"
#include "pixel_throb.h"
#include <algorithm>
#include <vector>

namespace cntrl::ui {
// Platform supplies time, physical I/O, display arbitration and optional
// utilities. This class owns page lifecycle, routing, rendering and brightness.
template<class Platform>
class PageHost {
public:
    PageHost(Platform &platform, Application &application, Documents &documents,
             Display &display, const Fonts &fonts, Renderer &renderer, Controls controls)
        : pages_(fonts, display.viewport(display.count() - 1)), platform_(platform),
          application_(application), documents_(documents), display_(display), renderer_(renderer), controls_(controls) { pages_.controls(controls); pages_.monitor = &platform_.midi_monitor(); }
    bool monitor_button(unsigned input, bool pressed) {
        if (pages_.page() != Pages::Page::midi_monitor) return false;
        if (!pressed) return true;
        if (input == controls_.back) {
            leave_page();
            application_.operation_ui(Operation::menu_open, platform_); return true;
        }
        if (!platform_.midi_monitor().active()) return true;
        if (input == controls_.confirm) platform_.midi_monitor().freeze();
        else if (input == controls_.previous || input == controls_.next) platform_.midi_monitor().browse(input == controls_.previous);
        pages_.revisit(); return true;
    }
    bool keyboard_button(unsigned i, bool pressed) {
        if (pages_.page() != Pages::Page::keyboard || (i != controls_.previous && i != controls_.next && i != controls_.confirm)) return false;
        if (i == controls_.confirm) {
            if (!pressed) keyboard_.release(0);
            else {
                const auto shortcut = pages_.keyboard_shortcut();
                pressed = shortcut.name[0] && keyboard_.press(0, shortcut);
            }
            pages_.keyboard_held(pressed);
        } else if (pressed) {
            keyboard_.release(0);
            pages_.keyboard_held(false);
            pages_.keyboard_select(i == controls_.previous);
        }
        return true;
    }
    void poll_keyboard() {
        keyboard_.poll(platform_.keyboard_connected(), platform_);
        if (!platform_.keyboard_connected()) pages_.keyboard_held(false);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_show)
#endif
    void show(unsigned pane, const cntrl::Pane &content) {
        if (content.performance) {
            if (!application_.visible() || application_.feature_open(platform_)) return;
            const auto layout = documents_.document().page_layout();
            const int destination = layout.controller_window(application_.rendering(), pane, pages_.windows());
            if (destination < 0) return;
            if (!performance_visible_) { pages_.revisit(); enter_page(); }
            performance_visible_ = true;
            if (pane == 0) { pending_patch_ = content.pending_patch; apply_brightness(platform_.milliseconds()); }
            renderer_.show(unsigned(destination), content);
            return;
        }
        if (pane == 0) { pending_patch_ = content.pending_patch; apply_brightness(platform_.milliseconds()); }
        if (pane + 1 == display_.count()) {
            if (performance_visible_) leave_page();
            performance_visible_ = false;
        }
        renderer_.show(pane, content);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_configure_pages)
#endif
    void configure_pages(const cntrl::Document &document) {
        pages_.outputs(platform_.midi_outputs());
        configure_ports();
        pages_.layout(document.valid() ? document.page_layout() : cntrl::PageLayout{});
        application_.configure(document, platform_);
        performance_visible_ = true;
        if (document.valid()) enter_page();
    }
    bool refresh_pages() { application_.refresh(platform_); return true; }
    void blank_performance() {
        cntrl::Pane empty;
        empty.graphics.background = 0;
        for (unsigned i = 0; i < display_.count(); ++i) renderer_.show(i, empty);
        pending_patch_ = -1;
        performance_visible_ = true;
        pages_.revisit();
    }
    void select_trs_jack() {
        auto &page = pages_;
        if (ports_.empty()) {
            page.port_name("No configurable ports", false); page.port_status(Pages::PortMode::off, "Unavailable"); return;
        }
        auto port = std::find_if(ports_.begin(), ports_.end(), [&](const Port &p) { return p.index == page.jack; });
        if (port == ports_.end()) { port = ports_.begin(); page.jack = port->index; }
        page.port_name(port->label, true);
        page.candidate = platform_.port_mode(page.jack);
        page.port_status(page.candidate, documents_.document().valid() && documents_.document().trs_modes()[page.jack] != uint8_t(page.candidate) ? "Driver failed" : nullptr);
    }
    bool trs_button(unsigned i) {
        if (pages_.page() != Pages::Page::ports || (i != controls_.previous && i != controls_.next)) return false;
        if (ports_.empty()) { select_trs_jack(); return true; }
        if (i == controls_.previous) {
            pages_.jack = next_port(pages_.jack, false);
            select_trs_jack();
        } else for (unsigned mode = 0; mode < 5; ++mode) {
            pages_.next_port_mode();
            if (platform_.supports_trs_mode(pages_.jack, pages_.candidate)) break;
        }
        return true;
    }
    void enter_page() {
        if (pages_.page() == Pages::Page::midi_monitor) {
            application_.controller().cancel_gesture();
            if (!platform_.midi_monitor().active()) {
                monitor_unavailable_ = !platform_.midi_monitor().open(platform_.microseconds());
                if (monitor_unavailable_) error("MIDI monitor memory unavailable");
            }
        }
        if (pages_.page() == Pages::Page::ports && documents_.document().valid()) select_trs_jack();
        if (pages_.page() == Pages::Page::expression) {
            if (expression_ports_.empty()) pages_.expression_status("No configurable ports", "", "");
            else if (std::find(expression_ports_.begin(), expression_ports_.end(), platform_.expression_controls().selected()) == expression_ports_.end())
                platform_.expression_controls().select(expression_ports_[0]);
        }
#if CNTRL_CABLE_TEST
        if (pages_.page() == Pages::Page::cable) platform_.cable_test.enter();
#endif
#if CNTRL_PANIC
        if (pages_.page() == Pages::Page::panic) platform_.panic.enter(platform_.runtime.brightness);
#endif
    }
    void leave_page() {
        if (pages_.page() == Pages::Page::midi_monitor) { platform_.midi_monitor().close(); monitor_unavailable_ = false; }
        keyboard_.clear(); pages_.keyboard_held(false);
        if (pages_.page() == Pages::Page::expression) platform_.expression_controls().cancel();
#if CNTRL_CABLE_TEST
        if (pages_.page() == Pages::Page::cable) platform_.leave_cable_test();
#endif
#if CNTRL_PANIC
        if (pages_.page() == Pages::Page::panic) platform_.leave_panic();
#endif
    }
    bool stop_clock() {
        pages_.generator.stop();
        pages_.generator.poll(platform_.microseconds(), platform_.clock_available(), platform_);
        return !pages_.generator.stopping();
    }
    bool clock_control(unsigned button, bool pressed) {
        if (!pages_.clock_page()) return false;
        if (pressed) pages_.clock_button(button == controls_.previous ? 0 : button == controls_.next ? 1 : button == controls_.confirm ? 2 : button == controls_.next_section ? 4 : Controls::absent, platform_.microseconds(), platform_.clock_available());
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_configure)
#endif
    void configure_page_settings(const cntrl::PageSettings &settings) {
        pages_.outputs(platform_.midi_outputs());
        configure_ports();
        leave_page();
        platform_.expression_controls().configure(settings.expression);
        pages_.configure(settings);
        platform_.midi_bridge().clock_owner(pages_.clock_owner() | platform_.midi_file_clock_outputs());
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_browse)
#endif
    void browse_page(bool reverse) {
        if (!documents_.document().valid()) return;
        if (pages_.page() == Pages::Page::confirm_firmware
            && controls_.confirm < 64 && (application_.held & (uint64_t(1) << controls_.confirm))) return;
        const unsigned previous_windows = pages_.windows();
        const unsigned previous_controller = application_.visible_index;
        leave_page();
        if (pages_.browse(reverse)) {
            if (!platform_.switch_firmware()) error("Firmware switch failed");
        } else {
            focus_controller();
            const unsigned windows = std::max(previous_windows, pages_.windows());
            const bool utility_only = previous_controller == application_.visible_index && windows == 1;
            renderer_.transition(reverse, documents_.document().scroll_ms(), utility_only ? int(display_.count() - 1) : -1);
            application_.refresh(platform_);
        }
        if (pages_.clock_page()) application_.controller().cancel_gesture();
        enter_page();
    }
    void select_page(unsigned index) {
        leave_page(); pages_.select(index);
        focus_controller();
        application_.refresh(platform_); enter_page();
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_tap)
#endif
    void tap_tempo(uint32_t now) {
        if (!documents_.document().valid()) return;
        auto &page = pages_;
        if (page.page() == Pages::Page::ports) {
            if (ports_.empty()) { select_trs_jack(); return; }
            auto active = platform_.port_mode(page.jack);
            if (!platform_.output_idle() || platform_.configuration_locked() || documents_.busy() || documents_.dirty()) page.port_status(active, "Busy");
            else if (active == page.candidate && documents_.document().trs_modes()[page.jack] == uint8_t(active)) page.port_status(active, "Already set");
            else if (!documents_.save_trs_mode(page.jack, page.candidate, now)) page.port_status(active, "Cannot apply");
            else { port_save_pending_ = true; page.port_status(active, "Saving"); }
            return;
        }
#if CNTRL_CABLE_TEST
        if (pages_.page() == Pages::Page::cable) {
            if (!platform_.cable_test.enabled() && (application_.patch_pending() || !platform_.output_idle() || documents_.busy() || !stop_clock())) return;
            application_.controller().cancel_gesture();
            platform_.cable_test.toggle(now);
            if (platform_.cable_test.enabled())
                for (const auto &output : platform_.midi_outputs()) platform_.forget_output(output.route);
            platform_.midi_bridge().configure(platform_.cable_test.enabled() ? false : platform_.runtime.interface);
            return;
        }
#endif
#if CNTRL_PANIC
        if (pages_.page() == Pages::Page::panic) {
            if (!platform_.panic_owns_midi && (application_.patch_pending() || !platform_.output_idle() || documents_.busy() || !stop_clock())) return;
            application_.controller().cancel_gesture();
            if (!platform_.panic_owns_midi) platform_.midi_bridge().configure(false);
            platform_.panic_owns_midi = true;
            platform_.panic.button(true, now);
            return;
        }
#endif
        if (pages_.page() == Pages::Page::expression) {
            if (expression_ports_.empty()) { pages_.expression_status("No configurable ports", "", ""); return; }
            platform_.expression_controls().calibrate(platform_.expression_available(platform_.expression_controls().selected()));
            return;
        }
        pages_.tap(now);
    }
    void set_target_pulse(unsigned period, uint8_t low, uint8_t high, unsigned delay) {
        throb_.configure(period, low, high, delay);
    }
    void transition(bool undo, std::optional<unsigned> duration_ms, int pane, bool vertical = false, bool panorama = true) {
        if (!application_.visible() || application_.feature_open(platform_)) return;
        const unsigned occupied = pages_.windows();
        if (occupied == display_.count()) return;
        if (pane >= 0) {
            pane = documents_.document().page_layout().controller_window(application_.rendering(), unsigned(pane), occupied);
            if (pane < 0) return;
        } else if (occupied + 1 == display_.count()) pane = 0;
        renderer_.transition(undo, duration_ms, pane, occupied, vertical, panorama);
    }
    void set_brightness(uint8_t value, bool temporary = false) {
        brightness_feedback_ = temporary;
        if (temporary) feedback_brightness_ = value;
        else { platform_.runtime.brightness = value; application_.brightness(value); }
        if (temporary) platform_.animation_session().stop();
        apply_brightness(platform_.milliseconds());
    }
    void set_splash(bool enabled, uint32_t idle_ms) {
        platform_.animation_session().configure(enabled, idle_ms, platform_.milliseconds());
    }
    void set_movies(const uint8_t *startup, unsigned startup_count, const uint8_t *idle, unsigned idle_count) {
        platform_.animation_session().movies(startup, startup_count, idle, idle_count);
    }
    void error(const char *message) {
        pending_patch_ = -1;
        renderer_.error(message);
    }
    void draw() {
        if (platform_.animation_session().take_redraw()) renderer_.invalidate();
        renderer_.draw(platform_.microseconds());
    }
    bool expression_button(unsigned i) {
        if (pages_.page() != Pages::Page::expression || (i != controls_.previous && i != controls_.next)) return false;
        if (expression_ports_.empty()) { pages_.expression_status("No configurable ports", "", ""); return true; }
        if (i == controls_.previous) platform_.expression_controls().select(next_port(platform_.expression_controls().selected(), true));
        else platform_.expression_controls().cancel();
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_display)
#endif
    void display(uint32_t now, bool transfer_busy) {
        const auto &message = platform_.text_messages();
        if (message.active(now)) platform_.animation_session().stop();
        renderer_.message(message, now);
        if (application_.pending_page < application_.layout.pages()) {
            select_page(application_.pending_page); application_.pending_page = PageLayout::max_pages;
        }
        if (platform_.display_priority_due()) return;
        if (pages_.page() == Pages::Page::expression && expression_ports_.empty()) pages_.expression_status("No configurable ports", "", "");
        if (pages_.page() == Pages::Page::expression && !expression_ports_.empty() && uint32_t(now - expression_refresh_) >= 80) {
            expression_refresh_ = now;
            const unsigned selected = platform_.expression_controls().selected();
            const auto port = std::find_if(ports_.begin(), ports_.end(), [&](const Port &p) { return p.index == selected; });
            char title[40], position[40], prompt[40];
            platform_.expression_controls().status(port->label, platform_.expression_available(selected), title, position, prompt);
            pages_.expression_status(title, position, prompt);
        }
#if CNTRL_CABLE_TEST
        transfer_busy = transfer_busy || platform_.cable_test.enabled();
#endif
#if CNTRL_PANIC
        transfer_busy = transfer_busy || platform_.panic_owns_midi;
#endif
        if (port_save_pending_ && !documents_.busy()) {
            port_save_pending_ = false;
            if (documents_.dirty()) {
                documents_.edit_cancel();
                pages_.port_status(platform_.port_mode(pages_.jack), "Save failed");
            }
        }
        pages_.time(now, platform_.microseconds(), controls_.confirm < 64 && (application_.held & (uint64_t(1) << controls_.confirm)));
        platform_.displayed();
        if (!platform_.display_busy()) splash_visible_ = platform_.animation_session().poll(now, renderer_.ready(), transfer_busy);
        apply_brightness(now);
#if CNTRL_CABLE_TEST
        if (pages_.page() == Pages::Page::cable && platform_.cable_test.take_redraw())
            pages_.revisit();
#endif
#if CNTRL_PANIC
        if (pages_.page() == Pages::Page::panic && platform_.panic.take_redraw())
            pages_.revisit();
#endif
        if (!splash_visible_ && !application_.feature_open(platform_) && documents_.document().valid()
            && pages_.windows() && pages_.due(now)) {
            if (pages_.page() == Pages::Page::midi_monitor && !platform_.midi_monitor().active() && !monitor_unavailable_) enter_page();
            // Prepare the incoming card during the renderer's bounded transition preparation.
            PerformanceStatus status = pages_.page() == Pages::Page::status ? platform_.status() : PerformanceStatus{};
            for (unsigned i = 0; i < pages_.windows(); ++i) {
                const unsigned screen = display_.count() - pages_.windows() + i;
                pages_.viewport(display_.viewport(screen));
                const cntrl::Pane content = pages_.view(now, platform_.microseconds(), status
#if CNTRL_CABLE_TEST
                , platform_.cable_test.status()
#else
                , {}
#endif
#if CNTRL_PANIC
                , platform_.panic.status()
#else
                , cntrl::Panic::Status::ready
#endif
                , i, pages_.windows()
            );
                renderer_.show(screen, pages_.page() == Pages::Page::midi_monitor ? content : Pages::window_view(content, i, pages_.windows()));
            }
        }
        if (!splash_visible_) draw();
    }
    void apply_brightness(uint32_t now) {
        renderer_.target_brightness(throb_.brightness(
            !splash_visible_ && !brightness_feedback_ ? pending_patch_ : -1, now, 128, application_.held));
        uint8_t brightness = brightness_feedback_ ? feedback_brightness_ : platform_.animation_session().brightness(platform_.runtime.brightness, now);
        if (brightness != applied_brightness_) {
            platform_.display_brightness(brightness); applied_brightness_ = brightness;
        }
    }
    int hardware_brightness() const { return applied_brightness_; }
    Pages pages_;
    Keyboard keyboard_;
private:
    void focus_controller() {
        if (pages_.page() != Pages::Page::patch_switcher) return;
        application_.controller().cancel_gesture();
        application_.visible_index = application_.layout.controller_slot(pages_.index());
    }
    struct Port { unsigned index; const char *label; };
    std::vector<Port> ports_;
    std::vector<unsigned> expression_ports_;
    void configure_ports() {
        ports_.clear(); expression_ports_.clear();
        for (const auto &endpoint : platform_.midi_outputs()) {
            if (endpoint.port == midi::no_port) continue;
            if (std::any_of(ports_.begin(), ports_.end(), [&](const Port &p) { return p.index == endpoint.port; })) continue;
            ports_.push_back({endpoint.port, endpoint.label});
            if (platform_.supports_trs_mode(endpoint.port, midi::TrsMode::expression)) expression_ports_.push_back(endpoint.port);
        }
        select_trs_jack();
    }
    unsigned next_port(unsigned current, bool expression) const {
        if (expression) {
            auto at = std::find(expression_ports_.begin(), expression_ports_.end(), current);
            return at == expression_ports_.end() || ++at == expression_ports_.end() ? expression_ports_[0] : *at;
        }
        auto at = std::find_if(ports_.begin(), ports_.end(), [&](const Port &p) { return p.index == current; });
        return at == ports_.end() || ++at == ports_.end() ? ports_[0].index : at->index;
    }
    Platform &platform_;
    Application &application_;
    Documents &documents_;
    Display &display_;
    Renderer &renderer_;
    Controls controls_;
    uint32_t expression_refresh_ = 0;
    PixelThrob throb_;
    bool port_save_pending_ = false, performance_visible_ = false, splash_visible_ = false;
    int pending_patch_ = -1;
    bool brightness_feedback_ = false;
    bool monitor_unavailable_ = false;
    uint8_t feedback_brightness_ = 255;
    int applied_brightness_ = -1;
};
} // namespace cntrl::ui
