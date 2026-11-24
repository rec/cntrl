#pragma once
#include "messages.h"
#include "application.h"
#include "pages.h"
#include "renderer.h"
#include "pixel_throb.h"
#include "memon_score.h"
#include "prompts.h"
#include <cntrl/midi_files.h>
#include <cntrl/buttons.h>
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
          application_(application), documents_(documents), display_(display), renderer_(renderer), controls_(controls) {
        pages_.controls(controls); pages_.monitor = &platform_.midi_monitor(); pages_.languages = &platform_;
        renderer_.artwork_source(application_);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_prepare_display)
#endif
    bool prepare_display(const Document &document) {
        pending_animations_ = !document.valid() || document.use_animations();
        return renderer_.prepare(document.valid() ? document.frame_buffers() : 0, pending_animations_);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_finish_display)
#endif
    void finish_display(bool commit) {
        renderer_.finish(commit);
        if (commit) platform_.animation_session().allow(pending_animations_);
    }
    void looper_files(midi::MidiFiles &files, midi::Sink &sink) { looper_files_ = &files; looper_sink_ = &sink; pages_.looper = &files.looper; }
    void arpeggiator(midi::MidiArpeggiator &engine) { pages_.arpeggiator = &engine; }
    void memon_scores(files::FileSystem &filesystem) { memon_score_.attach(filesystem); }
    void invalidate_language() { renderer_.invalidate_language(); }
    void language_changed() {
        refresh_looper_prompt(); pages_.revisit();
        for (auto &view : application_.views) {
            view.dirty = true; view.refreshed = 0;
            if (view.page == Pages::Page::midi_looper)
                utf8::copy(view.looper_prompt, sizeof(view.looper_prompt), pages_.looper_prompt);
        }
    }
#ifdef CNTRL_EMULATOR
    void font_preview(const char *label,unsigned size) {
        pages_.font_preview(label,size);
        for (auto &view : application_.views) view.dirty = true;
        renderer_.invalidate();
    }
#endif
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
        if (pages_.page() == Pages::Page::memon) {
            if (!pressed) return true;
            const char *label = platform_.input_label(input);
            if (!label || !label[0] || label[1]) return true;
            const char key = char(label[0] >= 'a' && label[0] <= 'z' ? label[0] - 'a' + 'A' : label[0]);
            const uint32_t now = platform_.milliseconds();
            if (key == 'F') browse_page(false);
            else if (key == 'C') memon_.start(now, platform_.microseconds(), platform_);
            else {
                constexpr char keys[] = "DEAB";
                if (const char *tile = std::strchr(keys, key))
                    memon_.press(unsigned(tile - keys), now, platform_);
            }
            pages_.revisit();
            return true;
        }
        if (pages_.page() == Pages::Page::doom) {
            if (input != controls_.confirm) return false;
            if (pressed && (documents_.busy() || documents_.dirty() ||
                platform_.song_transfer_busy() || !platform_.launch_doom()))
                error("Doom could not start");
            return true;
        }
        if (pages_.page() == Pages::Page::calibrate) {
            if (input != controls_.confirm) return false;
            auto &pedals = platform_.expression_controls();
            if (!pressed || !pedals.count()) return true;
            if (!documents_.busy() && !platform_.configuration_locked() && !pedals.calibrate())
                error(i18n::text(i18n::Message::save_failed));
            pages_.revisit();
            return true;
        }
        if ((pages_.page() == Pages::Page::setups || pages_.page() == Pages::Page::songs || pages_.page() == Pages::Page::switches || pages_.page() == Pages::Page::settings) && input == controls_.confirm) {
            if (pressed) application_.open_tool(unsigned(pages_.page()), platform_, controls_.settings_shortcut);
            return true;
        }
        if (pages_.page() == Pages::Page::language) {
            if (input != controls_.next && input != controls_.next_section && input != controls_.confirm && input != controls_.back) return false;
            if (!pressed) return true;
            if (input == controls_.back) {
                if (!application_.song_navigation_active() || application_.layout.navigation())
                    browse_page(controls_.reverse_page < 64 && (application_.held & (uint64_t(1) << controls_.reverse_page)));
                return true;
            }
            const unsigned count = platform_.language_count();
            if (count != pages_.language.count()) pages_.language.reset(count, pages_.language.selected());
            if (count) {
                if (input == controls_.confirm) platform_.request_language(platform_.language_tag(pages_.language.selected()));
                else pages_.language.move(input == controls_.next_section, platform_.milliseconds());
            }
            pages_.revisit(); return true;
        }
        if (pages_.page() == Pages::Page::pong) {
            if (!pages_.pong.engaged && input != controls_.next && input != controls_.next_section) return false;
            if (input == controls_.next_section) { if (pressed) browse_page(false); return true; }
            if (input == controls_.previous_section) pages_.pong.button(Pong::Button::left_up, pressed);
            else if (input == controls_.previous) pages_.pong.button(Pong::Button::left_down, pressed);
            else if (input == controls_.back) pages_.pong.button(Pong::Button::right_up, pressed);
            else if (input == controls_.confirm) pages_.pong.button(Pong::Button::right_down, pressed);
            else if (input == controls_.next) pages_.pong.button(Pong::Button::serve, pressed);
            pages_.revisit(); return true;
        }
        if (auto *bank = application_.focused_bank()) {
            bank->button(input, pressed, platform_.milliseconds()); return true;
        }
        if (pages_.page() == Pages::Page::arpeggiator && pages_.arpeggiator) {
            if (input != controls_.next && (input != controls_.previous || !pages_.arpeggiator->mono())) return false;
            if (!pressed) return true;
            auto &arp = *pages_.arpeggiator;
            uint32_t now = platform_.microseconds();
            if (!arp.mono() && arp.state() == midi::MidiArpeggiator::State::stopped && !pages_.external_clock() && !pages_.generator.running())
                pages_.generator.start(now,0,false);
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
        if (!ok) error(i18n::text(i18n::Message::looper_command_rejected));
        refresh_looper_prompt(); pages_.revisit(); return true;
    }
    bool monitor_button(unsigned input, bool pressed) {
        if (pages_.page() != Pages::Page::midi_monitor) return false;
        if (!pressed) return true;
        if (input == controls_.back) {
            browse_page(false); return true;
        }
        if (!platform_.midi_monitor().active()) return true;
        if (input == controls_.confirm) platform_.midi_monitor().freeze();
        else if (input == controls_.previous || input == controls_.next) platform_.midi_monitor().browse(input == controls_.previous);
        pages_.revisit(); return true;
    }
    KeyboardDelivery keyboard_action(const KeyboardKeys &keys, bool pressed) {
        poll_keyboard();
        return keyboard_.execute(keys, pressed, platform_);
    }
    void poll_keyboard() {
        keyboard_.poll(platform_.keyboard_connected(), platform_);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_external_keyboard)
#endif
    void external_keyboard(bool connected) {
        if (external_keyboard_ == connected) return;
        external_keyboard_ = connected;
        if (pages_.page() == Pages::Page::status) pages_.revisit();
        for (auto &view : application_.views)
            if (view.page == Pages::Page::status) view.dirty = true;
    }
    bool external_keyboard_connected() const { return external_keyboard_; }
    bool release_keyboard() {
        keyboard_.clear(); poll_keyboard();
        return keyboard_.released();
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_show)
#endif
    void show(unsigned pane, const cntrl::Pane &content) {
        if (application_.utility_visible()) return;
        if (content.page < application_.layout.pages()) {
            const unsigned r = application_.layout.page_region(content.page);
            const auto &region = application_.allocation.region(r);
            if (application_.feature_open(platform_) || region.selected != content.page || pane >= region.count) return;
            Pane view = content; view.focused = r == application_.allocation.focus();
            renderer_.show(region.first + pane, view); return;
        }
        if (content.performance) {
            if (!application_.visible() || application_.feature_open(platform_)) return;
            const int destination = application_.allocation.controller_display(application_.rendering(),pane);
            if (destination < 0) return;
            Pane view = content;
            unsigned region = application_.layout.page_region(application_.layout.controller_page(application_.rendering()));
            view.focused = region == application_.allocation.focus();
            if (application_.rendering() == application_.visible_index && unsigned(destination) == application_.allocation.region(region).first) { pending_patch_ = content.pending_patch; apply_brightness(platform_.milliseconds()); }
            renderer_.show(unsigned(destination),view);
            return;
        }
        if (pane == 0) {
            cancel_focus(); save_view(); management_visible_ = true;
            pending_patch_ = content.pending_patch; apply_brightness(platform_.milliseconds());
        }
        if (pane == 0 && application_.song_boundary() && !content.lines[4].text[0]
            && application_.settings_chord_available(platform_, controls_.settings_mask())) {
            Pane view = content;
            settings_hint(view.lines[4], view.graphics.background);
            renderer_.show(pane, view);
        } else renderer_.show(pane, content);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_configure_pages)
#endif
    void configure_pages(const cntrl::Document &document) {
        if (!document.valid()) {
            renderer_.prepare(0, true); renderer_.finish(true);
            platform_.animation_session().allow(true);
        }
        expression_bank_ = nullptr;
        keyboard_.clear(); platform_.midi_monitor().close(); monitor_unavailable_ = false;
        management_visible_ = false;
        if (const auto *settings = document.valid() ? document.memon_settings() : nullptr) {
            memon_.configure(MemonSettings::read(settings));
            memon_artwork_.configure(memon_.settings());
        }
        if (pages_.arpeggiator) pages_.arpeggiator->configure(document.valid() ? document.arpeggiator_settings() : nullptr);
        pages_.outputs(platform_.midi_outputs());
        pages_.layout(document.valid() ? document.page_layout() : cntrl::PageLayout{});
        if (looper_files_) {
            looper_sink_->configure_loop_controls(document.valid() ? document.loop_stop_settings() : nullptr, document.valid() && document.loop_stop_reset_controls());
            looper_files_->cancel_browse(); looper_menu_ = LooperMenu::none;
            if (looper_files_->looper.clear()) {
                if (const auto *p = document.valid() ? document.looper_settings() : nullptr) {
                    midi::LooperSettings settings{u16(p + 8),u16(p + 6),u16(p + 10),u16(p + 12)};
                    if (!looper_files_->looper.configure(settings,*looper_sink_)) error(i18n::text(i18n::Message::looper_memory_unavailable));
                } else looper_files_->looper.disable();
            }
        }
        application_.configure(document, platform_);
        for (unsigned i = 0; i < application_.views.size(); ++i) { application_.views[i] = {}; application_.views[i].index = i; application_.views[i].page = Pages::Page(document.page_layout().page(i)[0]); }
        for (unsigned r = 0; r < application_.layout.regions(); ++r) {
            load_view(application_.allocation.region(r).selected); enter_page(); save_view();
        }
        if (application_.layout.regions()) load_view(application_.allocation.selected());
        focus_controller(); application_.refresh(platform_);
    }
    bool refresh_pages() { application_.refresh(platform_); return true; }
    void blank_performance() {
        cntrl::Pane empty;
        empty.graphics.background = 0;
        for (unsigned i = 0; i < display_.count(); ++i) renderer_.show(i, empty);
        pending_patch_ = -1;
        pages_.revisit();
        for (auto &view : application_.views) view.dirty = true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_enter_page)
#endif
    void enter_page() {
        if (pages_.page() == Pages::Page::language) {
            const unsigned count = platform_.language_count();
            unsigned selected = 0;
            for (unsigned i = 0; i < count; ++i)
                if (!std::strcmp(platform_.current_language(), platform_.language_tag(i))) { selected = i; break; }
            pages_.language.reset(count, selected);
        }
        if (pages_.page() == Pages::Page::pong) pages_.pong.clock(platform_.milliseconds());
        if (pages_.page() == Pages::Page::memon) {
            memon_save_attempted_ = 0;
            if (!memon_score_.load()) error("Memon high score could not be loaded");
        }
        expression_bank_ = nullptr;
        if (auto *bank = application_.focused_bank()) {
            if (!bank->activate(uint32_t(application_.held))) error(i18n::text(i18n::Message::banked_memory_unavailable));
            else bank->refresh();
        }
        if (pages_.page() == Pages::Page::midi_monitor) {
            if (!platform_.midi_monitor().active()) {
                monitor_unavailable_ = !platform_.midi_monitor().open(platform_.microseconds());
                if (monitor_unavailable_) error(i18n::text(i18n::Message::monitor_memory_unavailable));
            }
        }
#if CNTRL_CABLE_TEST
        if (pages_.page() == Pages::Page::cable && pages_.index() == application_.allocation.selected()) platform_.cable_test.enter();
#endif
#if CNTRL_PANIC
        if (pages_.page() == Pages::Page::panic && pages_.index() == application_.allocation.selected()) platform_.panic.enter(platform_.runtime.brightness);
#endif
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_leave_page)
#endif
    void leave_page() {
        if (pages_.page() == Pages::Page::calibrate) platform_.expression_controls().cancel();
        if (pages_.page() == Pages::Page::pong) pages_.pong.leave();
        if (pages_.page() == Pages::Page::memon) finish_memon();
        if (pages_.page() == Pages::Page::midi_looper && looper_files_) { looper_files_->cancel_browse(); looper_menu_ = LooperMenu::none; }
        if (pages_.page() == Pages::Page::midi_monitor && !other_selected(Pages::Page::midi_monitor)) { platform_.midi_monitor().close(); monitor_unavailable_ = false; }
#if CNTRL_CABLE_TEST
        if (pages_.page() == Pages::Page::cable && pages_.index() == application_.allocation.selected()) platform_.leave_cable_test();
#endif
#if CNTRL_PANIC
        if (pages_.page() == Pages::Page::panic && pages_.index() == application_.allocation.selected()) platform_.leave_panic();
#endif
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_stop_clock)
#endif
    bool stop_clock() {
        if (pages_.clock_sending()) pages_.toggle_clock();
        pages_.generator.stop();
        pages_.generator.poll(platform_.microseconds(), platform_.clock_available(), platform_);
        if (looper_files_) {
            looper_files_->looper.stop();
            looper_files_->looper.poll(pages_.beat_position(platform_.microseconds()),pages_.clock_ready(),platform_.clock_available(),*looper_sink_);
            if (looper_sink_->loop_cleaning() || looper_sink_->loop_control_reset_pending()
                || looper_files_->storage_busy()) return false;
        }
        return !pages_.generator.stopping();
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_stop_setup)
#endif
    void stop_setup() {
        application_.stop_setup(); leave_page();
        release_keyboard();
        pages_.stop_setup();
        platform_.animation_session().stop();
        platform_.expression_controls().configure(nullptr, {});
        expression_bank_ = nullptr; expression_revision_ = 0;
        looper_menu_ = LooperMenu::none; pages_.looper_prompt[0] = 0;
        platform_.midi_monitor().close();
        brightness_feedback_ = false; pending_patch_ = -1;
        throb_.configure(0, 0, 0, 0);
        if (!platform_.relays.reset()) platform_.error(i18n::text(i18n::Message::relay_reset_failed));
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_configure)
#endif
    void configure_page_settings(const cntrl::PageSettings &settings) {
        pages_.outputs(platform_.midi_outputs());
        leave_page();
        platform_.expression_controls().configure(settings.expression, platform_.midi_outputs());
        pages_.configure(settings);
        platform_.midi_bridge().clock_owner(pages_.clock_owner() | platform_.midi_file_clock_outputs());
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_browse)
#endif
    void browse_page(bool reverse) {
        if (!documents_.document().valid()) return;
        if (application_.editing_tool) return;
        if (application_.song_navigation_active() || application_.utility_visible()) {
            const auto &layout = application_.layout;
            const unsigned count = layout.pages();
            if (!count) return;
            unsigned cursor = count;
            for (unsigned i = 0; i < count; ++i) {
                const unsigned page = layout.ordered_page(i);
                if (page == application_.utility_page) { cursor = i; break; }
                if (application_.utility_page >= count && layout.page(page)[0] == 10 && cursor == count) cursor = i;
            }
            for (unsigned i = 0; i < count; ++i) {
                cursor = cursor == count ? (reverse ? count - 1 : 0) : (cursor + (reverse ? count - 1 : 1)) % count;
                const unsigned index = layout.ordered_page(cursor);
                const auto type = layout.page(index)[0];
                if (type == PageLayout::patch_chooser || type == PageLayout::banked) continue;
#if !CNTRL_CABLE_TEST
                if (type == unsigned(Pages::Page::cable)) continue;
#endif
#if !CNTRL_PANIC
                if (type == unsigned(Pages::Page::panic)) continue;
#endif
                cancel_focus(); leave_page(); save_view();
                application_.utility_page = index;
                if (type == 10) {
                    load_view(application_.allocation.selected()); pages_.revisit();
                    application_.refresh(platform_); platform_.refresh_switches();
                    application_.song_ui.refresh();
                    return;
                }
                load_view(index); enter_page(); pages_.revisit(); save_view();
                platform_.blank_performance();
                return;
            }
            return;
        }
        auto previous = pages_.save_view();
        pages_.browse(reverse);
        unsigned selected = pages_.index();
        pages_.restore_view(previous);
        const auto &span = application_.allocation.region(application_.allocation.focus());
        if (span.count) renderer_.transition(reverse,documents_.document().scroll_ms(),int(span.first),0,false,true,span.count);
        select_page(selected);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_select_page)
#endif
    void select_page(unsigned index) {
        unsigned region = application_.layout.page_region(index);
        if (region == application_.layout.regions()) return;
        cancel_focus(); save_view();
        load_view(application_.allocation.region(region).selected); leave_page(); save_view();
        application_.allocation.select(index); load_view(index);
        focus_controller(); enter_page(); save_view();
        application_.refresh(platform_);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_focus_region)
#endif
    void focus_region(unsigned region) {
        if (region >= application_.layout.regions()) return;
        cancel_focus(); save_view(); application_.allocation.focus(region);
        load_view(application_.allocation.selected()); focus_controller();
#if CNTRL_CABLE_TEST
        if (pages_.page() == Pages::Page::cable) platform_.cable_test.enter();
#endif
#if CNTRL_PANIC
        if (pages_.page() == Pages::Page::panic) platform_.panic.enter(platform_.runtime.brightness);
#endif
        application_.controls_locked = true;
        application_.refresh(platform_);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_tap)
#endif
    bool time_button(unsigned input, uint8_t events, uint32_t now) {
        if (pages_.page() == Pages::Page::status && input == controls_.confirm) {
            if (!pages_.showing_contact() || !external_keyboard_ || !documents_.global_preferences()) {
                if (events & press_event) { contact_tap_ = false; pages_.toggle_contact(); }
            } else if (events & press_event) {
                // A press that opened Contact belongs to Status, even if the
                // next press is within the switch's global double-press window.
                const bool edit = contact_tap_ && contact_released_
                    && uint32_t(now - contact_released_at_) <= ButtonState::double_press_ms;
                contact_tap_ = !edit;
                contact_released_ = false;
                if (edit) {
                    application_.editing_tool = PageLayout::setups;
                    if (application_.setups.enter_contact()) application_.controls_locked = true;
                    else { application_.editing_tool = 0; error(i18n::text(i18n::Message::working)); }
                }
            }
            if (contact_tap_ && (events & release_event)) {
                contact_released_ = true;
                contact_released_at_ = now;
            }
            return true;
        }
        if (pages_.page() != Pages::Page::time) return false;
        if (input == controls_.confirm) {
            if (events & press_event) pages_.tap(now);
            if (events & hold_event) pages_.clear_tap();
        } else if (input == controls_.next) {
            if (events & press_event) pages_.toggle_clock();
        } else if (input == controls_.next_section) {
            if (events & press_event) pages_.cycle_clock();
        } else return false;
        return true;
    }
    void tap_tempo(uint32_t now) {
        if (!documents_.document().valid()) return;
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
            platform_.panic.button(true);
            start_panic();
            return;
        }
#endif
        pages_.tap(now);
    }
    void set_target_pulse(unsigned period, uint8_t low, uint8_t high, unsigned delay) {
        throb_.configure(period, low, high, delay);
    }
    void transition(bool undo, std::optional<unsigned> duration_ms, int pane, bool vertical = false, bool panorama = true) {
        if (!application_.visible() || application_.feature_open(platform_)) return;
        unsigned region_index = application_.layout.page_region(application_.layout.controller_page(application_.rendering()));
        if (region_index == application_.layout.regions()) return;
        const auto &region = application_.allocation.region(region_index);
        if (!region.count) return;
        if (pane >= 0) {
            pane = application_.allocation.controller_display(application_.rendering(),unsigned(pane));
            if (pane < 0) return;
        } else if (region.count == 1) pane = int(region.first);
        if (pane >= 0) renderer_.transition(undo,duration_ms,pane,0,vertical,panorama,1);
        else renderer_.transition(undo,duration_ms,int(region.first),0,vertical,panorama,region.count);
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
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_display)
#endif
    void display(uint32_t now, bool transfer_busy) {
        if (contact_tap_ && contact_released_ && uint32_t(now - contact_released_at_) > ButtonState::double_press_ms) {
            contact_tap_ = false;
            if (pages_.showing_contact()) pages_.toggle_contact();
        }
        memon_.tick(platform_.milliseconds(), platform_);
        if (application_.stopping_setup()) return;
#if CNTRL_PANIC
        if (pages_.page() == Pages::Page::panic && platform_.panic.active() && !platform_.panic_owns_midi) start_panic();
#endif
        if (auto *bank = application_.focused_bank()) {
            if (expression_bank_ != bank || expression_revision_ != bank->revision()) {
                ExpressionAssignment assignments[4];
                for (unsigned i = 0; i < 4; ++i) assignments[i] = bank->expression(i);
                platform_.expression_controls().configure(assignments, platform_.midi_outputs());
                expression_bank_ = bank; expression_revision_ = bank->revision();
            }
        } else expression_bank_ = nullptr;
        const auto &message = platform_.text_messages();
        if (message.active(now)) platform_.animation_session().stop();
        renderer_.message(message, now);
        if (application_.pending_page < application_.layout.pages()) {
            select_page(application_.pending_page); application_.pending_page = PageLayout::max_pages;
        }
        if (application_.editing_tool && !application_.tool_editor_open(platform_)) {
            application_.finish_tool();
            management_visible_ = false;
            load_view(application_.utility_visible() ? application_.utility_page : application_.allocation.selected());
            pages_.revisit();
            application_.refresh(platform_); platform_.refresh_switches(); application_.song_ui.refresh();
        }
        if (management_visible_ && !application_.utility_visible() && !application_.feature_open(platform_)) {
            management_visible_ = false; enter_page(); pages_.revisit(); save_view();
        }
        if (platform_.display_priority_due()) return;
        if (!transfer_busy && !documents_.busy() && pages_.page() == Pages::Page::memon)
            save_memon_score();
#if CNTRL_CABLE_TEST
        transfer_busy = transfer_busy || platform_.cable_test.enabled();
#endif
#if CNTRL_PANIC
        transfer_busy = transfer_busy || platform_.panic_owns_midi;
#endif
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
        save_view();
        if (const auto *pedals = application_.setups.calibration(); pedals && !splash_visible_ && display_.count()
            && uint32_t(now - calibration_view_at_) >= 100) {
            calibration_view_at_ = now;
            Prompt prompt(platform_);
            if (!pedals->calibrating()) prompt.add(ControlRole::confirm, i18n::text(i18n::Message::calibrate));
            else if (pedals->captured()) prompt.add(ControlRole::confirm, "Review");
            prompt.add(ControlRole::back, i18n::text(i18n::Message::back), ", ");
            const unsigned screen = display_.count() < 3 ? 0 : display_.count() / 2;
            renderer_.show(screen, pages_.calibration_view(now, *pedals,
                [&](unsigned jack) { return platform_.expression_available(jack); }, display_.viewport(screen), prompt.text()));
        }
        if (!splash_visible_ && !application_.feature_open(platform_) && documents_.document().valid()) {
          for (unsigned r = 0; r < (application_.utility_visible() ? 1u : application_.layout.regions()); ++r) {
            const auto region = application_.utility_visible() ? PageAllocation::Region{application_.utility_page, 0, std::min(display_.count(), PageLayout::maximum_displays(application_.layout.page(application_.utility_page)[0], display_.count()))} : application_.allocation.region(r);
            if (!region.count || application_.layout.page(region.selected)[0] == PageLayout::patch_chooser || application_.layout.page(region.selected)[0] == PageLayout::banked) continue;
            load_view(region.selected);
            if (!pages_.due(now)) { save_view(); continue; }
            if (pages_.page() == Pages::Page::midi_looper && looper_files_) refresh_looper_prompt();
            if (pages_.page() == Pages::Page::midi_monitor && !platform_.midi_monitor().active() && !monitor_unavailable_) enter_page();
            // Prepare the incoming card during the renderer's bounded transition preparation.
            PerformanceStatus status = pages_.page() == Pages::Page::status ? platform_.status() : PerformanceStatus{};
            for (unsigned i = 0; i < region.count; ++i) {
                const unsigned screen = region.first + i;
                pages_.viewport(display_.viewport(screen));
                if (pages_.page() == Pages::Page::calibrate) {
                    auto content = pages_.calibration_view(now, platform_.expression_controls(),
                        [&](unsigned jack) { return platform_.expression_available(jack); });
                    content.focused = r == application_.allocation.focus();
                    renderer_.show(screen, content);
                    continue;
                }
                if (pages_.page() == Pages::Page::memon) {
                    renderer_.show(screen, pages_.memon_view(memon_, memon_artwork_, memon_score_.high(), i, now));
                    continue;
                }
                cntrl::Pane content = pages_.view(now, platform_.microseconds(), status
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
                , i, region.count, documents_.name_and_contact(),
                !application_.song_navigation_active() || application_.layout.navigation(),
                documents_.document().contact_style(), documents_.document().contact_artwork(&application_),
                external_keyboard_ && documents_.global_preferences()
            );
                if (pages_.page() != Pages::Page::midi_monitor && pages_.page() != Pages::Page::pong && pages_.page() != Pages::Page::language && pages_.page() != Pages::Page::time) content = Pages::display_view(content,i,region.count);
                content.focused = r == application_.allocation.focus() && pages_.page() != Pages::Page::pong; renderer_.show(screen,content);
            }
            save_view();
          }
        }
        if (application_.layout.regions()) load_view(application_.utility_visible() ? application_.utility_page : application_.allocation.selected());
        if (!splash_visible_) draw();
    }
    void apply_brightness(uint32_t now) {
        const int patch_screen = application_.allocation.controller_display(application_.visible_index, 0);
        unsigned screen = patch_screen >= 0 ? unsigned(patch_screen) : 0;
        renderer_.target_brightness(throb_.brightness(
            patch_screen >= 0 && !splash_visible_ && !brightness_feedback_ ? pending_patch_ : -1, now, 128, application_.held),screen);
        uint8_t brightness = brightness_feedback_ ? feedback_brightness_ : platform_.animation_session().brightness(platform_.runtime.brightness, now);
        if (brightness != applied_brightness_) {
            platform_.display_brightness(brightness); applied_brightness_ = brightness;
        }
    }
    int hardware_brightness() const { return applied_brightness_; }
    bool showing_splash() const { return splash_visible_; }
    Pages pages_;
    Keyboard keyboard_;
private:
#if CNTRL_PANIC
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_start_panic)
#endif
    void start_panic() {
        release_keyboard();
        if (looper_files_) looper_files_->player.cancel();
        if (!platform_.panic_owns_midi && (application_.patch_pending() || !platform_.output_idle()
            || documents_.busy() || (looper_files_ && looper_files_->player.cleaning()) || !stop_clock())) return;
        application_.controller().cancel_gesture();
        if (!platform_.panic_owns_midi) platform_.midi_bridge().configure(false);
        platform_.panic_owns_midi = true;
    }
#endif
    BankedPage *expression_bank_ = nullptr;
    unsigned expression_revision_ = 0;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_settings_hint)
#endif
    void settings_hint(TextLine &line, uint16_t background) const {
        line.graphics.font = localized_face;
        line.graphics.size = line.graphics.line_height = 16;
        line.graphics.background = background;
        line.graphics.foreground = Raster::luminance(background) < 1790 ? 0xffff : 0;
        line.centered = true;
        std::snprintf(line.text, sizeof(line.text), "%s+%s %s",
            controls_.bindings[controls_.confirm].label, controls_.bindings[controls_.back].label,
            i18n::text(i18n::Message::settings));
    }
    void save_view() { if (pages_.index() < application_.views.size()) application_.views[pages_.index()] = pages_.save_view(); }
    void load_view(unsigned page) { if (page < application_.views.size()) pages_.restore_view(application_.views[page]); }
    bool other_selected(Pages::Page type) const {
        if (!documents_.document().valid() || application_.layout.records != documents_.document().page_layout().records) return false;
        for (unsigned r = 0; r < application_.layout.regions(); ++r) {
            unsigned selected = application_.allocation.region(r).selected;
            if (selected != pages_.index() && application_.layout.page(selected)[0] == unsigned(type)) return true;
        }
        return false;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_save_memon_score)
#endif
    void save_memon_score() {
        memon_score_.observe(memon_.score());
        if (memon_score_.high() <= memon_save_attempted_) return;
        memon_save_attempted_ = memon_score_.high();
        if (!memon_score_.save()) error("Memon high score could not be saved");
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_host_finish_memon)
#endif
    void finish_memon() {
        memon_.leave(platform_.milliseconds(), platform_);
        if (!documents_.busy()) save_memon_score();
    }
    void cancel_focus() {
        contact_tap_ = false;
        if (pages_.page() == Pages::Page::calibrate) platform_.expression_controls().cancel();
        if (pages_.page() == Pages::Page::pong) pages_.pong.leave();
        if (pages_.page() == Pages::Page::memon) finish_memon();
        application_.controller().cancel_gesture();
        if (looper_files_ && pages_.page() == Pages::Page::midi_looper) { looper_files_->cancel_browse(); looper_menu_ = LooperMenu::none; pages_.looper_prompt[0] = 0; }
#if CNTRL_CABLE_TEST
        if (pages_.page() == Pages::Page::cable) platform_.leave_cable_test();
#endif
#if CNTRL_PANIC
        if (pages_.page() == Pages::Page::panic) platform_.leave_panic();
#endif
        auto view = pages_.save_view(); view.held = false; pages_.restore_view(view);
    }
    enum class LooperMenu { none, more, files, clear, replace };
    Memon memon_;
    MemonArtwork memon_artwork_;
    MemonScore memon_score_;
    unsigned memon_save_attempted_ = 0;
    LooperMenu looper_menu_ = LooperMenu::none;
    unsigned looper_choice_ = 0;
    uint32_t contact_released_at_ = 0;
    bool external_keyboard_ = false, contact_tap_ = false, contact_released_ = false;
    char looper_load_file_[96] = {};
    midi::MidiFiles *looper_files_ = nullptr;
    midi::Sink *looper_sink_ = nullptr;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_prompt)
#endif
    void refresh_looper_prompt() {
        const char *choices[] = {i18n::text(i18n::Message::save_confirm),i18n::text(i18n::Message::load_confirm),i18n::text(i18n::Message::reverse_phrase_confirm),i18n::text(i18n::Message::toggle_direction_confirm)};
        const char *text = looper_menu_ == LooperMenu::more ? choices[looper_choice_]
            : looper_menu_ == LooperMenu::clear ? i18n::text(i18n::Message::confirm_clear)
            : looper_menu_ == LooperMenu::replace ? i18n::text(i18n::Message::replace_unsaved_loop)
            : looper_menu_ == LooperMenu::files ? (looper_files_->browsing() ? i18n::text(i18n::Message::scanning) : looper_files_->loop_filename()[0] ? looper_files_->loop_filename() : i18n::text(i18n::Message::no_midi_files))
            : looper_files_->storage_busy() ? i18n::text(i18n::Message::storage_busy) : "";
        std::snprintf(pages_.looper_prompt,sizeof(pages_.looper_prompt),"%s",text);
    }
    void focus_controller() {
        if (pages_.page() != Pages::Page::patch_chooser) return;
        application_.controller().cancel_gesture();
        application_.visible_index = application_.layout.controller_slot(pages_.index());
    }
    Platform &platform_;
    Application &application_;
    Documents &documents_;
    Display &display_;
    Renderer &renderer_;
    Controls controls_;
    PixelThrob throb_;
    bool splash_visible_ = false, management_visible_ = false;
    uint32_t calibration_view_at_ = 0;
    bool pending_animations_ = true;
    int pending_patch_ = -1;
    bool brightness_feedback_ = false;
    bool monitor_unavailable_ = false;
    uint8_t feedback_brightness_ = 255;
    int applied_brightness_ = -1;
};
} // namespace cntrl::ui
