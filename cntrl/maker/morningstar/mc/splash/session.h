#pragma once
#include "receiver.h"

namespace cntrl::splash {
// Own splash lifetime independently of the normal renderer and patch history.
class Session {
public:
    Session(Receiver &assets, Player &player) : assets_(assets), player_(player) {}
    void configure(bool enabled) {
        stop(); pending_ = enabled;
    }
    void stop() {
        player_.stop();
        if (running_) redraw_ = true;
        running_ = pending_ = false;
    }
    bool controls(uint8_t pressed) {
        if (running_ || pending_) {
            if (pressed) { stop(); held_ = true; }
            return true;
        }
        if (held_) {
            if (!pressed) held_ = false;
            return true; // Consume the entire dismissing gesture, including release.
        }
        return false;
    }
    bool poll(uint32_t now) {
        if (pending_ && !assets_.busy()) {
            pending_ = false;
            running_ = player_.start(now);
        }
        if (running_ && player_.poll(now, false) == Step::failed) stop();
        return running_ || pending_;
    }
    bool take_redraw() {
        bool redraw = redraw_; redraw_ = false; return redraw;
    }
private:
    Receiver &assets_;
    Player &player_;
    bool running_ = false, pending_ = false, held_ = false, redraw_ = false;
};
} // namespace cntrl::splash
