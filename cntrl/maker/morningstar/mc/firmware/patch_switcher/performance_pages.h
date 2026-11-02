#pragma once
#include <cntrl/hardware.h>
#include <cntrl/tempo.h>
#include "show_graphics.h"
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
    enum class Page { undo, tap, status };
    void configure(const cntrl::RightPageSettings &settings) {
        settings_ = settings;
        using R = cntrl::RightPageSettings;
        tap_.configure(settings[R::tap_intervals], settings[R::minimum_taps], settings[R::minimum_interval], settings[R::reset_gap]);
        clock_.configure(settings[R::clocks_per_quarter], settings[R::clock_beats], uint32_t(settings[R::clock_timeout]) * 1000);
        dirty_ = true;
    }
    void browse(bool reverse, const char *setup) {
        page_ = Page((unsigned(page_) + (reverse ? 2 : 1)) % 3);
        if (page_ == Page::status) std::snprintf(setup_, sizeof(setup_), "%.63s", setup);
        dirty_ = true;
    }
    Page page() const { return page_; }
    bool active() const { return page_ != Page::undo; }
    void history(const cntrl::Pane &pane) { history_ = pane; }
    void revisit(const char *setup) {
        if (page_ == Page::status) std::snprintf(setup_, sizeof(setup_), "%.63s", setup);
        dirty_ = true;
    }
    const cntrl::Pane &history() const { return history_; }
    void tap(uint32_t now) { if (page_ == Page::tap) { tap_.tap(now); dirty_ = true; } }
    void clock(unsigned source, uint32_t now_us) { clock_.clock(source, now_us); }
    void time(uint32_t now, uint32_t now_us) {
        uptime_ += uint32_t(now - previous_); previous_ = now;
        clock_.poll(now_us); tap_.flash(now, 0);
    }
    bool due(uint32_t now, bool held) {
        using R = cntrl::RightPageSettings;
        if (page_ == Page::undo) return false;
        if (dirty_) return true;
        if (page_ == Page::status) return uint32_t(now - refreshed_) >= settings_[R::refresh];
        return (held || tap_.flash(now, settings_[R::flash_duration])) != flashing_;
    }
    cntrl::Pane view(uint32_t now, uint32_t now_us, bool held, const PerformanceStatus &status) {
        using R = cntrl::RightPageSettings;
        dirty_ = false; refreshed_ = now;
        cntrl::Pane pane;
        pane.graphics.background = settings_[R::background];
        pane.graphics.foreground = 0; pane.graphics.pad = settings_[R::padding];
        const unsigned pad = pane.graphics.pad, title = settings_[R::title_height];
        line(pane, 0, page_ == Page::tap ? "Tap Tempo" : "Status", pad, title, 0, true);
        if (page_ == Page::tap) {
            char number[24], sample[24];
            number_text(number, sizeof(number), tap_.period(), false, true);
            const unsigned digits = settings_[R::digits], decimals = settings_[R::decimals];
            unsigned pos = 0;
            for (unsigned i = 0; i < digits; ++i) sample[pos++] = '8';
            if (decimals) { sample[pos++] = '.'; for (unsigned i = 0; i < decimals; ++i) sample[pos++] = '8'; }
            sample[pos] = 0;
            line(pane, 1, number, pad + title, settings_[R::tempo_height], settings_[R::tempo_font], false, sample);
            flashing_ = held || tap_.flash(now, settings_[R::flash_duration]);
            pane.circle.diameter = settings_[R::diameter]; pane.circle.annulus = settings_[R::annulus];
            pane.circle.x = 80;
            unsigned start = pad + title + settings_[R::tempo_height];
            pane.circle.y = (start + 128 - pad) / 2;
            pane.circle.color = settings_[flashing_ ? R::on_color : R::off_color];
            pane.circle.ring_color = settings_[R::ring_color];
        } else {
            char rows[10][64] = {};
            uint64_t seconds = uptime_ / 1000;
            std::snprintf(rows[0], sizeof(rows[0]), "Up %lluh %02u:%02u", (unsigned long long)(seconds / 3600),
                unsigned(seconds / 60 % 60), unsigned(seconds % 60));
            std::snprintf(rows[1], sizeof(rows[1]), "Setup %.57s", setup_);
            char tap[24], clock[24];
            number_text(tap, sizeof(tap), tap_.period(), false, false);
            number_text(clock, sizeof(clock), clock_.period_us(), true, false);
            std::snprintf(rows[2], sizeof(rows[2]), "Tap %s", tap);
            std::snprintf(rows[3], sizeof(rows[3]), "Clock %s", clock);
            std::snprintf(rows[4], sizeof(rows[4]), "CPU --");
            for (unsigned i = 0; i < 2; ++i)
                std::snprintf(rows[5 + i], sizeof(rows[5 + i]), "RAM%u U/F %lu/%luK", i + 1,
                    (unsigned long)(status.ram_used[i] / 1024), (unsigned long)(status.ram_free[i] / 1024));
            if (status.disk_valid)
                std::snprintf(rows[7], sizeof(rows[7]), "Disk U/F %lu/%luM",
                    (unsigned long)(status.disk_used / 1048576), (unsigned long)(status.disk_free / 1048576));
            else std::snprintf(rows[7], sizeof(rows[7]), "Disk U/F --");
            std::snprintf(rows[8], sizeof(rows[8]), "Cycles %u", status.cycles);
            std::snprintf(rows[9], sizeof(rows[9]), "Patches %u", status.patches);
            unsigned height = (128 - 2 * pad - title) / 10;
            for (unsigned i = 0; i < 10; ++i)
                line(pane, i + 1, rows[i], pad + title + i * height, height, settings_[R::status_font], false);
            clock_.poll(now_us);
            pane.lines[4].stippled = clock_.period_us() && !clock_.active();
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
              unsigned face, bool centered, const char *sample = nullptr) {
        auto &l = pane.lines[index];
        std::snprintf(l.text, sizeof(l.text), "%s", text);
        l.graphics = pane.graphics; l.graphics.font = face;
        const unsigned sizes[] = {96, 72, 60, 48, 40, 32, 28, 24, 20, 18, 16, 14, 13, 12, 11, 10, 9, 8};
        ST7735_t3 measure(255, 255, 255); measure.setTextSize(1);
        int16_t left = 0, top = 0; uint16_t width = 0, ink_height = 0;
        const char *fitting = sample ? sample : l.text;
        for (unsigned size : sizes) {
            const auto *font = ShowGraphics::native_font(face, size);
            if (!font) continue;
            measure.setFont(*font); measure.getTextBounds(fitting, 0, 0, &left, &top, &width, &ink_height);
            if ((width > 160 - 2 * pane.graphics.pad || ink_height > height) && size > (face >= 8 ? 12u : 8u)) continue;
            l.graphics.size = size; l.graphics.line_height = height; l.graphics.top = 0;
            l.x = centered ? (160 - width) / 2 - left : sample ? 160 - pane.graphics.pad - width - left : pane.graphics.pad;
            l.y = int(y) + (int(height) - ink_height) / 2 - top;
            if (!sample && width > 160 - 2 * pane.graphics.pad) {
                unsigned n = unsigned(std::strlen(l.text));
                while (n > 3 && width > 160 - 2 * pane.graphics.pad) {
                    --n; std::memcpy(l.text + n - 3, "...", 4);
                    measure.getTextBounds(l.text, 0, 0, &left, &top, &width, &ink_height);
                }
            }
            return;
        }
    }
    cntrl::RightPageSettings settings_;
    cntrl::TapTempo tap_;
    cntrl::ClockTempo clock_;
    cntrl::Pane history_;
    Page page_ = Page::undo;
    char setup_[64] = {};
    uint64_t uptime_ = 0;
    uint32_t previous_ = 0, refreshed_ = 0;
    bool dirty_ = false, flashing_ = false;
};
