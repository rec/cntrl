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
        result.tap_period_ms = tap_.period();
        result.clock_period_us = observed_clock().period_us(); result.clock_active = observed_clock().measured();
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
    enum class Page { status = 1, cable = 3, panic, none = 10, patch_chooser, midi_monitor, midi_looper, arpeggiator, banked, pong, language, menu, time, calibrate };
    struct ViewState {
        unsigned index = 0, language = 0;
        Page page = Page::none;
        bool dirty = true, flashing = false, held = false;
        uint32_t refreshed = 0;
        char looper_prompt[96] = {};
        Pong pong;
    };
    ViewState save_view() const {
        ViewState view;
        view.index = index_; view.page = page_;
        view.language = language;
        view.dirty = dirty_; view.flashing = flashing_; view.held = held_; view.refreshed = refreshed_;
        std::memcpy(view.looper_prompt,looper_prompt,sizeof(looper_prompt));
        view.pong = pong;
        return view;
    }
    void restore_view(const ViewState &view) {
        index_ = view.index; page_ = view.page;
        language = view.language;
        dirty_ = view.dirty; flashing_ = view.flashing; held_ = view.held; refreshed_ = view.refreshed;
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
        settings_ = settings;
        using R = cntrl::PageSettings;
        selected_clock_ = 0; sending_clock_ = waiting_for_clock_ = false;
        if (unsigned bpm = settings_.words[R::slot1]) generator.set_tempo((60000000u + bpm / 2) / bpm);
        tap_.configure(settings[R::tap_intervals], settings[R::minimum_taps], settings[R::minimum_interval], settings[R::reset_gap]);
        for (auto &clock : clocks_) clock.configure(settings[R::clocks_per_quarter], settings[R::clock_beats], uint32_t(settings[R::clock_timeout]) * 1000);
        clock_changed_ = true;
        dirty_ = true;
    }
    void stop_setup() {
        generator.stop(); tap_ = TapTempo();
        for (auto &clock : clocks_) clock = ClockTempo();
        selected_clock_ = 0; sending_clock_ = waiting_for_clock_ = false;
        held_ = flashing_ = false; clock_changed_ = dirty_ = true;
    }
    void browse(bool reverse) {
        if (!layout_.pages()) return;
        unsigned region = layout_.page_region(index_);
        if (region == layout_.regions()) return;
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
    }
    cntrl::ClockGenerator generator;
    unsigned clock_outputs() const { return settings_[cntrl::PageSettings::clock_outputs]; }
    bool external_clock() const { return selected_clock_ != 0; }
    unsigned clock_owner() const { return clock_outputs() | generator.stopping(); }
    unsigned clock_selection() const { return selected_clock_; }
    bool clock_sending() const { return sending_clock_; }
    uint64_t source_period(unsigned source) const {
        return source == 0 ? uint64_t(tap_.period()) * 1000 : source <= 2 && clocks_[source - 1].measured() ? clocks_[source - 1].period_us() : 0;
    }
    void toggle_clock() { sending_clock_ = !sending_clock_; dirty_ = true; }
    void cycle_clock() {
        select_clock((selected_clock_ + 1) % 3);
        waiting_for_clock_ = !source_period(selected_clock_);
    }
    void clear_tap() {
        using R = cntrl::PageSettings;
        tap_.configure(settings_[R::tap_intervals], settings_[R::minimum_taps], settings_[R::minimum_interval], settings_[R::reset_gap]);
        select_available_clock(); dirty_ = true;
    }
    template<class Sink>
    void poll_clock(uint32_t now_us, unsigned available, Sink &sink) {
        for (auto &clock : clocks_) clock.poll(now_us);
        select_available_clock();
        unsigned mask = sending_clock_ && !selected_clock_ && tap_.period() ? clock_outputs() & available : 0;
        if (mask && !generator.running() && !generator.stopping()) generator.start(now_us, 0, false);
        generator.clock_outputs(mask);
        generator.poll(now_us, available, sink);
    }
    unsigned relay_clock_outputs(unsigned input) const {
        return sending_clock_ && selected_clock_ && input == clock_input(selected_clock_) ? clock_outputs() : 0;
    }
    Page page() const { return page_; }
    void revisit() {
        dirty_ = true;
    }
    void tap(uint32_t now) {
        if (page_ == Page::time) {
            if (tap_.tap(now)) {
                generator.set_tempo(tap_.period() * 1000);
                if (!generator.running() && !generator.stopping()) generator.start(now * 1000, 0, false);
                select_clock(0);
            }
        }
        else return;
        dirty_ = true;
    }
    void clock(unsigned source, uint32_t now_us) { clock_message(source, 0xf8, now_us); }
    void clock_message(unsigned source, uint8_t status, uint32_t now_us) {
        for (unsigned i = 0; i < 2; ++i) if (source < midi::max_routes && source == clock_input(i + 1)) {
            clocks_[i].poll(now_us);
            if (clocks_[i].transport(source, status, now_us) && selected_clock_ == i + 1 && status == 0xfc && arpeggiator) arpeggiator->stop();
        }
        select_available_clock();
    }
    bool clock_ready() const { return !selected_clock_ || clocks_[selected_clock_ - 1].measured(); }
    unsigned clock_period() const { return selected_clock_ ? unsigned(clocks_[selected_clock_ - 1].period_us()) : generator.period_us(); }
    cntrl::midi::BeatPosition beat_position(uint32_t now_us) {
        if (selected_clock_) clocks_[selected_clock_ - 1].poll(now_us);
        auto position = selected_clock_ ? clocks_[selected_clock_ - 1].beat_position(now_us) : generator.beat_position(now_us);
        if (clock_changed_ || selected_clock_ != beat_source_ || position.restart != beat_source_restart_) {
            ++beat_restart_; beat_source_restart_ = position.restart; beat_source_ = selected_clock_; clock_changed_ = false;
        }
        position.restart = beat_restart_; return position;
    }
    cntrl::midi::BeatPosition arpeggiator_beat_position(uint32_t now_us) {
        auto position = beat_position(now_us);
        if (selected_clock_) {
            auto continuing = clocks_[selected_clock_ - 1].holdover_position(now_us);
            position.value = continuing.value; position.running = continuing.running;
        }
        return position;
    }
    void time(uint32_t now, uint32_t now_us, bool held) {
        held_ = held;
        uptime_ += uint32_t(now - previous_); previous_ = now;
        for (auto &clock : clocks_) clock.poll(now_us);
        select_available_clock(); tap_.flash(now, 0);
    }
    template<class Pedals, class Enabled>
    cntrl::Pane calibration_view(uint32_t now, const Pedals &pedals, Enabled enabled) {
        using R = cntrl::PageSettings;
        dirty_ = false; refreshed_ = now;
        cntrl::Pane pane;
        pane.graphics.background = settings_[R::background]; pane.graphics.pad = uint8_t(settings_[R::padding]);
        const bool active = pedals.calibrating();
        constexpr uint16_t green = 0x03e0, red = 0xf800;
        const unsigned split = viewport_.width / 3;
        line(pane, 0, i18n::text(active ? i18n::Message::press_heel_then_toe : i18n::Message::calibrate),
            vertical(4), vertical(18), localized_face, false, nullptr, 0, viewport_.width, 12, true);
        if (active) pane.lines[0].graphics.foreground = green;
        line(pane, 1, i18n::text(i18n::Message::calibration_heel), vertical(23), vertical(16), localized_face, true, nullptr, 0, split);
        line(pane, 2, i18n::text(i18n::Message::calibration_toe), vertical(23), vertical(16), localized_face, true, nullptr, split, 2 * split);
        unsigned row = 0;
        for (unsigned jack = 0; jack < pedals.count() && row < 4; ++jack) {
            if (!enabled(jack)) continue;
            const auto saved = pedals.calibration(jack);
            const auto &capture = pedals.capture(jack);
            const int heel = active && capture.heel >= 0 ? capture.heel : saved.valid() ? int(saved.heel) : -1;
            const int toe = active && capture.toe >= 0 ? capture.toe : saved.valid() ? int(saved.toe) : -1;
            char text[16];
            const auto percent = [&](int value) {
                if (value < 0) { std::strcpy(text, "--.-"); return; }
                const unsigned hundredths = (unsigned(value) * 10000 + ExpressionCalibration::maximum / 2) / ExpressionCalibration::maximum;
                std::snprintf(text, sizeof(text), "%u.%02u%%", hundredths / 100, hundredths % 100);
            };
            const unsigned index = 3 + 3 * row, y = vertical(42 + 20 * row);
            percent(heel);
            line(pane, index, text, y, vertical(17), localized_face, false, "100.00%", 0, split, 12, true);
            percent(toe);
            line(pane, index + 1, text, y, vertical(17), localized_face, false, "100.00%", split, 2 * split, 12, true);
            line(pane, index + 2, pedals.name(jack), y, vertical(17), localized_face, false, nullptr, 2 * split, viewport_.width, 12);
            if (active) {
                pane.lines[index].graphics.foreground = capture.heel_ready ? green : red;
                pane.lines[index + 1].graphics.foreground = capture.toe_ready ? green : red;
            }
            ++row;
        }
        if (!row) line(pane, 3, i18n::text(i18n::Message::select_expression_role), vertical(52), vertical(30), localized_face, true);
        return pane;
    }

    bool due(uint32_t now) {
        if (page_ == Page::calibrate) return dirty_ || uint32_t(now - refreshed_) >= 100;
        using R = cntrl::PageSettings;
        if (page_ == Page::pong) { pong.advance(now); return dirty_ || uint32_t(now - refreshed_) >= 20; }
        if (dirty_) return true;
        if (page_ == Page::midi_monitor) return uint32_t(now - refreshed_) >= midi::Monitor::refresh_ms;
        if (page_ == Page::midi_looper) return uint32_t(now - refreshed_) >= 50;
        if (page_ == Page::arpeggiator) return uint32_t(now - refreshed_) >= 50;
        if (page_ == Page::cable || page_ == Page::panic) return uint32_t(now - refreshed_) >= 80;
        if (page_ == Page::status) return uint32_t(now - refreshed_) >= settings_[R::refresh];
        return page_ == Page::time && (uint32_t(now - refreshed_) >= 50 || (held_ || tap_.flash(now, settings_[R::flash_fraction])) != flashing_);
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
        if (page_ == Page::time && window == 0) { time_view(pane); return pane; }
        if (page_ == Page::menu) {
            page_heading(pane, i18n::text(i18n::Message::pages_and_tools));
            i18n::format(prompt_, sizeof(prompt_), i18n::Message::control_action, "", label(controls_.confirm), i18n::text(i18n::Message::ok));
            line(pane, 1, prompt_, vertical(64), vertical(24), settings_[R::status_font], true);
            return pane;
        }
        if (page_ == Page::language) { language_view(pane, window); return pane; }
        if (page_ == Page::pong) { pong_view(pane, window); return pane; }
        if (page_ == Page::midi_monitor) { monitor_view(pane, window, count); return pane; }
        if (page_ == Page::midi_looper) { looper_view(pane,beat_position(now_us)); return pane; }
        if (page_ == Page::arpeggiator) { arpeggiator_view(pane); return pane; }
        const unsigned pad = pane.graphics.pad, title = settings_[R::title_height];
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
                const unsigned height = vertical(48);
                line(pane, 1, panic == cntrl::Panic::Status::notes_off ? i18n::text(i18n::Message::notes_off) : i18n::text(i18n::Message::all_off),
                     (viewport_.height - height) / 2, height, settings_[R::status_font], true, i18n::text(i18n::Message::notes_off));
                pane.lines[1].graphics.foreground = 0x07e0;
            }
            return pane;
        }
        const unsigned heading_text_height = std::min(title * 3 / 2, (viewport_.height - 2 * pad) / 2);
        const unsigned heading_height = heading_text_height + 4;
        const char *heading = page_ == Page::time ? i18n::text(i18n::Message::tap_tempo) : page_ == Page::status ? i18n::text(i18n::Message::status) : page_ == Page::cable ? i18n::text(i18n::Message::midi_cable_test) : "";
#ifdef CNTRL_EMULATOR
        if (page_ == Page::status && preview_size_) heading = preview_label_;
#endif
        page_heading(pane, heading);
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
        if (page_ == Page::time) {
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
            const unsigned inset = (pane.circles[0].diameter + 2 * pane.circles[0].annulus + 1) / 2 + pad;
            pane.circles[0].x = uint16_t(std::max(0, int(viewport_.width) - int(inset)));
            pane.circles[0].y = uint16_t(std::max(0, int(viewport_.height) - int(inset)));
            pane.circles[0].color = settings_[flashing_ ? R::on_color : R::off_color];
            pane.circles[0].ring_color = settings_[R::ring_color];
        } else {
            char rows[5][64] = {};
            i18n::format(rows[0], sizeof(rows[0]), i18n::Message::cycles_patches, status.cycles, status.patches);
            char tap[24], clock[24], tap_label[32], uptime[32], git[16] = {};
            number_text(tap, sizeof(tap), status_snapshot ? status_snapshot->tap_period_ms : tap_.period(), false, true);
            number_text(clock, sizeof(clock), status_snapshot ? status_snapshot->clock_period_us : observed_clock().period_us(), true, true);
            i18n::format(rows[1], sizeof(rows[1]), i18n::Message::clock_value, clock + std::strspn(clock," "));
            std::snprintf(rows[2], sizeof(rows[2]), "RAM:%lu/%luk %lu/%luk",
                (unsigned long)(status.ram_used[0] / 1024),
                (unsigned long)((status.ram_used[0] + status.ram_free[0]) / 1024),
                (unsigned long)(status.ram_used[1] / 1024),
                (unsigned long)((status.ram_used[1] + status.ram_free[1]) / 1024));
            constexpr uint64_t gibibyte = 1024ull * 1024 * 1024;
            if (status.disk_valid) {
                uint64_t tenths = (status.disk_used * 10 + gibibyte / 2) / gibibyte;
                const bool integer = tenths >= 100;
                const auto whole = integer ? (status.disk_used + gibibyte / 2) / gibibyte : tenths / 10;
                i18n::format(rows[3], sizeof(rows[3]), i18n::Message::disk_value, (unsigned long)whole, (unsigned long)(integer ? 0 : tenths % 10), (unsigned long)((status.disk_used + status.disk_free + gibibyte / 2) / gibibyte));
                // Retain the translated label while omitting its fixed fractional field.
                if (integer) if (char *fraction = std::strstr(rows[3], ".0/"))
                    std::memmove(fraction, fraction + 2, std::strlen(fraction + 2) + 1);
            } else i18n::format(rows[3], sizeof(rows[3]), i18n::Message::disk_unknown);
            uint64_t seconds = (status_snapshot ? status_snapshot->uptime_ms : uptime_) / 1000;
            i18n::format(uptime, sizeof(uptime), i18n::Message::uptime_value, (unsigned long)(seconds / 3600), unsigned(seconds / 60 % 60), unsigned(seconds % 60));
            if (status.starts_status == StartsStatus::ready) {
                char number[21]; *std::to_chars(number, number + 20, status.starts).ptr = 0;
                i18n::format(rows[4], sizeof(rows[4]), i18n::Message::starts_value, number);
            }
            else i18n::format(rows[4], sizeof(rows[4]), i18n::Message::starts_value, status.starts_status == StartsStatus::error ? (status.starts_error[0] ? status.starts_error : i18n::text(i18n::Message::error)) : "--");
#ifdef CNTRL_GIT_COMMIT
            std::snprintf(git, sizeof(git), "Git:%07x", unsigned(status_snapshot ? status_snapshot->git_commit : CNTRL_GIT_COMMIT));
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
                        const char *value = i == 3 ? uptime + std::strspn(uptime, " ") : git;
                        if (*value) line(pane, right, value, y, height, settings_[R::status_font], false, nullptr, 0, viewport_.width, maximum_size, true);
                        line(pane, i + 2, rows[i], y, height, settings_[R::status_font], false, nullptr, 0, *value ? unsigned(pane.lines[right].x) : viewport_.width, maximum_size);
                        if (*value) pane.lines[right].y = pane.lines[i + 2].y;
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
                bool value = false;
                const char *cursor = l.text;
                for (unsigned j = 0; *cursor; ++j) {
                    uint32_t code; if (!utf8::next(cursor, code)) break;
                    if (code == ':' && !value) { value = true; continue; }
                    if (i == 1 && code == ' ' && *cursor == ' ') value = false;
                    if (value) l.color_mask |= uint64_t(1) << j;
                }
            }
            pane.lines[2].stippled = status_snapshot ? status_snapshot->clock_period_us && !status_snapshot->clock_active : observed_clock().period_us() && !observed_clock().measured();
        }
        return pane;
    }
private:
    const ClockTempo &observed_clock() const {
        if (selected_clock_) return clocks_[selected_clock_ - 1];
        if (clocks_[0].measured() || !clocks_[1].period_us()) return clocks_[0];
        return clocks_[1];
    }
    unsigned clock_input(unsigned source) const {
        const auto *input = outputs_.find(source == 1 ? "din" : "trs");
        return input && input->input && input->transport == midi::OutputTransport::physical ? input->route : ~0u;
    }
    void select_clock(unsigned source) {
        waiting_for_clock_ = false;
        if (selected_clock_ == source) return;
        selected_clock_ = source; clock_changed_ = dirty_ = true;
    }
    void select_available_clock() {
        if (source_period(selected_clock_)) { waiting_for_clock_ = false; return; }
        if ((settings_[cntrl::PageSettings::clock_options] & 2) || waiting_for_clock_) return;
        for (unsigned i = 0; i < 3; ++i) if (source_period(i)) { select_clock(i); return; }
        select_clock(0);
    }
    void time_view(cntrl::Pane &pane) {
        using R = cntrl::PageSettings;
        page_heading(pane, i18n::text(i18n::Message::time));
        char help[96];
        i18n::format(help, sizeof(help), i18n::Message::time_toggle_send, label(controls_.next));
        line(pane, 1, help, vertical(99), vertical(14), localized_face, true, nullptr, 0, 0, 14);
        i18n::format(help, sizeof(help), i18n::Message::time_cycle_source, label(controls_.next_section));
        line(pane, 5, help, vertical(114), vertical(14), localized_face, true, nullptr, 0, 0, 14);
        const char *names[] = {"Tap", "DIN", "TRS"};
        for (unsigned i = 0; i < 3; ++i) {
            char number[24], text[64];
            number_text(number, sizeof(number), source_period(i), true, true);
            std::snprintf(text, sizeof(text), "%s: %s", names[i], number + std::strspn(number, " "));
            unsigned y = vertical(27 + 18 * i), height = vertical(16);
            line(pane, 2 + i, text, y, height, settings_[R::status_font], true);
            if (i == selected_clock_) {
                const auto &text_line = pane.lines[2 + i];
                const auto bounds = fonts_.bounds(text_line.graphics.font, text_line.graphics.size, 0, text_line.text);
                const int padding = (int(height) - bounds.height) / 2 + int(vertical(2));
                int x = text_line.x + bounds.left - padding, top = text_line.y + bounds.top - padding;
                unsigned width = unsigned(bounds.width + 2 * padding), box_height = unsigned(bounds.height + 2 * padding);
                pane.rectangles[0] = {int16_t(x), int16_t(top), uint16_t(width), 1, 0x07e0, false};
                pane.rectangles[1] = {int16_t(x), int16_t(top + box_height - 1), uint16_t(width), 1, 0x07e0, false};
                pane.rectangles[2] = {int16_t(x), int16_t(top), 1, uint16_t(box_height), 0x07e0, false};
                pane.rectangles[3] = {int16_t(x + width - 1), int16_t(top), 1, uint16_t(box_height), 0x07e0, false};
            }
        }
        pane.toggle = {uint16_t(viewport_.width / 2), uint16_t(vertical(84)), true, sending_clock_, false};
    }
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
    bool clock_changed_ = false;
    unsigned beat_source_ = 0;
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
    unsigned selected_clock_ = 0;
    bool sending_clock_ = false, waiting_for_clock_ = false;
    unsigned horizontal(unsigned value) const { return value * viewport_.width / design_width; }
    unsigned vertical(unsigned value) const { return value * viewport_.height / design_height; }
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
        const bool ordinary = !preview && page_ != Page::cable && page_ != Page::status && page_ != Page::time && page_ != Page::pong && page_ != Page::calibrate;
        if (ordinary) {
            face = 17;
            maximum_size = std::min(maximum_size, index ? (page_ == Page::panic ? 24u : 12u) : 16u);
        } else if (!preview && page_ == Page::status && index) face = status_face;
        if (!preview && i18n::translated_ui && face != status_face) face = localized_face;
        l.graphics = pane.graphics; l.graphics.font = uint8_t(face);
        l.graphics.asset = preview ? 0 : i18n::identity(text);
#ifdef CNTRL_EMULATOR
        l.translation_id = uint16_t(l.graphics.asset);
#endif
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
                    if (*short_form) {
                        utf8::copy(l.text, sizeof(l.text), short_form); measure(l.text);
#ifdef CNTRL_EMULATOR
                        l.translation_short = true;
#endif
                    }
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
            const unsigned inputs[] = {controls_.next_section, controls_.next, controls_.confirm, controls_.back};
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
    cntrl::PageSettings settings_;
#ifdef CNTRL_EMULATOR
    char preview_label_[48] = {};
    unsigned preview_size_ = 0;
#endif
    cntrl::TapTempo tap_;
    cntrl::ClockTempo clocks_[2];
    Page page_ = Page::status;
    uint64_t uptime_ = 0;
    uint32_t previous_ = 0, refreshed_ = 0;
    bool dirty_ = false, flashing_ = false, held_ = false;
};

} // namespace cntrl::ui
