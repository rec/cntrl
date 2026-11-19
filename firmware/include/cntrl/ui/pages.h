#pragma once
#include "messages.h"
#include <cntrl/hardware.h>
#include <cntrl/tempo.h>
#include <cntrl/clock_generator.h>
#include <cntrl/midi_outputs.h>
#include <cntrl/midi_monitor.h>
#include <cntrl/midi_looper.h>
#include <cntrl/midi_arpeggiator.h>
#include <cntrl/cable_test.h>
#include <cntrl/panic.h>
#include "fonts.h"
#include "display.h"
#include "controls.h"
#include "pong.h"
#include "flags.h"
#include "status_snapshot.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <charconv>

namespace cntrl::ui {
class Pages {
public:
    const StatusSnapshot *status_snapshot = nullptr;
    StatusSnapshot capture_status(const PerformanceStatus &performance) const {
        StatusSnapshot result{performance, uptime_, 0};
        result.tap_period_ms = tap_.period(); result.clock_period_us = clock_.period_us(); result.clock_active = clock_.active();
#ifdef CNTRL_GIT_COMMIT
        result.git_commit = CNTRL_GIT_COMMIT;
#endif
        return result;
    }
    Pages(const Fonts &fonts, Viewport viewport) : fonts_(fonts), viewport_(viewport) {}
    void outputs(cntrl::midi::Outputs value) { outputs_ = value; }
    void controls(Controls value) { controls_ = value; }
    void viewport(Viewport value) { viewport_ = value; }
#ifdef CNTRL_EMULATOR
    void font_preview(const char *label,unsigned size) {
        std::snprintf(preview_label_,sizeof(preview_label_),"%s",label);
        preview_size_ = size; revisit();
    }
#endif
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_window_view)
#endif
    static cntrl::Pane window_view(const cntrl::Pane &content, unsigned window, unsigned count) {
        if (count == 1) return content;
        cntrl::Pane result;
        result.graphics = content.graphics;
        result.heading_band = content.heading_band;
        result.lines[0] = content.lines[0];
        unsigned lines = 0;
        for (unsigned i = 1; i < cntrl::max_lines; ++i) if (content.lines[i].text[0]) ++lines;
        const unsigned first = lines * window / count, end = lines * (window + 1) / count;
        unsigned source = 0, destination = 1;
        for (unsigned i = 1; i < cntrl::max_lines; ++i) {
            if (!content.lines[i].text[0]) continue;
            if (source >= first && source < end) {
                result.lines[destination] = content.lines[i];
                result.lines[destination++].y = -1;
            }
            ++source;
        }
        if (window == 0) for (unsigned i = 0; i < 4; ++i) result.circles[i] = content.circles[i];
        return result;
    }
    enum class Page { tap, status, firmware, cable, panic, ports, expression, clock_transport, clock_tempo, keyboard, none, patch_switcher, midi_monitor, midi_looper, arpeggiator, banked, pong, language, confirm_firmware };
    struct ViewState {
        unsigned index = 0, jack = 0, shortcut = 0, language = 0;
        Page page = Page::none;
        midi::TrsMode candidate = midi::TrsMode::off, active_port = midi::TrsMode::off;
        const char *port_message = nullptr, *port_name = i18n::definitions[unsigned(i18n::Message::no_configurable_ports)].text;
        bool port_available = false, shortcut_held = false, dirty = true, flashing = false, held = false;
        uint32_t refreshed = 0;
        char expression[3][40] = {}, looper_prompt[96] = {};
        Pong pong;
    };
    ViewState save_view() const {
        ViewState view;
        view.index = index_; view.page = page_; view.jack = jack; view.candidate = candidate;
        view.language = language;
        view.shortcut = shortcut; view.shortcut_held = shortcut_held; view.active_port = active_port_;
        view.port_message = port_message_; view.port_name = port_name_; view.port_available = port_available_;
        view.dirty = dirty_; view.flashing = flashing_; view.held = held_; view.refreshed = refreshed_;
        std::memcpy(view.expression,expression_lines_,sizeof(expression_lines_));
        std::memcpy(view.looper_prompt,looper_prompt,sizeof(looper_prompt));
        view.pong = pong;
        return view;
    }
    void restore_view(const ViewState &view) {
        index_ = view.index; page_ = view.page; jack = view.jack; candidate = view.candidate;
        language = view.language;
        shortcut = view.shortcut; shortcut_held = view.shortcut_held; active_port_ = view.active_port;
        port_message_ = view.port_message; port_name_ = view.port_name; port_available_ = view.port_available;
        dirty_ = view.dirty; flashing_ = view.flashing; held_ = view.held; refreshed_ = view.refreshed;
        std::memcpy(expression_lines_,view.expression,sizeof(expression_lines_));
        std::memcpy(looper_prompt,view.looper_prompt,sizeof(looper_prompt));
        pong = view.pong;
    }
    midi::MidiArpeggiator *arpeggiator = nullptr;
    Pong pong;
    midi::Monitor *monitor = nullptr;
    Hardware *languages = nullptr;
    unsigned language = 0;
    midi::MidiLooper *looper = nullptr;
    char looper_prompt[96] = {};
    void layout(cntrl::PageLayout layout) {
        layout_ = layout;
        index_ = 0;
        page_ = layout.pages() ? Page(layout.page(0)[0]) : Page::none;
        dirty_ = true;
    }
    unsigned index() const { return index_; }
    void select(unsigned index) {
        index_ = index; page_ = Page(layout_.page(index)[0]); dirty_ = true;
    }
    void configure(const cntrl::PageSettings &settings) {
        generator.stop();
        slot_ = 0;
        settings_ = settings;
        shortcut = 0;
        using R = cntrl::PageSettings;
        external_ = settings[R::clock_external];
        recall_slot();
        tap_.configure(settings[R::tap_intervals], settings[R::minimum_taps], settings[R::minimum_interval], settings[R::reset_gap]);
        clock_.configure(settings[R::clocks_per_quarter], settings[R::clock_beats], uint32_t(settings[R::clock_timeout]) * 1000);
        prepare_expression_layout();
        clock_changed_ = true;
        dirty_ = true;
    }
    void stop_setup() {
        generator.stop(); tap_ = TapTempo(); clock_ = ClockTempo();
        held_ = flashing_ = false; clock_changed_ = dirty_ = true;
    }
    bool browse(bool reverse) {
        if (page_ == Page::confirm_firmware) return !reverse;
        if (!layout_.pages()) return false;
        unsigned region = layout_.page_region(index_);
        if (region == layout_.regions()) return false;
        unsigned count = layout_.region_size(region), position = 0;
        while (position < count && layout_.region_page(region,position) != index_) ++position;
        for (unsigned i = 0; i < count;) {
            ++i;
            position = (position + count + (reverse ? -1 : 1)) % count;
            index_ = layout_.region_page(region,position);
            page_ = Page(layout_.page(index_)[0]);
#if defined(CNTRL_CABLE_TEST) && !CNTRL_CABLE_TEST
            if (page_ == Page::cable) continue;
#endif
#if defined(CNTRL_PANIC) && !CNTRL_PANIC
            if (page_ == Page::panic) continue;
#endif
            break;
        }
        dirty_ = true;
        return false;
    }
    using PortMode = cntrl::midi::TrsMode;
    unsigned jack = 0;
    PortMode candidate = PortMode::off;
    void port_status(PortMode active, const char *message = nullptr) {
        active_port_ = active; port_message_ = i18n::stable(message); dirty_ = true;
    }
    void port_name(const char *label, bool available) { port_name_ = i18n::stable(label); port_available_ = available; dirty_ = true; }
    void next_port_mode() {
        candidate = PortMode((unsigned(candidate) + 1) % 5);
        port_message_ = nullptr; dirty_ = true;
    }
    cntrl::ClockGenerator generator;
    unsigned clock_outputs() const { return settings_[cntrl::PageSettings::clock_outputs]; }
    bool external_clock() const { return external_; }
    unsigned clock_owner() const { return (external_ ? 0 : clock_outputs()) | generator.stopping(); }
    bool clock_page() const { return page_ == Page::clock_transport || page_ == Page::clock_tempo; }
    void clock_connections(unsigned available) {
        available &= clock_outputs();
        if (available != available_outputs_) {
            available_outputs_ = available;
            if (clock_page()) dirty_ = true;
        }
    }
    void clock_button(unsigned button, uint32_t now_us, unsigned available) {
        generator.beat_position(now_us);
        using R = cntrl::PageSettings;
        if (page_ == Page::clock_transport) {
            if (button == 2) generator.stop();
            if (!external_ && button < 2)
                generator.start(now_us, clock_outputs() & available, button == 1);
            if (button == 4) { generator.stop(); external_ = !external_; }
        } else if (page_ == Page::clock_tempo) {
            if (button < 2) {
                unsigned bpm = std::clamp(int(generator.bpm()) + (button ? 1 : -1), 20, 600);
                generator.set_tempo((60000000u + bpm / 2) / bpm);
            } else if (button == 2) {
                if (++slot_ == 4 || !settings_.words[R::slot1 + slot_]) slot_ = 0;
                recall_slot();
            }
        }
        dirty_ = true;
    }
    unsigned shortcut = 0;
    bool shortcut_held = false;
    void keyboard_select(bool reverse) {
        if (settings_.keyboard_count) shortcut = (shortcut + (reverse ? settings_.keyboard_count - 1 : 1)) % settings_.keyboard_count;
        dirty_ = true;
    }
    cntrl::KeyboardShortcut keyboard_shortcut() const {
        return settings_.keyboard_shortcut(shortcut);
    }
    void keyboard_held(bool held) { if (held != shortcut_held) { shortcut_held = held; dirty_ = true; } }
    Page page() const { return page_; }
    void revisit() {
        dirty_ = true;
    }
    void tap(uint32_t now) {
        if (page_ == Page::tap) {
            if (tap_.tap(now)) generator.set_tempo(tap_.period() * 1000);
        }
        else if (page_ == Page::firmware) page_ = Page::confirm_firmware;
        else if (page_ == Page::confirm_firmware) page_ = Page::firmware;
        else return;
        dirty_ = true;
    }
    void clock(unsigned source, uint32_t now_us) { clock_.clock(source, now_us); }
    void clock_message(unsigned source, uint8_t status, uint32_t now_us) {
        if (clock_.transport(source,status,now_us) && external_ && status == 0xfc && arpeggiator) arpeggiator->stop();
    }
    bool clock_ready() const { return !external_ || clock_.available(); }
    unsigned clock_period() const { return external_ ? unsigned(clock_.period_us()) : generator.period_us(); }
    cntrl::midi::BeatPosition beat_position(uint32_t now_us) {
        if (external_) clock_.poll(now_us);
        auto position = external_ ? clock_.beat_position(now_us) : generator.beat_position(now_us);
        if (clock_changed_ || external_ != beat_external_ || position.restart != beat_source_restart_) {
            ++beat_restart_; beat_source_restart_ = position.restart; beat_external_ = external_; clock_changed_ = false;
        }
        position.restart = beat_restart_; return position;
    }
    cntrl::midi::BeatPosition arpeggiator_beat_position(uint32_t now_us) {
        auto position = beat_position(now_us);
        if (external_) {
            auto continuing = clock_.holdover_position(now_us);
            position.value = continuing.value; position.running = continuing.running;
        }
        return position;
    }
    void time(uint32_t now, uint32_t now_us, bool held) {
        held_ = held;
        uptime_ += uint32_t(now - previous_); previous_ = now;
        clock_.poll(now_us); tap_.flash(now, 0);
    }
    void expression_status(const char *title, const char *position, const char *prompt) {
        const char *fields[] = {title, position, prompt};
        for (unsigned i = 0; i < 3; ++i) {
            if (std::strcmp(expression_lines_[i], fields[i])) {
                std::snprintf(expression_lines_[i], sizeof(expression_lines_[i]), "%s", fields[i]);
                dirty_ = true;
            }
        }
    }
    bool expression_status_due(uint32_t now) {
        // Sampling a value does not make an unchanged page dirty.
        if (!dirty_ && uint32_t(now - refreshed_) < 80) return false;
        refreshed_ = now;
        return true;
    }
    bool due(uint32_t now) {
        using R = cntrl::PageSettings;
        if (page_ == Page::pong) { pong.advance(now); return dirty_ || uint32_t(now - refreshed_) >= 20; }
        if (dirty_) return true;
        if (page_ == Page::midi_monitor) return uint32_t(now - refreshed_) >= midi::Monitor::refresh_ms;
        if (page_ == Page::midi_looper) return uint32_t(now - refreshed_) >= 50;
        if (page_ == Page::arpeggiator) return uint32_t(now - refreshed_) >= 50;
        if (page_ == Page::cable || page_ == Page::panic) return uint32_t(now - refreshed_) >= 80;
        if (page_ == Page::status || clock_page()) return uint32_t(now - refreshed_) >= settings_[R::refresh];
        return page_ == Page::tap && (held_ || tap_.flash(now, settings_[R::flash_fraction])) != flashing_;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_view)
#endif
    cntrl::Pane view(uint32_t now, uint32_t now_us, const PerformanceStatus &live_status, const cntrl::CableTest::Status &cable = {}, cntrl::Panic::Status panic = cntrl::Panic::Status::ready, unsigned window = 0, unsigned count = 1) {
        using R = cntrl::PageSettings;
        const auto &status = status_snapshot ? status_snapshot->performance : live_status;
        dirty_ = false; refreshed_ = now;
        cntrl::Pane pane;
        pane.graphics.background = settings_[R::background];
        pane.graphics.foreground = 0; pane.graphics.pad = uint8_t(settings_[R::padding]);
        if (page_ == Page::language) { language_view(pane, window); return pane; }
        if (page_ == Page::pong) { pong_view(pane, window); return pane; }
        if (page_ == Page::midi_monitor) { monitor_view(pane, window, count); return pane; }
        if (page_ == Page::midi_looper) { looper_view(pane,beat_position(now_us)); return pane; }
        if (page_ == Page::arpeggiator) { arpeggiator_view(pane); return pane; }
        if (page_ == Page::expression) {
            pane.heading_band = true;
            std::copy_n(expression_layout_, 5, pane.lines);
            for (unsigned i = 0; i < 3; ++i)
                std::snprintf(pane.lines[i + 1].text, sizeof(pane.lines[i + 1].text), "%s", expression_lines_[i]);
            return pane;
        }
        const unsigned pad = pane.graphics.pad, title = settings_[R::title_height];
        if (page_ == Page::confirm_firmware) {
            const unsigned height = title * 3 / 2;
            line(pane, 0, prompt(i18n::Message::stock_firmware_prompt, controls_.next_page), viewport_.height / 2 - height, height, settings_[R::title_font], true);
            line(pane, 1, prompt(i18n::Message::cancel_prompt, controls_.confirm), viewport_.height / 2, height, settings_[R::title_font], true, nullptr, 0, viewport_.width, pane.lines[0].graphics.size);
            return pane;
        }
        if (page_ == Page::panic) {
            page_heading(pane, i18n::text(i18n::Message::panic));
            constexpr uint16_t red = 0xf800, dark_red = 0x6000;
            const bool pressed = panic != cntrl::Panic::Status::ready;
            auto &button = pane.circles[0];
            button.diameter = 25; button.annulus = settings_[R::annulus];
            const unsigned inset = (button.diameter + 2 * button.annulus + 1) / 2 + pad;
            button.x = uint16_t(std::max(0, int(viewport_.width) - int(inset)));
            button.y = uint16_t(std::max(0, int(viewport_.height) - int(inset)));
            button.color = pressed ? red : dark_red; button.ring_color = settings_[R::ring_color];
            if (pressed) {
                pane.graphics.foreground = red;
                const unsigned height = vertical(24);
                line(pane, 1, panic == cntrl::Panic::Status::notes_off ? i18n::text(i18n::Message::notes_off) : i18n::text(i18n::Message::all_off),
                     (viewport_.height - height) / 2, height, settings_[R::status_font], true, i18n::text(i18n::Message::notes_off));
            }
            return pane;
        }
        const unsigned heading_text_height = std::min(title * 3 / 2, (viewport_.height - 2 * pad) / 2);
        const unsigned heading_height = heading_text_height + 4;
        const char *heading = page_ == Page::clock_transport ? i18n::text(i18n::Message::midi_clock) : page_ == Page::clock_tempo ? i18n::text(i18n::Message::clock_tempo) : page_ == Page::tap ? i18n::text(i18n::Message::tap_tempo) : page_ == Page::status ? i18n::text(i18n::Message::status) : page_ == Page::cable ? i18n::text(i18n::Message::midi_cable_test) : page_ == Page::ports ? i18n::text(i18n::Message::trs_ports) : page_ == Page::keyboard ? i18n::text(i18n::Message::keyboard) : i18n::text(i18n::Message::firmware);
#ifdef CNTRL_EMULATOR
        if (page_ == Page::status && preview_size_) heading = preview_label_;
#endif
        page_heading(pane, heading);
        if (clock_page()) {
            const unsigned font = settings_[R::status_font];
            char tempo[32], detail[64];
            std::snprintf(tempo, sizeof(tempo), "%u BPM", generator.bpm());
            if (page_ == Page::clock_transport) {
                const char *state = external_ ? (clock_.active() ? i18n::text(i18n::Message::external_receiving) : i18n::text(i18n::Message::external_waiting))
                    : !clock_outputs() ? i18n::text(i18n::Message::no_outputs_configured) : !available_outputs_ ? i18n::text(i18n::Message::outputs_offline) : generator.running() ? i18n::text(i18n::Message::internal_running) : i18n::text(i18n::Message::internal_stopped);
                line(pane, 1, state, vertical(30), vertical(19), font, true);
                if (external_) number_text(tempo, sizeof(tempo), clock_.period_us(), true, true);
                line(pane, 2, tempo, vertical(48), vertical(19), font, true);
                detail[0] = 0;
                auto output = [&](const cntrl::midi::OutputConnection &port) {
                    if (!(clock_outputs() & (1u << port.route))) return;
                    auto used = std::strlen(detail);
                    std::snprintf(detail + used, sizeof(detail) - used, "%s%s%s", used ? " " : "",
                        port.label, available_outputs_ & (1u << port.route) ? "" : "*");
                };
                for (const auto &port : outputs_) output(port);
                line(pane, 3, detail, vertical(67), vertical(14), font, true);
                line(pane, 4, prompt(i18n::Message::start_continue_prompt, controls_.previous, controls_.next), vertical(82), vertical(16), font, true);
                line(pane, 5, prompt(i18n::Message::stop_source_prompt, controls_.confirm, controls_.next_section), vertical(102), vertical(16), font, true);
            } else {
                i18n::format(detail, sizeof(detail), i18n::Message::tempo_slot, slot_ + 1, settings_.words[R::slot1 + slot_]);
                line(pane, 1, detail, vertical(28), vertical(18), font, true);
                line(pane, 2, tempo, vertical(50), vertical(24), font, true);
                line(pane, 3, prompt(i18n::Message::tempo_adjust_prompt, controls_.previous, controls_.next, controls_.confirm), vertical(80), vertical(17), font, true);
                i18n::format(detail, sizeof(detail), i18n::Message::clock_late_lost, generator.late_us, generator.skipped + generator.blocked);
                line(pane, 4, detail, vertical(102), vertical(16), font, true);
            }
            return pane;
        }
        if (page_ == Page::cable) {
            const bool active = cable.enabled;
            const unsigned font = 16; // Preserve Cable Test's Atkinson font.
            line(pane, 1, i18n::text(i18n::Message::output_heading), vertical(32), vertical(18), font, true, nullptr, horizontal(56), horizontal(100));
            line(pane, 2, i18n::text(i18n::Message::input_heading), vertical(32), vertical(18), font, true, nullptr, horizontal(106), horizontal(154));
            unsigned rows = std::max(cable.output_count, cable.input_count);
            if (rows > cntrl::max_lines - 3) rows = cntrl::max_lines - 3;
            for (unsigned i = 0; i < rows; ++i) {
                unsigned y = vertical(59 + i * 52 / std::max(1u, rows));
                const char *label = i < cable.output_count ? cable.connections[i].label : i18n::text(i18n::Message::input);
                line(pane, 3 + i, label, y, vertical(18), font, false, nullptr, horizontal(8), horizontal(54));
                pane.lines[3 + i].graphics.foreground = active ? 0 : 0x8410;
                if (i < cable.output_count && i < sizeof(pane.circles) / sizeof(pane.circles[0])) {
                    uint16_t color = !active || !cable.sending[i] ? 0x8410 : i % 2 ? 0x07e0 : 0x001f;
                    pane.circles[i] = {uint16_t(horizontal(78)), uint16_t(y + vertical(9)), uint16_t(vertical(12)), 0, color, 0};
                }
                if (i < cable.input_count && i + cable.output_count < sizeof(pane.circles) / sizeof(pane.circles[0])) {
                    int source = cable.inputs[i];
                    uint16_t color = !active || source < 0 ? 0x8410 : source % 2 ? 0x07e0 : 0x001f;
                    pane.circles[i + cable.output_count] = {uint16_t(horizontal(130)), uint16_t(y + vertical(9)), uint16_t(vertical(12)), 0, color, 0};
                }
            }
            pane.toggle = {uint16_t(horizontal(design_height)), uint16_t(vertical(110)), true, cable.enabled, false};
            return pane;
        }
        if (page_ == Page::keyboard) {
            const auto selected_shortcut = keyboard_shortcut();
            const unsigned font = settings_[R::status_font];
            line(pane, 1, selected_shortcut.name[0] ? selected_shortcut.name : i18n::text(i18n::Message::no_shortcuts), vertical(38), vertical(24), font, true);
            line(pane, 2, shortcut_held ? i18n::text(i18n::Message::held) : i18n::text(i18n::Message::ready), vertical(66), vertical(20), font, true);
            line(pane, 3, prompt(i18n::Message::select_pair_prompt, controls_.previous, controls_.next), vertical(91), vertical(16), font, true);
            line(pane, 4, prompt(i18n::Message::hold_send_prompt, controls_.confirm), vertical(109), vertical(16), font, true);
            return pane;
        }
        if (page_ == Page::ports) {
            const char *names[] = {i18n::text(i18n::Message::off), i18n::text(i18n::Message::midi_type_a), i18n::text(i18n::Message::midi_type_b), i18n::text(i18n::Message::expression), i18n::text(i18n::Message::aux_switch)};
            char current[40];
            if (port_available_) std::snprintf(current, sizeof(current), "%.23s: %s", i18n::translated(port_name_), names[unsigned(active_port_)]);
            else std::snprintf(current, sizeof(current), "%s", i18n::translated(port_name_));
            const unsigned font = settings_[R::status_font];
            line(pane, 1, current, vertical(34), vertical(22), font, true);
            line(pane, 2, port_message_ ? i18n::translated(port_message_) : names[unsigned(candidate)], vertical(60), vertical(22), font, true);
            if (port_available_) {
                line(pane, 3, prompt(i18n::Message::jack_mode_prompt, controls_.previous, controls_.next), vertical(87), vertical(17), font, true);
                line(pane, 4, prompt(i18n::Message::apply_prompt, controls_.confirm), vertical(106), vertical(17), font, true);
            }
            return pane;
        }
        if (page_ == Page::firmware) {
            const unsigned label_height = std::min(title * 2, (viewport_.height - 2 * pad) / 4);
            line(pane, 1, i18n::text(i18n::Message::use_stock), viewport_.height - pad - label_height * 2, label_height, pane.graphics.font, true);
            line(pane, 2, i18n::text(i18n::Message::firmware_caption), viewport_.height - pad - label_height, label_height, pane.graphics.font, true);
            return pane;
        }
        if (page_ == Page::tap) {
            char number[24], sample[24];
            number_text(number, sizeof(number), tap_.period(), false, true);
            const unsigned digits = settings_[R::digits], decimals = settings_[R::decimals];
            unsigned pos = 0;
            for (unsigned i = 0; i < digits; ++i) sample[pos++] = '8';
            if (decimals) { sample[pos++] = '.'; for (unsigned i = 0; i < decimals; ++i) sample[pos++] = '8'; }
            sample[pos] = 0;
            line(pane, 1, number, pad + heading_height, settings_[R::tempo_height], settings_[R::tempo_font], false, sample);
            flashing_ = held_ || tap_.flash(now, settings_[R::flash_fraction]);
            pane.circles[0].diameter = settings_[R::diameter]; pane.circles[0].annulus = settings_[R::annulus];
            pane.circles[0].x = uint16_t(viewport_.width / 2);
            unsigned start = pad + heading_height + settings_[R::tempo_height];
            pane.circles[0].y = uint16_t((start + viewport_.height - pad) / 2);
            pane.circles[0].color = settings_[flashing_ ? R::on_color : R::off_color];
            pane.circles[0].ring_color = settings_[R::ring_color];
        } else {
            char rows[5][64] = {};
            i18n::format(rows[0], sizeof(rows[0]), i18n::Message::cycles_patches, status.cycles, status.patches);
            char tap[24], clock[24], tap_label[32], uptime[32], git[16] = {};
            number_text(tap, sizeof(tap), status_snapshot ? status_snapshot->tap_period_ms : tap_.period(), false, true);
            number_text(clock, sizeof(clock), status_snapshot ? status_snapshot->clock_period_us : clock_.period_us(), true, true);
            i18n::format(rows[1], sizeof(rows[1]), i18n::Message::clock_value, clock + std::strspn(clock," "));
            std::snprintf(rows[2], sizeof(rows[2]), "RAM:%lu/%luk %lu/%luk",
                (unsigned long)(status.ram_used[0] / 1024),
                (unsigned long)((status.ram_used[0] + status.ram_free[0]) / 1024),
                (unsigned long)(status.ram_used[1] / 1024),
                (unsigned long)((status.ram_used[1] + status.ram_free[1]) / 1024));
            constexpr uint64_t gibibyte = 1024ull * 1024 * 1024;
            if (status.disk_valid) {
                uint64_t tenths = (status.disk_used * 10 + gibibyte / 2) / gibibyte;
                i18n::format(rows[3], sizeof(rows[3]), i18n::Message::disk_value, (unsigned long)(tenths / 10), (unsigned long)(tenths % 10), (unsigned long)((status.disk_used + status.disk_free + gibibyte / 2) / gibibyte));
            } else i18n::format(rows[3], sizeof(rows[3]), i18n::Message::disk_unknown);
            uint64_t seconds = (status_snapshot ? status_snapshot->uptime_ms : uptime_) / 1000;
            std::snprintf(uptime, sizeof(uptime), "%lu:%02u:%02u", (unsigned long)(seconds / 3600), unsigned(seconds / 60 % 60), unsigned(seconds % 60));
            if (status.starts_status == StartsStatus::ready) {
                char number[21]; *std::to_chars(number, number + 20, status.starts).ptr = 0;
                i18n::format(rows[4], sizeof(rows[4]), i18n::Message::starts_value, number);
            }
            else i18n::format(rows[4], sizeof(rows[4]), i18n::Message::starts_value, status.starts_status == StartsStatus::error ? (status.starts_error[0] ? status.starts_error : i18n::text(i18n::Message::error)) : "--");
#ifdef CNTRL_GIT_COMMIT
            std::snprintf(git, sizeof(git), "%07x", unsigned(status_snapshot ? status_snapshot->git_commit : CNTRL_GIT_COMMIT));
#endif
            i18n::format(tap_label, sizeof(tap_label), i18n::Message::tap_value, tap + std::strspn(tap," "));
            const unsigned body_top = pad + heading_height;
            const unsigned body_height = viewport_.height - pad - body_top;
            auto body = [&](unsigned maximum_size) {
                for (unsigned i = 0; i < 5; ++i) {
                    const unsigned y = body_top + body_height * i / 5;
                    const unsigned height = body_height * (i + 1) / 5 - body_height * i / 5;
                    if (i == 1) {
                        const auto inset = pane.graphics.pad;
                        pane.graphics.pad = std::min<uint8_t>(inset,1);
                        line(pane, 2, rows[i], y, height, settings_[R::status_font], false, nullptr, 0, viewport_.width / 2, maximum_size);
                        line(pane, 3, tap_label, y, height, settings_[R::status_font], false, nullptr, viewport_.width / 2, viewport_.width, maximum_size);
                        pane.graphics.pad = inset;
                    } else if (i >= 3) {
                        const unsigned right = i + 4;
                        const char *value = i == 3 ? uptime : git;
                        if (*value) line(pane, right, value, y, height, settings_[R::status_font], false, nullptr, 0, viewport_.width, maximum_size, true);
                        line(pane, i + 2, rows[i], y, height, settings_[R::status_font], false, nullptr, 0, *value ? unsigned(pane.lines[right].x) : viewport_.width, maximum_size);
                    } else {
                        line(pane, i == 0 ? 1 : i + 2, rows[i], y, height, settings_[R::status_font], false, nullptr, 0, viewport_.width, maximum_size);
                    }
                }
            };
            body(96);
            unsigned shared_size = pane.lines[1].graphics.size;
            for (unsigned i = 2; i <= 8; ++i) if (pane.lines[i].text[0]) shared_size = std::min(shared_size, unsigned(pane.lines[i].graphics.size));
            body(shared_size);
            for (unsigned i = 1; i <= 8; ++i) {
                auto &l = pane.lines[i];
                l.graphics.foreground = 0x0011; // Dark blue, #00008B.
                l.alternate_color = 0x0400; // Green, #008000.
                bool value = i >= 7;
                const char *cursor = l.text;
                for (unsigned j = 0; *cursor; ++j) {
                    uint32_t code; if (!utf8::next(cursor, code)) break;
                    if (code == ':' && !value) { value = true; continue; }
                    if (i == 1 && code == ' ' && *cursor == ' ') value = false;
                    if (value) l.color_mask |= uint64_t(1) << j;
                }
            }
            clock_.poll(now_us);
            pane.lines[2].stippled = status_snapshot ? status_snapshot->clock_period_us && !status_snapshot->clock_active : clock_.period_us() && !clock_.active();
        }
        return pane;
    }
private:
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(pong_view)
#endif
    void pong_view(Pane &pane, unsigned window) {
        pane.graphics.background = 0; pane.graphics.foreground = 0xffff; pane.graphics.pad = 0;
        auto rectangle = [&](unsigned index, float x, float y, float width, float height, uint16_t color, bool stippled = false) {
            const int left = int((x - window * 160) * viewport_.width / 160), top = int(y * viewport_.height / 128);
            const int right = std::min(int(viewport_.width), left + std::max(1, int(width * viewport_.width / 160)));
            const int bottom = std::min(int(viewport_.height), top + std::max(1, int(height * viewport_.height / 128)));
            const int clipped_left = std::max(0, left), clipped_top = std::max(0, top);
            if (right <= clipped_left || bottom <= clipped_top) return;
            pane.rectangles[index] = {int16_t(clipped_left), int16_t(clipped_top), uint16_t(right - clipped_left), uint16_t(bottom - clipped_top), color, stippled};
        };
        rectangle(0, 7, pong.left - Pong::paddle_height / 2, 4, Pong::paddle_height, 0x07ff);
        rectangle(1, 469, pong.right - Pong::paddle_height / 2, 4, Pong::paddle_height, 0xf81f);
        rectangle(2, 240, Pong::top, 1, Pong::bottom - Pong::top, 0x8410);
        rectangle(3, pong.x - 2, pong.y - 2, 4, 4, 0xffe0);
        const unsigned font = settings_[cntrl::PageSettings::status_font];
        char score[32];
        i18n::format(score, sizeof(score), i18n::Message::player_score, window == 0 ? 1 : 2, window == 0 ? pong.left_score : pong.right_score);
        line(pane, 0, window == 1 ? "PONG" : score, vertical(3), vertical(18), font, true, nullptr, 0, 0, 16);
        pane.lines[0].graphics.foreground = window == 0 ? 0x07ff : window == 2 ? 0xf81f : 0xffff;
        const char *hint = window == 0 ? prompt(i18n::Message::up_down_prompt, controls_.previous_section, controls_.previous)
            : window == 2 ? prompt(i18n::Message::up_down_prompt, controls_.back, controls_.confirm)
            : prompt(i18n::Message::serve_exit_prompt, controls_.next, controls_.next_section);
        if (pong.engaged || window == 1) line(pane, 1, hint, vertical(115), vertical(11), font, true, nullptr, 0, 0, 10);
        if (window == 1 && pong.engaged && pong.state != Pong::State::playing) {
            const char *message = pong.state == Pong::State::paused ? i18n::text(i18n::Message::paused) : (pong.left_score == 11 ? i18n::text(i18n::Message::player_one_wins) : i18n::text(i18n::Message::player_two_wins));
            line(pane, 2, message, vertical(45), vertical(15), font, true, nullptr, 0, 0, 12);
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(arpeggiator_view)
#endif
    void arpeggiator_view(Pane &pane) {
        using R = cntrl::PageSettings;
        line(pane,0,i18n::text(i18n::Message::arpeggiator),2,18,settings_[R::title_font],true);
        const char *states[] = {i18n::text(i18n::Message::off),i18n::text(i18n::Message::waiting),i18n::text(i18n::Message::playing),i18n::text(i18n::Message::record_armed),i18n::text(i18n::Message::recording),i18n::text(i18n::Message::finish_armed)};
        const char *selections[] = {i18n::text(i18n::Message::forward),i18n::text(i18n::Message::reverse),i18n::text(i18n::Message::played),i18n::text(i18n::Message::back_and_forth),i18n::text(i18n::Message::random),i18n::text(i18n::Message::random_walk),i18n::text(i18n::Message::mod),i18n::text(i18n::Message::shuffle),i18n::text(i18n::Message::outside_in)};
        const char *errors[] = {"",i18n::text(i18n::Message::capacity_exceeded),i18n::text(i18n::Message::clock_unavailable),i18n::text(i18n::Message::output_blocked)};
        char text[96];
        std::snprintf(text,sizeof(text),"%s %s",arpeggiator && arpeggiator->mono() ? "Mono" : "Poly",arpeggiator && arpeggiator->configured() ? states[unsigned(arpeggiator->state())] : i18n::text(i18n::Message::unavailable));
        line(pane,1,text,24,14,settings_[R::status_font],true);
        line(pane,2,arpeggiator ? selections[arpeggiator->selection()] : "",42,12,settings_[R::status_font],true);
        i18n::format(text, sizeof(text), i18n::Message::entries_count, arpeggiator ? arpeggiator->size() : 0);
        line(pane,3,text,58,12,settings_[R::status_font],true);
        line(pane,4,arpeggiator ? errors[unsigned(arpeggiator->error())] : "",74,12,settings_[R::status_font],true);
        line(pane,5,arpeggiator && arpeggiator->mono() ? prompt(i18n::Message::record_finish_play_stop_prompt, controls_.previous, controls_.next) : prompt(i18n::Message::on_off_prompt, controls_.next),90,12,settings_[R::status_font],true);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_view)
#endif
    void looper_view(Pane &pane, midi::BeatPosition clock) {
        using R = cntrl::PageSettings;
        line(pane,0,i18n::text(i18n::Message::midi_looper),2,18,settings_[R::title_font],true);
        const char *states[] = {i18n::text(i18n::Message::empty),i18n::text(i18n::Message::armed),i18n::text(i18n::Message::recording),i18n::text(i18n::Message::closing),i18n::text(i18n::Message::playing),i18n::text(i18n::Message::overdub_armed),i18n::text(i18n::Message::overdubbing),i18n::text(i18n::Message::preparing),i18n::text(i18n::Message::stopped),i18n::text(i18n::Message::launch_armed),i18n::text(i18n::Message::undo_pending),i18n::text(i18n::Message::loading)};
        const char *errors[] = {"",i18n::text(i18n::Message::busy),i18n::text(i18n::Message::clock_unavailable),i18n::text(i18n::Message::capacity_exceeded),i18n::text(i18n::Message::memory_unavailable),i18n::text(i18n::Message::late_boundary),i18n::text(i18n::Message::output_retired),i18n::text(i18n::Message::file_error),i18n::text(i18n::Message::input_changed)};
        char text[96];
        std::snprintf(text,sizeof(text),"%s%s%s",looper ? states[unsigned(looper->state())] : i18n::text(i18n::Message::unavailable),
            looper && looper->error() != midi::MidiLooper::Error::none ? ": " : "",looper ? errors[unsigned(looper->error())] : "");
        line(pane,1,text,24,14,settings_[R::status_font],true);
        uint64_t length = looper ? looper->length() : 0;
        uint64_t position = looper ? looper->position(clock) : 0;
        i18n::format(text, sizeof(text), i18n::Message::loop_beats, (unsigned long long)(position >> 32), unsigned((position & 0xffffffffu) * 100 >> 32), (unsigned long long)(length >> 32), unsigned((length & 0xffffffffu) * 100 >> 32));
        line(pane,2,text,42,12,settings_[R::status_font],true);
        i18n::format(text, sizeof(text), i18n::Message::loop_events, looper ? looper->events() : 0, looper ? looper->capacity() : 0, !clock.running ? i18n::text(i18n::Message::clock_waiting_suffix) : looper && looper->unsaved() ? i18n::text(i18n::Message::unsaved_suffix) : "", looper && looper->backward() ? i18n::text(i18n::Message::backward_suffix) : i18n::text(i18n::Message::forward_suffix));
        line(pane,3,text,58,12,settings_[R::status_font],true);
        line(pane,4,looper_prompt[0] ? looper_prompt : looper && looper->direction_pending() ? i18n::text(i18n::Message::direction_change_pending) : "",74,12,settings_[R::status_font],true);
        line(pane,5,prompt(i18n::Message::record_play_undo_prompt, controls_.previous, controls_.next, controls_.confirm),90,12,settings_[R::status_font],true);
        line(pane,6,prompt(i18n::Message::clear_more_prompt, controls_.previous_section, controls_.next_section),106,12,settings_[R::status_font],true);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(monitor_view)
#endif
    void monitor_view(Pane &pane, unsigned window, unsigned count) {
        using Direction = midi::Monitor::Direction;
        using R = cntrl::PageSettings;
        const Direction direction = count == 1 || (count == 3 && window == 1) ? Direction::both : window == 0 ? Direction::incoming : Direction::outgoing;
        const char *heading = direction == Direction::both ? i18n::text(i18n::Message::combined) : direction == Direction::incoming ? i18n::text(i18n::Message::incoming) : i18n::text(i18n::Message::outgoing);
        const unsigned pad = pane.graphics.pad;
        const unsigned title = std::min(unsigned(settings_[R::title_height]) + 4, viewport_.height / 4);
        line(pane, 0, heading, pad, title, settings_[R::title_font], true);
        pane.heading_band = true; pane.lines[0].graphics.foreground = 0xffff; pane.lines[0].graphics.background = 0;
        pane.lines[0].graphics.line_height = uint16_t(title + 2 * pad); pane.lines[0].y = 0;
        const unsigned row_height = 10, body_top = title + 2 * pad + row_height;
        const unsigned bottom = viewport_.height > pad + row_height ? viewport_.height - pad - row_height : 0;
        const unsigned rows = std::min(4u, bottom > body_top ? (bottom - body_top) / (3 * row_height) : 0u);
        char text[64];
        if (!monitor || !monitor->active()) i18n::format(text, sizeof(text), i18n::Message::unavailable_memory);
        else i18n::format(text, sizeof(text), i18n::Message::monitor_history, monitor->frozen() ? i18n::text(i18n::Message::frozen) : i18n::text(i18n::Message::live), monitor->advanced() ? i18n::text(i18n::Message::advanced_suffix) : "", (unsigned long long)monitor->overwritten(), monitor->dropped());
        line(pane, 1, text, body_top - row_height, row_height, settings_[R::status_font], false, nullptr, 0, 0, 8);
        const midi::Monitor::Entry *entries[4] = {}; unsigned used = 0;
        if (monitor && monitor->active()) for (uint64_t position = monitor->cursor(); position > monitor->oldest() && used < rows;) {
            const auto *e = monitor->entry(--position);
            if (e && (direction == Direction::both || e->direction == direction || e->direction == Direction::both)) entries[used++] = e;
        }
        if (!used) line(pane, 2, direction == Direction::both ? i18n::text(i18n::Message::no_traffic) : direction == Direction::incoming ? i18n::text(i18n::Message::no_input) : i18n::text(i18n::Message::no_output), body_top, row_height, settings_[R::status_font], false, nullptr, 0, 0, 8);
        for (unsigned i = 0; i < used; ++i) {
            const auto &e = *entries[used - i - 1];
            const auto *endpoint = outputs_.route(e.route);
            const char *alias = endpoint ? endpoint->name : i18n::text(i18n::Message::unknown);
            const char *direction_label = e.direction == Direction::incoming ? "I" : e.direction == Direction::outgoing ? "O" : "-";
            char seconds[21]; *std::to_chars(seconds, seconds + 20, e.time / 1000000).ptr = 0;
            std::snprintf(text, sizeof(text), "%s.%03u %s%s %.12s%s/%u", seconds, unsigned(e.time / 1000 % 1000), direction_label,
                e.flags & midi::Monitor::diagnostic ? "/test" : "", alias, std::strlen(alias) > 12 ? "~" : "", unsigned(e.cable));
            const unsigned first = 2 + i * 3, y = body_top + i * 3 * row_height;
            line(pane, first, text, y, row_height, settings_[R::status_font], false, nullptr, 0, 0, 8);
            midi::Monitor::describe(e, text, sizeof(text));
            line(pane, first + 1, text, y + row_height, row_height, settings_[R::status_font], false, nullptr, 0, 0, 8);
            text[0] = 0;
            unsigned start = e.flags & midi::Monitor::running ? 1 : 0;
            for (unsigned j = start; j < e.size; ++j) {
                const auto used_bytes = std::strlen(text);
                std::snprintf(text + used_bytes, sizeof(text) - used_bytes, "%s%02X", used_bytes ? " " : "", unsigned(e.bytes[j]));
            }
            if (e.flags & midi::Monitor::running) std::strcat(text, i18n::text(i18n::Message::running_suffix));
            line(pane, first + 2, text, y + 2 * row_height, row_height, settings_[R::status_font], false, nullptr, 0, 0, 8);
        }
        const char *help = monitor && monitor->frozen() ? prompt(i18n::Message::history_live_prompt, controls_.previous, controls_.next, controls_.confirm) : prompt(i18n::Message::freeze_prompt, controls_.confirm);
        line(pane, 14, help, bottom, row_height, settings_[R::status_font], false, nullptr, 0, 0, 8);
    }
    bool clock_changed_ = false, beat_external_ = false;
    uint32_t beat_restart_ = 0, beat_source_restart_ = 0;
    static constexpr unsigned design_width = 5 * 32, design_height = 4 * 32;
    Controls controls_;
    char prompt_[64] = {};
    template<class... Indices> const char *prompt(i18n::Message message, Indices... indices) {
        if (((!label(indices)[0]) || ...)) return "";
        i18n::format(prompt_, sizeof(prompt_), message, label(indices)...); return prompt_;
    }
    const char *label(unsigned index) const { return index < controls_.count ? controls_.bindings[index].label : ""; }
    cntrl::midi::Outputs outputs_;
    const Fonts &fonts_;
    Viewport viewport_;
    cntrl::PageLayout layout_;
    unsigned index_ = 0;
    PortMode active_port_ = PortMode::off;
    const char *port_message_ = nullptr;
    const char *port_name_ = i18n::definitions[unsigned(i18n::Message::no_configurable_ports)].text;
    bool port_available_ = false;
    unsigned available_outputs_ = 0, slot_ = 0;
    bool external_ = false;
    unsigned horizontal(unsigned value) const { return value * viewport_.width / design_width; }
    unsigned vertical(unsigned value) const { return value * viewport_.height / design_height; }
    void recall_slot() {
        unsigned bpm = settings_.words[cntrl::PageSettings::slot1 + slot_];
        if (bpm) generator.set_tempo((60000000u + bpm / 2) / bpm);
    }
    void number_text(char *text, unsigned length, uint64_t period, bool microseconds, bool fixed) const {
        using R = cntrl::PageSettings;
        unsigned digits = settings_[R::digits], decimals = settings_[R::decimals];
        if (!period) {
            if (!fixed) { std::snprintf(text, length, "--"); return; }
            unsigned pos = 0;
            for (unsigned i = 0; i < digits; ++i) text[pos++] = '-';
            if (decimals) { text[pos++] = '.'; for (unsigned i = 0; i < decimals; ++i) text[pos++] = '-'; }
            text[pos] = 0; return;
        }
        unsigned scale = 1, limit = 1;
        for (unsigned i = 0; i < decimals; ++i) scale *= 10;
        for (unsigned i = 0; i < digits; ++i) limit *= 10;
        uint64_t value = ((microseconds ? 60000000ull : 60000ull) * scale + period / 2) / period;
        if (fixed && value / scale >= limit) { i18n::format(text, length, i18n::Message::over); return; }
        if (decimals) std::snprintf(text, length, "%*lu.%0*lu", fixed ? int(digits) : 1,
            (unsigned long)(value / scale), int(decimals), (unsigned long)(value % scale));
        else std::snprintf(text, length, "%*lu", fixed ? int(digits) : 1, (unsigned long)value);
    }
    void line(cntrl::Pane &pane, unsigned index, const char *text, unsigned y, unsigned height,
              unsigned face, bool centered, const char *sample = nullptr, unsigned left_edge = 0, unsigned right_edge = 0, unsigned maximum_size = 96, bool right_aligned = false) {
        bool preview = false;
#ifdef CNTRL_EMULATOR
        preview = page_ == Page::status && preview_size_ && index > 0;
        if (preview) { face = 254; maximum_size = preview_size_; }
#endif
        auto &l = pane.lines[index];
        utf8::copy(l.text, sizeof(l.text), text);
        const bool ordinary = !preview && page_ != Page::cable && page_ != Page::status && page_ != Page::tap && page_ != Page::pong;
        if (ordinary) {
            face = 17;
            maximum_size = std::min(maximum_size, index ? 12u : 16u);
        } else if (!preview && page_ == Page::status && index) face = status_face;
        if (!preview && i18n::translated_ui && face != status_face) face = localized_face;
        l.graphics = pane.graphics; l.graphics.font = uint8_t(face);
        l.graphics.asset = preview ? 0 : i18n::identity(text);
        int left = 0, top = 0, width = 0, ink_height = 0;
        const char *fitting = sample ? sample : l.text;
        if (!right_edge) right_edge = viewport_.width;
        unsigned minimum_size = ordinary && index ? std::min(12u, maximum_size) : 0;
        if (!minimum_size) for (unsigned size = 8; size <= 96; ++size)
            if (fonts_.supports(face, size)) { minimum_size = size; break; }
        for (unsigned size = std::max(minimum_size, std::min(96u, maximum_size)); size >= 8; --size) {
            // A fixed bitmap face may start above the requested size ceiling.
            if ((size > maximum_size && size != minimum_size) || !fonts_.supports(face, size)) continue;
            auto measure = [&](const char *value) {
                const auto b = fonts_.bounds(face, size, 0, value);
                left = b.left; top = b.top; width = b.width; ink_height = b.height;
            };
            measure(fitting);
            const int available = std::max(0, int(right_edge - left_edge) - 2 * int(pane.graphics.pad));
            if (!preview && (width > available || ink_height > int(height)) && size > minimum_size) continue;
            l.graphics.size = uint8_t(size); l.graphics.line_height = uint16_t(height); l.graphics.top = 0;
            if (preview && width > available) {
                unsigned n = unsigned(std::strlen(l.text));
                while (n && width > available) { l.text[--n] = 0; measure(l.text); }
            } else if (!sample && width > available) {
                if (l.graphics.asset) {
                    const char *short_form = i18n::short_text(i18n::Message(l.graphics.asset - 1));
                    if (*short_form) { utf8::copy(l.text, sizeof(l.text), short_form); measure(l.text); }
                }
                while (l.text[0] && width > available) {
                    utf8::erase_last(l.text);
                    char shortened[64]; std::snprintf(shortened, sizeof(shortened), "%s...", l.text);
                    measure(shortened);
                    if (width <= available) utf8::copy(l.text, sizeof(l.text), shortened);
                }
            }
            if (centered && sample) measure(l.text);
            l.x = uint16_t(centered ? (int(left_edge + right_edge) - width) / 2 - left : sample || right_aligned ? int(right_edge - pane.graphics.pad) - width - left : left_edge + pane.graphics.pad);
            l.y = int16_t(int(y) + (int(height) - ink_height) / 2 - top);
            return;
        }
    }

#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_language_view)
#endif
    void language_view(cntrl::Pane &pane, unsigned window) {
        if (!languages) return;
        if (window) {
            page_heading(pane, i18n::text(i18n::Message::language));
            const unsigned inputs[] = {controls_.next, controls_.next_section, controls_.confirm, controls_.back};
            char labels[4][32];
            for (unsigned i = 0; i < 4; ++i) {
                utf8::copy(labels[i], sizeof(labels[i]), languages->input_label(inputs[i]));
                // Letter switch IDs use the physical uppercase legend.
                if (labels[i][0] >= 'a' && labels[i][0] <= 'z' && !labels[i][1]) labels[i][0] -= 'a' - 'A';
            }
            char text[64];
            i18n::format(text, sizeof(text), i18n::Message::language_scroll,
                labels[0], labels[1]);
            line(pane, 1, text, vertical(40), vertical(20), 17, true, nullptr, 0, 0, 12);
            i18n::format(text, sizeof(text), i18n::Message::language_select, labels[2]);
            line(pane, 2, text, vertical(70), vertical(20), 17, true, nullptr, 0, 0, 12);
            i18n::format(text, sizeof(text), i18n::Message::language_next_page, labels[3]);
            line(pane, 3, text, vertical(100), vertical(20), 17, true, nullptr, 0, 0, 12);
            return;
        }
        const unsigned count = languages->language_count();
        if (!count) return;
        language = std::min(language, count - 1);
        const unsigned pad = pane.graphics.pad;
        const unsigned row_height = std::max(16u, viewport_.height * 20 / 128);
        const unsigned available = viewport_.height > 2 * pad ? viewport_.height - 2 * pad : 0;
        const unsigned rows = std::min({count, max_lines, available / row_height});
        const unsigned first = language > rows / 2 ? std::min(language - rows / 2, count - rows) : 0;
        for (unsigned row = 0; row < rows; ++row) {
            const unsigned item = first + row;
            auto &text = pane.lines[row]; text.graphics = pane.graphics;
            utf8::copy(text.text, sizeof(text.text), languages->language_name(item));
            text.graphics.font = 17;
            text.graphics.flag = flags::identifier(languages->language_tag(item));
            text.x = uint16_t(pad + flags::width + 4);
            unsigned size = 12;
            for (; size > 8; --size) if (fonts_.bounds(17, size, 0, text.text).width <= int(viewport_.width - pad - text.x)) break;
            text.graphics.size = uint8_t(size); text.graphics.line_height = uint16_t(row_height);
            const auto bounds = fonts_.bounds(17, size, 0, text.text);
            text.y = int16_t(pad + row * row_height + (int(row_height) - bounds.height) / 2 - bounds.top);
            if (item == language) {
                text.graphics.foreground = 0xffff; text.graphics.background = 0;
                pane.rectangles[0] = {int16_t(pad), int16_t(pad + row * row_height), uint16_t(viewport_.width - 2 * pad), uint16_t(row_height), 0, false};
            }
        }
    }

#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_heading)
#endif
    void page_heading(cntrl::Pane &pane, const char *text) {
        using R = cntrl::PageSettings;
        const unsigned pad = pane.graphics.pad;
        const unsigned height = std::min(unsigned(settings_[R::title_height]) * 3 / 2, (viewport_.height - 2 * pad) / 2);
        const char *sample = i18n::text(i18n::Message::midi_cable_test);
#ifdef CNTRL_EMULATOR
        if (page_ == Page::status && preview_size_) sample = nullptr;
#endif
        line(pane, 0, text, pad + 2, height, page_ == Page::cable ? 16 : settings_[R::title_font], true, sample);
        pane.heading_band = true;
        auto &header = pane.lines[0];
        header.graphics.line_height += 4;
        header.graphics.foreground = 0xffff; header.graphics.background = 0;
        header.graphics.top = header.y; header.y = 0;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_expression_layout)
#endif
    void prepare_expression_layout() {
        using R = cntrl::PageSettings;
        // Configure may run while another page is selected.
        const auto previous_page = page_;
        page_ = Page::expression;
        cntrl::Pane pane;
        pane.graphics.background = settings_[R::background];
        pane.graphics.foreground = 0; pane.graphics.pad = uint8_t(settings_[R::padding]);
        page_heading(pane, i18n::text(i18n::Message::expression));
        const unsigned font = settings_[R::status_font];
        // Fit port labels and status messages once, sharing one stable body size.
        unsigned size = 96;
        const auto fit = [&](const char *text) {
            line(pane, 1, text, vertical(30), vertical(20), font, false, nullptr, 0, 0, size);
            size = pane.lines[1].graphics.size;
        };
        fit(i18n::text(i18n::Message::no_configurable_ports));
        fit(i18n::text(i18n::Message::raw_uncalibrated_sample));
        fit(i18n::text(i18n::Message::select_expression_role));
        fit(i18n::text(i18n::Message::reconnect_heel));
        fit(prompt(i18n::Message::calibrate_prompt, controls_.confirm));
        for (const auto &port : outputs_) if (port.port != midi::no_port) {
            char text[40];
            i18n::format(text, sizeof(text), i18n::Message::unassigned_port, port.label); fit(text);
            std::snprintf(text, sizeof(text), "%.20s  CC 888 Ch 88", port.label); fit(text);
        }
        for (unsigned i = 0; i < 3; ++i) {
            line(pane, i + 1, "Ag0123456789", vertical(30 + 24 * i), vertical(20), font, false, nullptr, 0, 0, size);
            pane.lines[i + 1].text[0] = 0;
        }
        line(pane, 4, prompt(i18n::Message::jack_cancel_prompt, controls_.previous, controls_.next), vertical(105), vertical(18), font, true);
        std::copy_n(pane.lines, 5, expression_layout_);
        page_ = previous_page;
    }
    TextLine expression_layout_[5]; // Heading, three live values, and control hint.
    char expression_lines_[3][40] = {};
    cntrl::PageSettings settings_;
#ifdef CNTRL_EMULATOR
    char preview_label_[48] = {};
    unsigned preview_size_ = 0;
#endif
    cntrl::TapTempo tap_;
    cntrl::ClockTempo clock_;
    Page page_ = Page::status;
    uint64_t uptime_ = 0;
    uint32_t previous_ = 0, refreshed_ = 0;
    bool dirty_ = false, flashing_ = false, held_ = false;
};

} // namespace cntrl::ui
