#pragma once
#include "hardware.h"
#include <cstdio>

namespace cntrl {
enum class PanicPort { din, trs, usb_device, usb_host };
struct PanicOutputs {
    virtual ~PanicOutputs() = default;
    virtual bool available(PanicPort port) const = 0;
    virtual bool transmit(PanicPort port, const uint8_t *bytes) = 0;
};

class Panic {
public:
    Panic(Hardware &hardware, PanicOutputs &outputs) : hardware_(hardware), outputs_(outputs) {}
    bool open() const { return open_; }
    bool active() const { return phase_ != Phase::idle; }
    void enter(uint8_t brightness) {
        open_ = true; held_ = false; phase_ = Phase::idle;
        brightness_ = brightness; render();
    }
    void leave() {
        button(false); phase_ = Phase::idle; open_ = false;
    }
    void button(bool down) {
        if (!open_ || held_ == down) return;
        held_ = down;
        hardware_.set_brightness(down ? 255 : brightness_, down);
        if (down && !active()) {
            first_ = true; phase_ = Phase::controllers; reset_progress();
        } else if (!down && !first_) phase_ = Phase::idle;
    }
    // At most one three-byte message per poll. A blocked output cannot starve others.
    void tick() {
        if (!active()) return;
        for (unsigned attempt = 0; attempt < port_count; ++attempt) {
            unsigned port = cursor_;
            cursor_ = (cursor_ + 1) % port_count;
            auto &progress = progress_[port];
            if (progress.done) continue;
            if (!outputs_.available(PanicPort(port))) progress.done = true;
            else {
                static constexpr uint8_t controllers[] = {123, 120, 121};
                uint8_t bytes[] = {
                    uint8_t((phase_ == Phase::controllers ? 0xb0 : 0x80) + progress.channel),
                    phase_ == Phase::controllers ? controllers[progress.index] : uint8_t(progress.index),
                    0,
                };
                if (outputs_.transmit(PanicPort(port), bytes)) {
                    unsigned count = phase_ == Phase::controllers ? 3 : 128;
                    if (++progress.index == count) {
                        progress.index = 0;
                        if (++progress.channel == 16) progress.done = true;
                    }
                }
            }
            break;
        }
        for (const auto &progress : progress_) if (!progress.done) return;
        first_ = false;
        if (!held_) phase_ = Phase::idle;
        else phase_ = phase_ == Phase::controllers ? Phase::notes : Phase::controllers;
        reset_progress();
    }
private:
    static constexpr unsigned port_count = 4;
    enum class Phase { idle, controllers, notes };
    struct Progress { unsigned channel = 0, index = 0; bool done = false; };
    Hardware &hardware_;
    PanicOutputs &outputs_;
    Progress progress_[port_count];
    Phase phase_ = Phase::idle;
    bool open_ = false, held_ = false, first_ = false;
    uint8_t brightness_ = 192;
    unsigned cursor_ = 0;
    void reset_progress() { for (auto &progress : progress_) progress = Progress(); cursor_ = 0; }
    void render() {
        Pane views[3];
        for (auto &view : views) {
            view.graphics.foreground = 0xffff; view.graphics.background = 0;
            for (auto &line : view.lines) {
                line.graphics = view.graphics;
                line.graphics.size = line.graphics.line_height = 16;
            }
        }
        auto &label = views[1].lines[0];
        std::snprintf(label.text, 64, "Panic"); label.centered = true;
        label.y = hardware_.display_height() - 24;
        auto &back = views[2].lines[0];
        std::snprintf(back.text, 64, "Back");
        back.x = hardware_.display_width() - 52; back.y = 4;
        for (unsigned i = 0; i < 3; ++i) hardware_.show(i, views[i]);
    }
};
}
