#pragma once
#include "application.h"
#include "pages.h"
#include "renderer.h"
#include "pixel_throb.h"
#include <cntrl/midi_files.h>
#include <cntrl/checked_array.h>
#include <algorithm>

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
    void looper_files(midi::MidiFiles &files, midi::Sink &sink) { looper_files_ = &files; looper_sink_ = &sink; pages_.looper = &files.looper; }
    void arpeggiator(midi::MidiArpeggiator &engine) { pages_.arpeggiator = &engine; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_action)
#endif
    bool looper_action(unsigned action, const char *filename) {
        if (!looper_files_ || (looper_files_->storage_busy() && action != 1 && action != 3 && action != 7)) return false;
        if ((action == 4 || action == 5) && !platform_.looper_storage_ready()) return false;
        auto &files = *looper_files_; auto &loop = files.looper;
        if (action == 0) return loop.record(pages_.beat_position(platform_.microseconds()),pages_.clock_period());
        if (action == 1) return loop.play_stop(pages_.beat_position(platform_.microseconds()));
        if (action == 2) return loop.undo(pages_.beat_position(platform_.microseconds()));
        if (action == 6) return loop.reverse(pages_.beat_position(platform_.microseconds()));
        if (action == 7) return loop.direction(pages_.beat_position(platform_.microseconds()));
        if (action == 3) return loop.clear();
        if (action == 4) {
            auto status = platform_.status();
            return files.save_loop(pages_.clock_period(),status.starts,status.starts_status == StartsStatus::ready);
        }
        return action == 5 && files.load_loop(filename);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_button)
#endif
    bool looper_button(unsigned input, bool pressed) {
        if (pages_.page() == Pages::Page::arpeggiator && pages_.arpeggiator) {
            if (input != controls_.next && (input != controls_.previous || !pages_.arpeggiator->mono())) return false;
            if (!pressed) return true;
            auto &arp = *pages_.arpeggiator;
            uint32_t now = platform_.microseconds();
            if (!arp.mono() && arp.state() == midi::MidiArpeggiator::State::stopped && !pages_.external_clock() && !pages_.generator.running())
                pages_.generator.start(now,pages_.clock_outputs() & platform_.clock_available(),false);
            if (input == controls_.previous) arp.record(pages_.arpeggiator_beat_position(now));
            else arp.toggle(pages_.arpeggiator_beat_position(now));
            pages_.revisit(); return true;
        }
        if (pages_.page() != Pages::Page::midi_looper || !looper_files_) return false;
        auto &files = *looper_files_; auto &loop = files.looper;
        if (input == controls_.back) {
            if (looper_menu_ == LooperMenu::none) return false;
            files.cancel_browse(); looper_menu_ = LooperMenu::none; pages_.looper_prompt[0] = 0; pages_.revisit(); return true;
        }
        if (!pressed) return true;
        bool ok = true;
        if (looper_menu_ == LooperMenu::clear) {
            if (input == controls_.confirm) { ok = loop.clear(); looper_menu_ = LooperMenu::none; }
        } else if (looper_menu_ == LooperMenu::replace) {
            if (input == controls_.confirm) { ok = platform_.looper_storage_ready() && files.load_loop(looper_load_file_); if (ok) looper_menu_ = LooperMenu::none; }
        } else if (looper_menu_ == LooperMenu::files) {
            if (input == controls_.previous || input == controls_.next) ok = files.browse_loop(input == controls_.previous);
            else if (input == controls_.confirm && !files.browsing() && files.loop_filename()[0]) {
                if (loop.unsaved()) { std::strcpy(looper_load_file_,files.loop_filename()); looper_menu_ = LooperMenu::replace; }
                else { ok = platform_.looper_storage_ready() && files.load_loop(files.loop_filename()); if (ok) looper_menu_ = LooperMenu::none; }
            }
        } else if (looper_menu_ == LooperMenu::more) {
            if (input == controls_.previous || input == controls_.next) looper_choice_ = (looper_choice_ + (input == controls_.previous ? 3 : 1)) % 4;
            else if (input == controls_.confirm) {
                if (looper_choice_ == 1) { ok = platform_.looper_storage_ready() && files.browse_loop(false,true); if (ok) looper_menu_ = LooperMenu::files; }
                else {
                    ok = looper_action(looper_choice_ == 0 ? 4 : looper_choice_ + 4,"");
                    if (ok) looper_menu_ = LooperMenu::none;
                }
            }
        } else if (input == controls_.previous) ok = looper_action(0,"");
        else if (input == controls_.next) ok = looper_action(1,"");
        else if (input == controls_.confirm) ok = looper_action(2,"");
        else if (input == controls_.previous_section) looper_menu_ = LooperMenu::clear;
        else if (input == controls_.next_section) { looper_choice_ = 0; looper_menu_ = LooperMenu::more; }
        if (!ok) error("MIDI looper command rejected");
        refresh_looper_prompt(); pages_.revisit(); return true;
    }
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
        if (pages_.arpeggiator) pages_.arpeggiator->configure(document.valid() ? document.arpeggiator_settings() : nullptr);
        pages_.outputs(platform_.midi_outputs());
        if (!configure_ports()) { error("Port memory unavailable"); return; }
        pages_.layout(document.valid() ? document.page_layout() : cntrl::PageLayout{});
        if (looper_files_) {
            looper_files_->cancel_browse(); looper_menu_ = LooperMenu::none;
            if (looper_files_->looper.clear()) {
                if (const auto *p = document.valid() ? document.looper_settings() : nullptr) {
                    midi::LooperSettings settings{u16(p + 8),u16(p + 6),u16(p + 10),u16(p + 12)};
                    if (!looper_files_->looper.configure(settings,*looper_sink_)) error("MIDI looper memory unavailable");
                } else looper_files_->looper.disable();
            }
        }
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
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_enter_page)
#endif
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
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_leave_page)
#endif
    void leave_page() {
        if (pages_.page() == Pages::Page::midi_looper && looper_files_) { looper_files_->cancel_browse(); looper_menu_ = LooperMenu::none; }
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
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_stop_clock)
#endif
    bool stop_clock() {
        pages_.generator.stop();
        pages_.generator.poll(platform_.microseconds(), platform_.clock_available(), platform_);
        if (looper_files_) {
            looper_files_->looper.stop();
            looper_files_->looper.poll(pages_.beat_position(platform_.microseconds()),pages_.clock_ready(),platform_.clock_available(),*looper_sink_);
            if (looper_sink_->loop_cleaning() || looper_files_->storage_busy()) return false;
        }
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
        if (!configure_ports()) { error("Port memory unavailable"); return; }
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
        if (pages_.page() == Pages::Page::midi_looper && looper_files_) refresh_looper_prompt();
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
    enum class LooperMenu { none, more, files, clear, replace };
    LooperMenu looper_menu_ = LooperMenu::none;
    unsigned looper_choice_ = 0;
    char looper_load_file_[96] = {};
    midi::MidiFiles *looper_files_ = nullptr;
    midi::Sink *looper_sink_ = nullptr;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_prompt)
#endif
    void refresh_looper_prompt() {
        const char *choices[] = {"Save: Confirm","Load: Confirm","Reverse phrase: Confirm","Toggle direction: Confirm"};
        const char *text = looper_menu_ == LooperMenu::more ? choices[looper_choice_]
            : looper_menu_ == LooperMenu::clear ? "Confirm clear?"
            : looper_menu_ == LooperMenu::replace ? "Replace unsaved loop?"
            : looper_menu_ == LooperMenu::files ? (looper_files_->browsing() ? "Scanning..." : looper_files_->loop_filename()[0] ? looper_files_->loop_filename() : "No MIDI files")
            : looper_files_->storage_busy() ? "Storage busy" : "";
        std::snprintf(pages_.looper_prompt,sizeof(pages_.looper_prompt),"%s",text);
    }
    void focus_controller() {
        if (pages_.page() != Pages::Page::patch_switcher) return;
        application_.controller().cancel_gesture();
        application_.visible_index = application_.layout.controller_slot(pages_.index());
    }
    struct Port { unsigned index; const char *label; };
    CheckedArray<Port> ports_;
    CheckedArray<unsigned> expression_ports_;
    bool configure_ports() {
        const auto outputs = platform_.midi_outputs();
        unsigned count = 0, expressions = 0;
        auto first = [&](const midi::OutputConnection &endpoint) {
            if (endpoint.port == midi::no_port) return false;
            for (const auto &previous : outputs) {
                if (&previous == &endpoint) break;
                if (previous.port == endpoint.port) return false;
            }
            return true;
        };
        for (const auto &endpoint : outputs) if (first(endpoint)) {
            ++count;
            expressions += platform_.supports_trs_mode(endpoint.port, midi::TrsMode::expression);
        }
        CheckedArray<Port> ports;
        CheckedArray<unsigned> expression_ports;
        if (!ports.reserve(count) || !expression_ports.reserve(expressions)) return false;
        for (const auto &endpoint : outputs) {
            if (endpoint.port == midi::no_port) continue;
            if (!first(endpoint)) continue;
            (void)ports.append({endpoint.port, endpoint.label});
            if (platform_.supports_trs_mode(endpoint.port, midi::TrsMode::expression)) (void)expression_ports.append(endpoint.port);
        }
        ports_.swap(ports); expression_ports_.swap(expression_ports);
        select_trs_jack();
        return true;
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
