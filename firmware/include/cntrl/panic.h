#pragma once
#include "hardware.h"
#include "midi_outputs.h"
#include <vector>

namespace cntrl {
struct PanicOutputs {
    virtual ~PanicOutputs() = default;
    virtual bool available(const char *output) const = 0;
    virtual bool transmit(const char *output, const uint8_t *bytes) = 0;
};

class Panic {
public:
    enum class Status { ready, sending, incomplete };
    Status status() const { return active() ? Status::sending : incomplete_ ? Status::incomplete : Status::ready; }
    Panic(Hardware &hardware, PanicOutputs &outputs) : hardware_(hardware), outputs_(outputs),
        connections_(hardware.midi_outputs()), progress_(connections_.count) {}
    bool take_redraw() { bool result = dirty_; dirty_ = false; return result; }
    bool open() const { return open_; }
    bool active() const { return phase_ != Phase::idle; }
    void enter(uint8_t brightness) {
        open_ = true; held_ = incomplete_ = false; phase_ = Phase::idle;
        brightness_ = brightness; dirty_ = true;
    }
    void leave() {
        button(false, 0); phase_ = Phase::idle; open_ = false;
    }
    void button(bool down, uint32_t now) {
        if (!open_ || held_ == down) return;
        held_ = down; dirty_ = true;
        if (!down) released_ = now;
        hardware_.set_brightness(down ? 255 : brightness_, down);
        if (down && !active()) {
            incomplete_ = false; first_ = true; phase_ = Phase::controllers; reset_progress();
        } else if (!down && !first_) phase_ = Phase::idle;
    }
    // At most one three-byte message per poll. A blocked output cannot starve others.
    void tick(uint32_t now, bool outputs_ready) {
        if (!active()) return;
        if (!held_ && uint32_t(now - released_) >= 1000) {
            phase_ = Phase::idle; incomplete_ = dirty_ = true; return;
        }
        if (!outputs_ready) return;
        for (unsigned attempt = 0; attempt < connections_.count; ++attempt) {
            unsigned port = cursor_;
            cursor_ = (cursor_ + 1) % connections_.count;
            auto &progress = progress_[port];
            if (progress.done) continue;
            const auto &connection = connections_.data[port];
            if (!connection.output || !outputs_.available(connection.name)) { progress.done = true; continue; }
            else {
                static constexpr uint8_t controllers[] = {123, 120, 121};
                uint8_t bytes[] = {
                    uint8_t((phase_ == Phase::controllers ? 0xb0 : 0x80) + progress.channel),
                    phase_ == Phase::controllers ? controllers[progress.index] : uint8_t(progress.index),
                    0,
                };
                if (outputs_.transmit(connection.name, bytes)) {
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
        dirty_ = true; first_ = false;
        if (!held_) phase_ = Phase::idle;
        else phase_ = phase_ == Phase::controllers ? Phase::notes : Phase::controllers;
        reset_progress();
    }
private:
    enum class Phase { idle, controllers, notes };
    struct Progress { unsigned channel = 0, index = 0; bool done = false; };
    Hardware &hardware_;
    PanicOutputs &outputs_;
    midi::Outputs connections_;
    std::vector<Progress> progress_;
    Phase phase_ = Phase::idle;
    bool dirty_ = true, incomplete_ = false;
    uint32_t released_ = 0;
    bool open_ = false, held_ = false, first_ = false;
    uint8_t brightness_ = 192;
    unsigned cursor_ = 0;
    void reset_progress() { for (auto &progress : progress_) progress = Progress(); cursor_ = 0; }
};
}
