#pragma once
#include <cntrl/hardware.h>
#include <cntrl/tempo.h>
#include "show_graphics.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

struct PerformanceStatus {
    uint32_t ram_used[2] = {}, ram_free[2] = {};
    uint64_t disk_used = 0, disk_free = 0;
    bool disk_valid = false;
    unsigned cycles = 0, patches = 0;
};

class PerformancePages {
public:
    enum class Page { tap, status, firmware, confirm_firmware };
    void configure(const cntrl::RightPageSettings &settings) {
        settings_ = settings;
        using R = cntrl::RightPageSettings;
        tap_.configure(settings[R::tap_intervals], settings[R::minimum_taps], settings[R::minimum_interval], settings[R::reset_gap]);
        clock_.configure(settings[R::clocks_per_quarter], settings[R::clock_beats], uint32_t(settings[R::clock_timeout]) * 1000);
        dirty_ = true;
    }
    bool browse(bool reverse, const char *setup) {
        if (page_ == Page::confirm_firmware) return !reverse;
        page_ = Page((unsigned(page_) + (reverse ? 2 : 1)) % 3);
        if (page_ == Page::status) std::snprintf(setup_, sizeof(setup_), "%.63s", setup);
        dirty_ = true;
        return false;
    }
    Page page() const { return page_; }
    void revisit(const char *setup) {
        if (page_ == Page::status) std::snprintf(setup_, sizeof(setup_), "%.63s", setup);
        dirty_ = true;
    }
    void tap(uint32_t now) {
        if (page_ == Page::tap) tap_.tap(now);
        else if (page_ == Page::firmware) page_ = Page::confirm_firmware;
        else if (page_ == Page::confirm_firmware) page_ = Page::firmware;
        else return;
        dirty_ = true;
    }
    void clock(unsigned source, uint32_t now_us) { clock_.clock(source, now_us); }
    void time(uint32_t now, uint32_t now_us) {
        uptime_ += uint32_t(now - previous_); previous_ = now;
        clock_.poll(now_us); tap_.flash(now, 0);
    }
    bool due(uint32_t now) {
        using R = cntrl::RightPageSettings;
        if (dirty_) return true;
        if (page_ == Page::status) return uint32_t(now - refreshed_) >= settings_[R::refresh];
        return page_ == Page::tap && tap_.flash(now, settings_[R::flash_fraction]) != flashing_;
    }
    cntrl::Pane view(uint32_t now, uint32_t now_us, const PerformanceStatus &status) {
        using R = cntrl::RightPageSettings;
        dirty_ = false; refreshed_ = now;
        cntrl::Pane pane;
        pane.graphics.background = settings_[R::background];
        pane.graphics.foreground = 0; pane.graphics.pad = settings_[R::padding];
        const unsigned pad = pane.graphics.pad, title = settings_[R::title_height];
        if (page_ == Page::confirm_firmware) {
            const unsigned height = title * 3 / 2;
            line(pane, 0, "F for stock firmware.", 64 - height, height, 0, true);
            line(pane, 1, "C to cancel", 64, height, 0, true, nullptr, 0, 160, pane.lines[0].graphics.size);
            return pane;
        }
        const unsigned heading_height = std::min(title * 3 / 2, (128 - 2 * pad) / 2);
        const char *heading = page_ == Page::tap ? "Tap Tempo" : page_ == Page::status ? "Status" : "Firmware";
        line(pane, 0, heading, pad, heading_height, 0, true, "Tap Tempo");
        pane.heading_band = true;
        auto &header = pane.lines[0];
        header.graphics.foreground = 0xffff; header.graphics.background = 0;
        header.graphics.top = header.y; header.y = 0;
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
            flashing_ = tap_.flash(now, settings_[R::flash_fraction]);
            pane.circle.diameter = settings_[R::diameter]; pane.circle.annulus = settings_[R::annulus];
            pane.circle.x = 80;
            unsigned start = pad + heading_height + settings_[R::tempo_height];
            pane.circle.y = (start + 128 - pad) / 2;
            pane.circle.color = settings_[flashing_ ? R::on_color : R::off_color];
            pane.circle.ring_color = settings_[R::ring_color];
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
                std::snprintf(rows[4], sizeof(rows[4]), "Disk: %lu.%lu/%luG",
                    (unsigned long)(tenths / 10), (unsigned long)(tenths % 10),
                    (unsigned long)((status.disk_used + status.disk_free + gibibyte / 2) / gibibyte));
            } else std::snprintf(rows[4], sizeof(rows[4]), "Disk: --/--G");
            uint64_t seconds = uptime_ / 1000;
            std::snprintf(rows[5], sizeof(rows[5]), "Uptime: %lu:%02u:%02u",
                (unsigned long)(seconds / 3600), unsigned(seconds / 60 % 60), unsigned(seconds % 60));
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
                    if (i == 2 && l.text[j] == ' ' && l.text[j + 1] == ' ') value = false;
                    if (value) l.color_mask |= uint64_t(1) << j;
                }
            }
            clock_.poll(now_us);
            pane.lines[3].stippled = clock_.period_us() && !clock_.active();
        }
        return pane;
    }
private:
    void number_text(char *text, unsigned length, uint64_t period, bool microseconds, bool fixed) const {
        using R = cntrl::RightPageSettings;
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
    cntrl::RightPageSettings settings_;
    cntrl::TapTempo tap_;
    cntrl::ClockTempo clock_;
    Page page_ = Page::status;
    char setup_[64] = {};
    uint64_t uptime_ = 0;
    uint32_t previous_ = 0, refreshed_ = 0;
    bool dirty_ = false, flashing_ = false;
};
