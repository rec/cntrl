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
        if (running_ || fade_ != Fade::none) redraw_ = true;
        running_ = pending_ = false;
        fade_ = Fade::none;
    }
    bool controls(uint8_t pressed, uint32_t now) {
        if (running_ || pending_) {
            if (pressed) {
                if (running_) {
                    running_ = false;
                    fade_ = Fade::out; started_ = now;
                } else pending_ = false;
                held_ = true;
            }
            return true;
        }
        if (held_) {
            if (!pressed) held_ = false;
            return true; // Consume the entire dismissing gesture, including release.
        }
        return false;
    }
    bool poll(uint32_t now, bool ui_ready = false) {
        if (pending_ && !assets_.busy()) {
            pending_ = false;
            running_ = player_.start(now);
        }
        if ((running_ || fade_ == Fade::out) && player_.poll(now, false) == Step::failed) stop();
        if (fade_ == Fade::out && uint32_t(now - started_) >= fade_ms) {
            player_.stop();
            fade_ = Fade::painting; redraw_ = true;
        } else if (fade_ == Fade::painting && ui_ready && !redraw_) {
            fade_ = Fade::in; started_ = now;
        } else if (fade_ == Fade::in && uint32_t(now - started_) >= fade_ms) {
            fade_ = Fade::none;
        }
        return running_ || pending_ || fade_ == Fade::out;
    }
    uint8_t brightness(uint8_t target, uint32_t now) const {
        uint32_t elapsed = now - started_;
        if (elapsed > fade_ms) elapsed = fade_ms;
        if (fade_ == Fade::out) return target * (fade_ms - elapsed) / fade_ms;
        if (fade_ == Fade::painting) return 0;
        if (fade_ == Fade::in) return target * elapsed / fade_ms;
        return target;
    }
    bool take_redraw() {
        bool redraw = redraw_; redraw_ = false; return redraw;
    }
private:
    enum class Fade { none, out, painting, in };
    static constexpr uint32_t fade_ms = 1000;
    Receiver &assets_;
    Player &player_;
    bool running_ = false, pending_ = false, held_ = false, redraw_ = false;
    Fade fade_ = Fade::none;
    uint32_t started_ = 0;
};
} // namespace cntrl::splash
