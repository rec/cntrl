#pragma once
#include <cntrl/hardware.h>
#include <cntrl/tempo.h>
#include <cntrl/clock_generator.h>
#include <cntrl/midi_outputs.h>
#include <cntrl/midi_monitor.h>
#include <cntrl/midi_looper.h>
#include <cntrl/cable_test.h>
#include <cntrl/panic.h>
#include "fonts.h"
#include "display.h"
#include "controls.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <charconv>

namespace cntrl::ui {
enum class StartsStatus { unavailable, ready, error };

struct PerformanceStatus {
    uint32_t ram_used[2] = {}, ram_free[2] = {};
    uint64_t disk_used = 0, disk_free = 0;
    bool disk_valid = false;
    uint64_t starts = 0;
    StartsStatus starts_status = StartsStatus::unavailable;
    char starts_error[32] = {};
    unsigned cycles = 0, patches = 0;
};

class Pages {
public:
    Pages(const Fonts &fonts, Viewport viewport) : fonts_(fonts), viewport_(viewport) {}
    void outputs(cntrl::midi::Outputs value) { outputs_ = value; }
    void controls(Controls value) { controls_ = value; }
    void viewport(Viewport value) { viewport_ = value; }
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
    enum class Page { tap, status, firmware, cable, panic, ports, expression, clock_transport, clock_tempo, keyboard, none, patch_switcher, midi_monitor, midi_looper, confirm_firmware };
    midi::Monitor *monitor = nullptr;
    midi::MidiLooper *looper = nullptr;
    char looper_prompt[96] = {};
    void layout(cntrl::PageLayout layout) {
        layout_ = layout;
        index_ = 0;
        page_ = layout.pages() ? Page(layout.page(0)[0]) : Page::none;
        dirty_ = true;
    }
    unsigned windows() const {
        return layout_.pages() && page_ != Page::patch_switcher ? layout_.page(index_)[1] : 0;
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
        clock_changed_ = true;
        dirty_ = true;
    }
    bool browse(bool reverse) {
        if (page_ == Page::confirm_firmware) return !reverse;
        if (!layout_.pages()) return false;
        for (unsigned i = 0; i < layout_.pages();) {
            ++i;
            index_ = (index_ + layout_.pages() + (reverse ? -1 : 1)) % layout_.pages();
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
        active_port_ = active; port_message_ = message; dirty_ = true;
    }
    void port_name(const char *label, bool available) { port_name_ = label; port_available_ = available; dirty_ = true; }
    void next_port_mode() {
        candidate = PortMode((unsigned(candidate) + 1) % 5);
        port_message_ = nullptr; dirty_ = true;
    }
    cntrl::ClockGenerator generator;
    unsigned clock_outputs() const { return settings_[cntrl::PageSettings::clock_outputs]; }
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
    void clock_message(unsigned source, uint8_t status, uint32_t now_us) { clock_.transport(source, status, now_us); }
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
    bool due(uint32_t now) {
        using R = cntrl::PageSettings;
        if (dirty_) return true;
        if (page_ == Page::midi_monitor) return uint32_t(now - refreshed_) >= midi::Monitor::refresh_ms;
        if (page_ == Page::midi_looper) return uint32_t(now - refreshed_) >= 50;
        if (page_ == Page::status || clock_page()) return uint32_t(now - refreshed_) >= settings_[R::refresh];
        return page_ == Page::tap && (held_ || tap_.flash(now, settings_[R::flash_fraction])) != flashing_;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(page_view)
#endif
    cntrl::Pane view(uint32_t now, uint32_t now_us, const PerformanceStatus &status, const cntrl::CableTest::Status &cable = {}, cntrl::Panic::Status panic = cntrl::Panic::Status::ready, unsigned window = 0, unsigned count = 1) {
        using R = cntrl::PageSettings;
        dirty_ = false; refreshed_ = now;
        cntrl::Pane pane;
        pane.graphics.background = settings_[R::background];
        pane.graphics.foreground = 0; pane.graphics.pad = uint8_t(settings_[R::padding]);
        if (page_ == Page::midi_monitor) { monitor_view(pane, window, count); return pane; }
        if (page_ == Page::midi_looper) { looper_view(pane,beat_position(now_us)); return pane; }
        const unsigned pad = pane.graphics.pad, title = settings_[R::title_height];
        if (page_ == Page::confirm_firmware) {
            const unsigned height = title * 3 / 2;
            line(pane, 0, prompt("%s for stock firmware.", controls_.next_page), viewport_.height / 2 - height, height, settings_[R::title_font], true);
            line(pane, 1, prompt("%s to cancel", controls_.confirm), viewport_.height / 2, height, settings_[R::title_font], true, nullptr, 0, viewport_.width, pane.lines[0].graphics.size);
            return pane;
        }
        const unsigned heading_text_height = std::min(title * 3 / 2, (viewport_.height - 2 * pad) / 2);
        const unsigned heading_height = heading_text_height + 4;
        const char *heading = page_ == Page::clock_transport ? "MIDI Clock" : page_ == Page::clock_tempo ? "Clock Tempo" : page_ == Page::tap ? "Tap Tempo" : page_ == Page::status ? "Status" : page_ == Page::cable ? "MIDI Cable Test" : page_ == Page::panic ? "Panic" : page_ == Page::ports ? "TRS Ports" : page_ == Page::keyboard ? "Keyboard" : page_ == Page::expression ? "Expression" : "Firmware";
        line(pane, 0, heading, pad + 2, heading_text_height, settings_[R::title_font], true, "MIDI Cable Test");
        pane.heading_band = true;
        auto &header = pane.lines[0];
        header.graphics.line_height += 4;
        header.graphics.foreground = 0xffff; header.graphics.background = 0;
        header.graphics.top = header.y; header.y = 0;
        if (clock_page()) {
            const unsigned font = settings_[R::status_font];
            char tempo[32], detail[64];
            std::snprintf(tempo, sizeof(tempo), "%u BPM", generator.bpm());
            if (page_ == Page::clock_transport) {
                const char *state = external_ ? (clock_.active() ? "External: receiving" : "External: waiting")
                    : !clock_outputs() ? "No outputs configured" : !available_outputs_ ? "Outputs offline" : generator.running() ? "Internal: running" : "Internal: stopped";
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
                line(pane, 4, prompt("%s Start  %s Continue", controls_.previous, controls_.next), vertical(82), vertical(16), font, true);
                line(pane, 5, prompt("%s Stop  %s Source", controls_.confirm, controls_.next_section), vertical(102), vertical(16), font, true);
            } else {
                std::snprintf(detail, sizeof(detail), "Slot %u: %u BPM", slot_ + 1, settings_.words[R::slot1 + slot_]);
                line(pane, 1, detail, vertical(28), vertical(18), font, true);
                line(pane, 2, tempo, vertical(50), vertical(24), font, true);
                line(pane, 3, prompt("%s -1   %s +1   %s Slot", controls_.previous, controls_.next, controls_.confirm), vertical(80), vertical(17), font, true);
                std::snprintf(detail, sizeof(detail), "Late %uus  Lost %u", generator.late_us, generator.skipped + generator.blocked);
                line(pane, 4, detail, vertical(102), vertical(16), font, true);
            }
            return pane;
        }
        if (page_ == Page::cable) {
            const bool active = cable.enabled;
            const unsigned font = settings_[R::status_font];
            line(pane, 1, "Out", vertical(32), vertical(18), font, true, nullptr, horizontal(56), horizontal(100));
            line(pane, 2, "In", vertical(32), vertical(18), font, true, nullptr, horizontal(106), horizontal(154));
            unsigned rows = std::max(cable.output_count, cable.input_count);
            if (rows > cntrl::max_lines - 3) rows = cntrl::max_lines - 3;
            for (unsigned i = 0; i < rows; ++i) {
                unsigned y = vertical(59 + i * 52 / std::max(1u, rows));
                const char *label = i < cable.output_count ? cable.connections[i].label : "Input";
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
            line(pane, 1, selected_shortcut.name[0] ? selected_shortcut.name : "No shortcuts", vertical(38), vertical(24), font, true);
            line(pane, 2, shortcut_held ? "Held" : "Ready", vertical(66), vertical(20), font, true);
            line(pane, 3, prompt("%s/%s: Select", controls_.previous, controls_.next), vertical(91), vertical(16), font, true);
            line(pane, 4, prompt("Hold %s: Send", controls_.confirm), vertical(109), vertical(16), font, true);
            return pane;
        }
        if (page_ == Page::expression) {
            const unsigned font = settings_[R::status_font];
            for (unsigned i = 0; i < 3; ++i)
                line(pane, i + 1, expression_lines_[i], vertical(30 + 24 * i), vertical(20), font, true);
            line(pane, 4, prompt("%s: Jack   %s: Cancel", controls_.previous, controls_.next), vertical(105), vertical(18), font, true);
            return pane;
        }
        if (page_ == Page::ports) {
            constexpr const char *names[] = {"Off", "MIDI Type A", "MIDI Type B", "Expression", "Aux switch"};
            char current[40];
            if (port_available_) std::snprintf(current, sizeof(current), "%.23s: %s", port_name_, names[unsigned(active_port_)]);
            else std::snprintf(current, sizeof(current), "%s", port_name_);
            const unsigned font = settings_[R::status_font];
            line(pane, 1, current, vertical(34), vertical(22), font, true);
            line(pane, 2, port_message_ ? port_message_ : names[unsigned(candidate)], vertical(60), vertical(22), font, true);
            if (port_available_) {
                line(pane, 3, prompt("%s: Jack   %s: Mode", controls_.previous, controls_.next), vertical(87), vertical(17), font, true);
                line(pane, 4, prompt("%s: Apply", controls_.confirm), vertical(106), vertical(17), font, true);
            }
            return pane;
        }
        if (page_ == Page::panic) {
            line(pane, 1, prompt("Press %s", controls_.confirm), vertical(40), vertical(22), settings_[R::status_font], true);
            line(pane, 2, "Hold to repeat", vertical(66), vertical(22), settings_[R::status_font], true);
            line(pane, 3, panic == cntrl::Panic::Status::sending ? "Sending" : panic == cntrl::Panic::Status::incomplete ? "Incomplete" : "Ready", vertical(100), vertical(24), settings_[R::status_font], true);
            return pane;
        }
        if (page_ == Page::firmware) {
            const unsigned label_height = std::min(title * 2, (viewport_.height - 2 * pad) / 4);
            line(pane, 1, "Use stock", viewport_.height - pad - label_height * 2, label_height, 0, true);
            line(pane, 2, "firmware", viewport_.height - pad - label_height, label_height, 0, true);
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
            std::snprintf(rows[0], sizeof(rows[0]), "Cycles: %u  Patches: %u", status.cycles, status.patches);
            char tap[24], clock[24], tap_label[32];
            number_text(tap, sizeof(tap), tap_.period(), false, true);
            number_text(clock, sizeof(clock), clock_.period_us(), true, true);
            std::snprintf(rows[1], sizeof(rows[1]), "Clock: %s", clock);
            std::snprintf(rows[2], sizeof(rows[2]), "RAM: %lu/%luk %lu/%luk",
                (unsigned long)(status.ram_used[0] / 1024),
                (unsigned long)((status.ram_used[0] + status.ram_free[0]) / 1024),
                (unsigned long)(status.ram_used[1] / 1024),
                (unsigned long)((status.ram_used[1] + status.ram_free[1]) / 1024));
            constexpr uint64_t gibibyte = 1024ull * 1024 * 1024;
            if (status.disk_valid) {
                uint64_t tenths = (status.disk_used * 10 + gibibyte / 2) / gibibyte;
                std::snprintf(rows[3], sizeof(rows[3]), "Disk:%lu.%lu/%luG",
                    (unsigned long)(tenths / 10), (unsigned long)(tenths % 10),
                    (unsigned long)((status.disk_used + status.disk_free + gibibyte / 2) / gibibyte));
            } else std::snprintf(rows[3], sizeof(rows[3]), "Disk:--/--G");
            uint64_t seconds = uptime_ / 1000;
            auto disk_length = std::strlen(rows[3]);
            std::snprintf(rows[3] + disk_length, sizeof(rows[3]) - disk_length, " Up:%lu:%02u:%02u",
                (unsigned long)(seconds / 3600), unsigned(seconds / 60 % 60), unsigned(seconds % 60));
            if (status.starts_status == StartsStatus::ready) {
                char number[21]; *std::to_chars(number, number + 20, status.starts).ptr = 0;
                std::snprintf(rows[4], sizeof(rows[4]), "Starts: %s", number);
            }
            else std::snprintf(rows[4], sizeof(rows[4]), "Starts: %s",
                status.starts_status == StartsStatus::error ? (status.starts_error[0] ? status.starts_error : "error") : "--");
            std::snprintf(tap_label, sizeof(tap_label), "Tap: %s", tap);
            const unsigned body_top = pad + heading_height;
            const unsigned body_height = viewport_.height - pad - body_top;
            auto body = [&](unsigned maximum_size) {
                for (unsigned i = 0; i < 5; ++i) {
                    const unsigned y = body_top + body_height * i / 5;
                    const unsigned height = body_height * (i + 1) / 5 - body_height * i / 5;
                    if (i == 1) {
                        line(pane, 2, rows[i], y, height, settings_[R::status_font], false, nullptr, 0, viewport_.width / 2, maximum_size);
                        line(pane, 3, tap_label, y, height, settings_[R::status_font], false, nullptr, viewport_.width / 2, viewport_.width, maximum_size);
                    } else {
                        line(pane, i == 0 ? 1 : i + 2, rows[i], y, height, settings_[R::status_font], false, nullptr, 0, viewport_.width, maximum_size);
                    }
                }
            };
            body(96);
            unsigned shared_size = pane.lines[1].graphics.size;
            for (unsigned i = 2; i <= 6; ++i) shared_size = std::min(shared_size, unsigned(pane.lines[i].graphics.size));
            body(shared_size);
            for (unsigned i = 1; i <= 6; ++i) {
                auto &l = pane.lines[i];
                l.graphics.foreground = 0x0011; // Dark blue, #00008B.
                l.alternate_color = 0x0400; // Green, #008000.
                bool value = false;
                for (unsigned j = 0; l.text[j]; ++j) {
                    if (l.text[j] == ':' && !value) { value = true; continue; }
                    if ((i == 1 && l.text[j] == ' ' && l.text[j + 1] == ' ') || (i == 5 && l.text[j] == ' ')) value = false;
                    if (value) l.color_mask |= uint64_t(1) << j;
                }
            }
            clock_.poll(now_us);
            pane.lines[2].stippled = clock_.period_us() && !clock_.active();
        }
        return pane;
    }
private:
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(looper_view)
#endif
    void looper_view(Pane &pane, midi::BeatPosition clock) {
        using R = cntrl::PageSettings;
        line(pane,0,"MIDI Looper",2,18,settings_[R::title_font],true);
        const char *states[] = {"Empty","Armed","Recording","Closing","Playing","Overdub armed","Overdubbing","Preparing","Stopped","Launch armed","Undo pending","Loading"};
        const char *errors[] = {"","Busy","Clock unavailable","Capacity exceeded","Memory unavailable","Late boundary","Output retired","File error","Input changed"};
        char text[96];
        std::snprintf(text,sizeof(text),"%s%s%s",looper ? states[unsigned(looper->state())] : "Unavailable",
            looper && looper->error() != midi::MidiLooper::Error::none ? ": " : "",looper ? errors[unsigned(looper->error())] : "");
        line(pane,1,text,24,14,settings_[R::status_font],true);
        uint64_t length = looper ? looper->length() : 0;
        uint64_t position = looper ? looper->position(clock) : 0;
        std::snprintf(text,sizeof(text),"%llu.%02u / %llu.%02u beats",(unsigned long long)(position >> 32),unsigned((position & 0xffffffffu) * 100 >> 32),(unsigned long long)(length >> 32),unsigned((length & 0xffffffffu) * 100 >> 32));
        line(pane,2,text,42,12,settings_[R::status_font],true);
        std::snprintf(text,sizeof(text),"%u/%u events%s%s",looper ? looper->events() : 0,looper ? looper->capacity() : 0,!clock.running ? " Clock waiting" : looper && looper->unsaved() ? " Unsaved" : "",looper && looper->backward() ? " Backward" : " Forward");
        line(pane,3,text,58,12,settings_[R::status_font],true);
        line(pane,4,looper_prompt[0] ? looper_prompt : looper && looper->direction_pending() ? "Direction change pending" : "",74,12,settings_[R::status_font],true);
        line(pane,5,prompt("%s Rec %s Play %s Undo",controls_.previous,controls_.next,controls_.confirm),90,12,settings_[R::status_font],true);
        line(pane,6,prompt("%s Clear %s More",controls_.previous_section,controls_.next_section),106,12,settings_[R::status_font],true);
    }
    void monitor_view(Pane &pane, unsigned window, unsigned count) {
        using Direction = midi::Monitor::Direction;
        using R = cntrl::PageSettings;
        const Direction direction = count == 1 || (count == 3 && window == 1) ? Direction::both : window == 0 ? Direction::incoming : Direction::outgoing;
        const char *heading = direction == Direction::both ? "Combined" : direction == Direction::incoming ? "Incoming" : "Outgoing";
        const unsigned pad = pane.graphics.pad;
        const unsigned title = std::min(unsigned(settings_[R::title_height]) + 4, viewport_.height / 4);
        line(pane, 0, heading, pad, title, settings_[R::title_font], true);
        pane.heading_band = true; pane.lines[0].graphics.foreground = 0xffff; pane.lines[0].graphics.background = 0;
        pane.lines[0].graphics.line_height = uint16_t(title + 2 * pad); pane.lines[0].y = 0;
        const unsigned row_height = 10, body_top = title + 2 * pad + row_height;
        const unsigned bottom = viewport_.height > pad + row_height ? viewport_.height - pad - row_height : 0;
        const unsigned rows = std::min(4u, bottom > body_top ? (bottom - body_top) / (3 * row_height) : 0u);
        char text[64];
        if (!monitor || !monitor->active()) std::snprintf(text, sizeof(text), "Unavailable: memory");
        else std::snprintf(text, sizeof(text), "%s%s Hist-%llu", monitor->frozen() ? "Frozen" : "Live", monitor->advanced() ? " advanced" : "", (unsigned long long)monitor->overwritten());
        line(pane, 1, text, body_top - row_height, row_height, settings_[R::status_font], false, nullptr, 0, 0, 8);
        const midi::Monitor::Entry *entries[4] = {}; unsigned used = 0;
        if (monitor && monitor->active()) for (uint64_t position = monitor->cursor(); position > monitor->oldest() && used < rows;) {
            const auto *e = monitor->entry(--position);
            if (e && (direction == Direction::both || e->direction == direction || e->direction == Direction::both)) entries[used++] = e;
        }
        if (!used) line(pane, 2, direction == Direction::both ? "No traffic" : direction == Direction::incoming ? "No input" : "No output", body_top, row_height, settings_[R::status_font], false, nullptr, 0, 0, 8);
        for (unsigned i = 0; i < used; ++i) {
            const auto &e = *entries[used - i - 1];
            const auto *endpoint = outputs_.route(e.route);
            const char *alias = endpoint ? endpoint->name : "unknown";
            const char *direction_label = e.direction == Direction::incoming ? "I" : e.direction == Direction::outgoing ? "O" : "-";
            std::snprintf(text, sizeof(text), "%llu.%03u %s%s %.12s%s/%u", (unsigned long long)(e.time / 1000000), unsigned(e.time / 1000 % 1000), direction_label,
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
            if (e.flags & midi::Monitor::running) std::strcat(text, " (running)");
            line(pane, first + 2, text, y + 2 * row_height, row_height, settings_[R::status_font], false, nullptr, 0, 0, 8);
        }
        const char *help = monitor && monitor->frozen() ? prompt("%s/%s History %s Live", controls_.previous, controls_.next, controls_.confirm) : prompt("%s Freeze", controls_.confirm);
        line(pane, 14, help, bottom, row_height, settings_[R::status_font], false, nullptr, 0, 0, 8);
    }
    bool clock_changed_ = false, beat_external_ = false;
    uint32_t beat_restart_ = 0, beat_source_restart_ = 0;
    static constexpr unsigned design_width = 5 * 32, design_height = 4 * 32;
    Controls controls_;
    char prompt_[64] = {};
    template<class... Indices> const char *prompt(const char *format, Indices... indices) {
        if (((!label(indices)[0]) || ...)) return "";
        std::snprintf(prompt_, sizeof(prompt_), format, label(indices)...); return prompt_;
    }
    const char *label(unsigned index) const { return index < controls_.count ? controls_.bindings[index].label : ""; }
    cntrl::midi::Outputs outputs_;
    const Fonts &fonts_;
    Viewport viewport_;
    cntrl::PageLayout layout_;
    unsigned index_ = 0;
    PortMode active_port_ = PortMode::off;
    const char *port_message_ = nullptr;
    const char *port_name_ = "No configurable ports";
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
        if (fixed && value / scale >= limit) { std::snprintf(text, length, "OVER"); return; }
        if (decimals) std::snprintf(text, length, "%*lu.%0*lu", fixed ? int(digits) : 1,
            (unsigned long)(value / scale), int(decimals), (unsigned long)(value % scale));
        else std::snprintf(text, length, "%*lu", fixed ? int(digits) : 1, (unsigned long)value);
    }
    void line(cntrl::Pane &pane, unsigned index, const char *text, unsigned y, unsigned height,
              unsigned face, bool centered, const char *sample = nullptr, unsigned left_edge = 0, unsigned right_edge = 0, unsigned maximum_size = 96) {
        auto &l = pane.lines[index];
        std::snprintf(l.text, sizeof(l.text), "%s", text);
        l.graphics = pane.graphics; l.graphics.font = uint8_t(face);
        int left = 0, top = 0, width = 0, ink_height = 0;
        const char *fitting = sample ? sample : l.text;
        if (!right_edge) right_edge = viewport_.width;
        unsigned minimum_size = 0;
        for (unsigned size = 8; size <= maximum_size && size <= 96; ++size)
            if (fonts_.supports(face, size)) { minimum_size = size; break; }
        for (unsigned size = 96; size >= 8; --size) {
            if (size > maximum_size || !fonts_.supports(face, size)) continue;
            auto measure = [&](const char *value) {
                const auto b = fonts_.bounds(face, size, 0, value);
                left = b.left; top = b.top; width = b.width; ink_height = b.height;
            };
            measure(fitting);
            const int available = std::max(0, int(right_edge - left_edge) - 2 * int(pane.graphics.pad));
            if ((width > available || ink_height > int(height)) && size > minimum_size) continue;
            l.graphics.size = uint8_t(size); l.graphics.line_height = uint16_t(height); l.graphics.top = 0;
            if (!sample && width > available) {
                unsigned n = unsigned(std::strlen(l.text));
                while (n > 3 && width > available) {
                    --n; std::memcpy(l.text + n - 3, "...", 4); measure(l.text);
                }
            }
            if (centered && sample) measure(l.text);
            l.x = uint16_t(centered ? (int(left_edge + right_edge) - width) / 2 - left : sample ? int(right_edge - pane.graphics.pad) - width - left : left_edge + pane.graphics.pad);
            l.y = int16_t(int(y) + (int(height) - ink_height) / 2 - top);
            return;
        }
    }

    char expression_lines_[3][40] = {};
    cntrl::PageSettings settings_;
    cntrl::TapTempo tap_;
    cntrl::ClockTempo clock_;
    Page page_ = Page::status;
    uint64_t uptime_ = 0;
    uint32_t previous_ = 0, refreshed_ = 0;
    bool dirty_ = false, flashing_ = false, held_ = false;
};

} // namespace cntrl::ui
