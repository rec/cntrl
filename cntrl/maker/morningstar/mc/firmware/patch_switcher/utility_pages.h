#pragma once
#include <cntrl/hardware.h>
#include <cntrl/tempo.h>
#include <cntrl/clock_generator.h>
#include <cntrl/midi_outputs.h>
#include <cntrl/cable_test.h>
#include <cntrl/panic.h>
#include "show_graphics.h"
#include "startup_counter.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

struct PerformanceStatus {
    uint32_t ram_used[2] = {}, ram_free[2] = {};
    uint64_t disk_used = 0, disk_free = 0;
    bool disk_valid = false;
    uint64_t starts = 0;
    StartupCounter::Status starts_status = StartupCounter::Status::unavailable;
    unsigned cycles = 0, patches = 0;
};

class UtilityPages {
public:
    enum class Page { tap, status, firmware, cable, panic, ports, expression, clock_transport, clock_tempo, keyboard, confirm_firmware };
    void configure(const cntrl::UtilityPageSettings &settings) {
        generator.stop();
        slot_ = 0;
        settings_ = settings;
        shortcut = 0;
        using R = cntrl::UtilityPageSettings;
        external_ = settings[R::clock_external];
        recall_slot();
        tap_.configure(settings[R::tap_intervals], settings[R::minimum_taps], settings[R::minimum_interval], settings[R::reset_gap]);
        clock_.configure(settings[R::clocks_per_quarter], settings[R::clock_beats], uint32_t(settings[R::clock_timeout]) * 1000);
        dirty_ = true;
    }
    bool browse(bool reverse, const char *setup) {
        if (page_ == Page::confirm_firmware) return !reverse;
        do {
            constexpr unsigned count = unsigned(Page::confirm_firmware);
            page_ = Page((unsigned(page_) + (reverse ? count - 1 : 1)) % count);
#if defined(CNTRL_CABLE_TEST) && !CNTRL_CABLE_TEST
            if (page_ == Page::cable) continue;
#endif
#if defined(CNTRL_PANIC) && !CNTRL_PANIC
            if (page_ == Page::panic) continue;
#endif
            break;
        } while (true);
        if (page_ == Page::status) std::snprintf(setup_, sizeof(setup_), "%.63s", setup);
        dirty_ = true;
        return false;
    }
    using PortMode = cntrl::midi::TrsMode;
    unsigned jack = 0;
    PortMode candidate = PortMode::off;
    void port_status(PortMode active, const char *message = nullptr) {
        active_port_ = active; port_message_ = message; dirty_ = true;
    }
    void next_port_mode() {
        candidate = PortMode((unsigned(candidate) + 1) % 5);
        port_message_ = nullptr; dirty_ = true;
    }
    cntrl::ClockGenerator generator;
    unsigned clock_outputs() const { return settings_[cntrl::UtilityPageSettings::clock_outputs]; }
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
        using R = cntrl::UtilityPageSettings;
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
    void revisit(const char *setup) {
        if (page_ == Page::status) std::snprintf(setup_, sizeof(setup_), "%.63s", setup);
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
        using R = cntrl::UtilityPageSettings;
        if (dirty_) return true;
        if (page_ == Page::status || clock_page()) return uint32_t(now - refreshed_) >= settings_[R::refresh];
        return page_ == Page::tap && (held_ || tap_.flash(now, settings_[R::flash_fraction])) != flashing_;
    }
    cntrl::Pane view(uint32_t now, uint32_t now_us, const PerformanceStatus &status, const cntrl::CableTest::Status &cable = {}, cntrl::Panic::Status panic = cntrl::Panic::Status::ready) {
        using R = cntrl::UtilityPageSettings;
        dirty_ = false; refreshed_ = now;
        cntrl::Pane pane;
        pane.graphics.background = settings_[R::background];
        pane.graphics.foreground = 0; pane.graphics.pad = settings_[R::padding];
        const unsigned pad = pane.graphics.pad, title = settings_[R::title_height];
        if (page_ == Page::confirm_firmware) {
            const unsigned height = title * 3 / 2;
            line(pane, 0, "F for stock firmware.", 64 - height, height, settings_[R::title_font], true);
            line(pane, 1, "C to cancel", 64, height, settings_[R::title_font], true, nullptr, 0, 160, pane.lines[0].graphics.size);
            return pane;
        }
        const unsigned heading_text_height = std::min(title * 3 / 2, (128 - 2 * pad) / 2);
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
                line(pane, 1, state, 30, 19, font, true);
                if (external_) number_text(tempo, sizeof(tempo), clock_.period_us(), true, true);
                line(pane, 2, tempo, 48, 19, font, true);
                detail[0] = 0;
                auto output = [&](const cntrl::midi::OutputConnection &port) {
                    if (!(clock_outputs() & (1u << port.route))) return;
                    auto used = std::strlen(detail);
                    std::snprintf(detail + used, sizeof(detail) - used, "%s%s%s", used ? " " : "",
                        port.label, available_outputs_ & (1u << port.route) ? "" : "*");
                };
                for (const auto &port : cntrl::midi::output_connections) output(port);
                for (const auto &port : cntrl::midi::usb_output_connections) output(port);
                line(pane, 3, detail, 67, 14, font, true);
                line(pane, 4, "A Start  B Continue", 82, 16, font, true);
                line(pane, 5, "C Stop  E Source", 102, 16, font, true);
            } else {
                std::snprintf(detail, sizeof(detail), "Slot %u: %u BPM", slot_ + 1, settings_.words[R::slot1 + slot_]);
                line(pane, 1, detail, 28, 18, font, true);
                line(pane, 2, tempo, 50, 24, font, true);
                line(pane, 3, "A -1   B +1   C Slot", 80, 17, font, true);
                std::snprintf(detail, sizeof(detail), "Late %uus  Lost %u", generator.late_us, generator.skipped + generator.blocked);
                line(pane, 4, detail, 102, 16, font, true);
            }
            return pane;
        }
        if (page_ == Page::cable) {
            const bool active = cable.enabled;
            const unsigned font = settings_[R::status_font];
            line(pane, 1, "Out", 32, 18, font, true, nullptr, 56, 100);
            line(pane, 2, "In", 32, 18, font, true, nullptr, 106, 154);
            line(pane, 3, "DIN", 59, 18, font, false, nullptr, 8, 54);
            line(pane, 4, "TRS", 85, 18, font, false, nullptr, 8, 54);
            for (unsigned i = 1; i <= 4; ++i)
                pane.lines[i].graphics.foreground = active ? 0 : 0x8410;
            using Source = cntrl::CableTest::Source;
            for (unsigned i = 0; i < 4; ++i) {
                Source source = i < 2 ? (cable.sending[i] ? (i == 0 ? Source::din : Source::trs) : Source::none) : cable.inputs[i - 2];
                uint16_t color = !active || source == Source::none ? 0x8410 : source == Source::din ? 0x001f : 0x07e0;
                pane.circles[i] = {uint16_t(i < 2 ? 78 : 130), uint16_t(i % 2 ? 94 : 68), 12, 0, color, 0};
            }
            pane.toggle = {128, 110, true, cable.enabled, false};
            return pane;
        }
        if (page_ == Page::keyboard) {
            const auto shortcut = keyboard_shortcut();
            const unsigned font = settings_[R::status_font];
            line(pane, 1, shortcut.name[0] ? shortcut.name : "No shortcuts", 38, 24, font, true);
            line(pane, 2, shortcut_held ? "Held" : "Ready", 66, 20, font, true);
            line(pane, 3, "A/B: Select", 91, 16, font, true);
            line(pane, 4, "Hold C: Send", 109, 16, font, true);
            return pane;
        }
        if (page_ == Page::expression) {
            const unsigned font = settings_[R::status_font];
            for (unsigned i = 0; i < 3; ++i)
                line(pane, i + 1, expression_lines_[i], 30 + 24 * i, 20, font, true);
            line(pane, 4, "A: Jack   B: Cancel", 105, 18, font, true);
            return pane;
        }
        if (page_ == Page::ports) {
            constexpr const char *names[] = {"Off", "MIDI Type A", "MIDI Type B", "Expression", "Aux switch"};
            char current[40];
            std::snprintf(current, sizeof(current), "Jack %u: %s", jack + 1, names[unsigned(active_port_)]);
            const unsigned font = settings_[R::status_font];
            line(pane, 1, current, 34, 22, font, true);
            line(pane, 2, port_message_ ? port_message_ : names[unsigned(candidate)], 60, 22, font, true);
            line(pane, 3, "A: Jack   B: Mode", 87, 17, font, true);
            line(pane, 4, "C: Apply", 106, 17, font, true);
            return pane;
        }
        if (page_ == Page::panic) {
            line(pane, 1, "Press C", 40, 22, settings_[R::status_font], true);
            line(pane, 2, "Hold to repeat", 66, 22, settings_[R::status_font], true);
            line(pane, 3, panic == cntrl::Panic::Status::sending ? "Sending" : panic == cntrl::Panic::Status::incomplete ? "Incomplete" : "Ready", 100, 24, settings_[R::status_font], true);
            return pane;
        }
        if (page_ == Page::firmware) {
            const unsigned label_height = std::min(title * 2, (128 - 2 * pad) / 4);
            line(pane, 1, "Use stock", 128 - pad - label_height * 2, label_height, 0, true);
            line(pane, 2, "firmware", 128 - pad - label_height, label_height, 0, true);
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
            pane.circles[0].x = 80;
            unsigned start = pad + heading_height + settings_[R::tempo_height];
            pane.circles[0].y = (start + 128 - pad) / 2;
            pane.circles[0].color = settings_[flashing_ ? R::on_color : R::off_color];
            pane.circles[0].ring_color = settings_[R::ring_color];
        } else {
            char rows[6][64] = {};
            std::snprintf(rows[0], sizeof(rows[0]), "Setup: %.56s", setup_);
            std::snprintf(rows[1], sizeof(rows[1]), "Cycles: %u  Patches: %u", status.cycles, status.patches);
            char tap[24], clock[24], tap_label[32];
            number_text(tap, sizeof(tap), tap_.period(), false, true);
            number_text(clock, sizeof(clock), clock_.period_us(), true, true);
            std::snprintf(rows[2], sizeof(rows[2]), "Clock: %s", clock);
            std::snprintf(rows[3], sizeof(rows[3]), "RAM: %lu/%luk %lu/%luk",
                (unsigned long)(status.ram_used[0] / 1024),
                (unsigned long)((status.ram_used[0] + status.ram_free[0]) / 1024),
                (unsigned long)(status.ram_used[1] / 1024),
                (unsigned long)((status.ram_used[1] + status.ram_free[1]) / 1024));
            constexpr uint64_t gibibyte = 1024ull * 1024 * 1024;
            if (status.disk_valid) {
                uint64_t tenths = (status.disk_used * 10 + gibibyte / 2) / gibibyte;
                std::snprintf(rows[4], sizeof(rows[4]), "Disk:%lu.%lu/%luG",
                    (unsigned long)(tenths / 10), (unsigned long)(tenths % 10),
                    (unsigned long)((status.disk_used + status.disk_free + gibibyte / 2) / gibibyte));
            } else std::snprintf(rows[4], sizeof(rows[4]), "Disk:--/--G");
            uint64_t seconds = uptime_ / 1000;
            unsigned disk_length = std::strlen(rows[4]);
            std::snprintf(rows[4] + disk_length, sizeof(rows[4]) - disk_length, " Up:%lu:%02u:%02u",
                (unsigned long)(seconds / 3600), unsigned(seconds / 60 % 60), unsigned(seconds % 60));
            if (status.starts_status == StartupCounter::Status::ready)
                std::snprintf(rows[5], sizeof(rows[5]), "Starts: %llu", (unsigned long long)status.starts);
            else std::snprintf(rows[5], sizeof(rows[5]), "Starts: %s",
                status.starts_status == StartupCounter::Status::error ? "error" : "--");
            std::snprintf(tap_label, sizeof(tap_label), "Tap: %s", tap);
            const unsigned body_top = pad + heading_height;
            const unsigned body_height = 128 - pad - body_top;
            auto body = [&](unsigned maximum_size) {
                for (unsigned i = 0; i < 6; ++i) {
                    const unsigned y = body_top + body_height * i / 6;
                    const unsigned height = body_height * (i + 1) / 6 - body_height * i / 6;
                    if (i == 2) {
                        line(pane, 3, rows[i], y, height, settings_[R::status_font], false, nullptr, 0, 80, maximum_size);
                        line(pane, 4, tap_label, y, height, settings_[R::status_font], false, nullptr, 80, 160, maximum_size);
                    } else {
                        line(pane, i < 2 ? i + 1 : i + 2, rows[i], y, height,
                            settings_[R::status_font], i == 0, nullptr, 0, 160, maximum_size);
                    }
                }
            };
            body(96);
            unsigned shared_size = pane.lines[1].graphics.size;
            for (unsigned i = 2; i <= 7; ++i) shared_size = std::min(shared_size, unsigned(pane.lines[i].graphics.size));
            body(shared_size);
            for (unsigned i = 1; i <= 7; ++i) {
                auto &l = pane.lines[i];
                l.graphics.foreground = 0x0011; // Dark blue, #00008B.
                l.alternate_color = 0x0400; // Green, #008000.
                bool value = false;
                for (unsigned j = 0; l.text[j]; ++j) {
                    if (l.text[j] == ':' && !value) { value = true; continue; }
                    if ((i == 2 && l.text[j] == ' ' && l.text[j + 1] == ' ') || (i == 6 && l.text[j] == ' ')) value = false;
                    if (value) l.color_mask |= uint64_t(1) << j;
                }
            }
            clock_.poll(now_us);
            pane.lines[3].stippled = clock_.period_us() && !clock_.active();
        }
        return pane;
    }
private:
    PortMode active_port_ = PortMode::off;
    const char *port_message_ = nullptr;
    unsigned available_outputs_ = 0, slot_ = 0;
    bool external_ = false;
    void recall_slot() {
        unsigned bpm = settings_.words[cntrl::UtilityPageSettings::slot1 + slot_];
        if (bpm) generator.set_tempo((60000000u + bpm / 2) / bpm);
    }
    void number_text(char *text, unsigned length, uint64_t period, bool microseconds, bool fixed) const {
        using R = cntrl::UtilityPageSettings;
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
              unsigned face, bool centered, const char *sample = nullptr, unsigned left_edge = 0, unsigned right_edge = 160, unsigned maximum_size = 96) {
        auto &l = pane.lines[index];
        std::snprintf(l.text, sizeof(l.text), "%s", text);
        l.graphics = pane.graphics; l.graphics.font = face;
        l.heavier = true;
        const unsigned sizes[] = {96, 72, 60, 48, 40, 32, 28, 24, 20, 18, 16, 14, 13, 12, 11, 10, 9, 8};
        ST7735_t3 measure(255, 255, 255); measure.setTextSize(1); measure.setTextWrap(false);
        int16_t left = 0, top = 0; uint16_t width = 0, ink_height = 0;
        const char *fitting = sample ? sample : l.text;
        for (unsigned size : sizes) {
            if (size > maximum_size) continue;
            const auto *font = ShowGraphics::native_font(face, size);
            if (!font) continue;
            measure.setFont(*font); measure.getTextBounds(fitting, 0, 0, &left, &top, &width, &ink_height);
            if ((width > right_edge - left_edge - 2 * pane.graphics.pad || ink_height + 1 > height) && size > (face >= 8 ? 12u : 8u)) continue;
            l.graphics.size = size; l.graphics.line_height = height; l.graphics.top = 0;
            if (!sample && width > right_edge - left_edge - 2 * pane.graphics.pad) {
                unsigned n = unsigned(std::strlen(l.text));
                while (n > 3 && width > right_edge - left_edge - 2 * pane.graphics.pad) {
                    --n; std::memcpy(l.text + n - 3, "...", 4);
                    measure.getTextBounds(l.text, 0, 0, &left, &top, &width, &ink_height);
                }
            }
            if (centered && sample)
                measure.getTextBounds(l.text, 0, 0, &left, &top, &width, &ink_height);
            l.x = centered ? (left_edge + right_edge - width) / 2 - left : sample ? right_edge - pane.graphics.pad - width - left : left_edge + pane.graphics.pad;
            l.y = int(y) + (int(height) - ink_height - 1) / 2 - top;
            return;
        }
    }
    char expression_lines_[3][40] = {};
    cntrl::UtilityPageSettings settings_;
    cntrl::TapTempo tap_;
    cntrl::ClockTempo clock_;
    Page page_ = Page::status;
    char setup_[64] = {};
    uint64_t uptime_ = 0;
    uint32_t previous_ = 0, refreshed_ = 0;
    bool dirty_ = false, flashing_ = false, held_ = false;
};
