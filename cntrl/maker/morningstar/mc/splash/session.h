#pragma once
#include "receiver.h"

namespace cntrl::splash {
// Own startup splash, idle animation and nonblocking backlight transitions.
class Session {
public:
    Session(Receiver &assets, Player &player) : assets_(assets), player_(player) {}
    void configure(bool enabled, uint32_t idle_ms, uint32_t now) {
        stop(); pending_ = enabled; idle_ms_ = idle_ms;
        last_input_ = now; clock_started_ = true;
    }
    void stop() {
        player_.stop();
        if (running_ || fade_ != Fade::none) redraw_ = true;
        running_ = pending_ = saver_ = false;
        fade_ = Fade::none; clock_started_ = false;
    }
    bool controls(uint8_t pressed, uint32_t now) {
        if (pressed || pressed != pressed_) last_input_ = now;
        pressed_ = pressed;
        if (pending_ || (running_ && !saver_ && fade_ == Fade::none)) {
            if (pressed) {
                if (running_) wake(now, 1000);
                else pending_ = false;
                held_ = true;
            }
            return true;
        }
        if (held_) {
            if (!pressed) held_ = false;
            return true; // Dismissal consumes the complete gesture.
        }
        if (saver_ && to_animation_ && fade_ != Fade::none && pressed) {
            stop(); // Cancel entry without a wake fade or a musical action.
            held_ = true;
            return true;
        }
        if (saver_ && pressed) wake(now, 200);
        return false; // Screensaver wake also performs the normal action.
    }
    bool poll(uint32_t now, bool ui_ready = false, bool transfer_busy = false) {
        bool busy = transfer_busy || assets_.busy();
        if (!clock_started_ || busy) {
            last_input_ = now; clock_started_ = true;
        }
        if (pending_ && !assets_.busy()) {
            pending_ = false;
            running_ = player_.start(now);
        }
        if (!pending_ && !running_ && fade_ == Fade::none && idle_ms_
            && !pressed_ && !busy && assets_.size()
            && uint32_t(now - last_input_) >= idle_ms_) {
            saver_ = to_animation_ = true;
            fade_ = Fade::out; started_ = now; duration_ = 4000; from_ = 1000;
        }
        if (running_ && player_.poll(now, false) == Step::failed) stop();
        if (fade_ == Fade::out && uint32_t(now - started_) >= duration_) {
            if (to_animation_) {
                running_ = player_.start(now);
                if (running_) fade_ = Fade::painting;
                else stop();
            } else {
                player_.stop(); running_ = false;
                fade_ = Fade::painting; redraw_ = true;
            }
        } else if (fade_ == Fade::painting
                   && (to_animation_ ? player_.frame_ready() : ui_ready && !redraw_)) {
            fade_ = Fade::in; started_ = now;
        } else if (fade_ == Fade::in && uint32_t(now - started_) >= duration_) {
            fade_ = Fade::none;
        }
        // Unlock the backlight only after one complete boot image, never a partial row.
        if (!first_image_ && !pending_
            && (running_ ? player_.frame_ready() : ui_ready && !redraw_)) first_image_ = true;
        return running_ || pending_ || (fade_ == Fade::out && !to_animation_);
    }
    uint8_t brightness(uint8_t target, uint32_t now) const {
        if (!first_image_) return 0;
        uint32_t value = level(now);
        return uint32_t(target) * value * value / 1000000;
    }
    bool take_redraw() {
        bool redraw = redraw_; redraw_ = false; return redraw;
    }
private:
    enum class Fade { none, out, painting, in };
    uint32_t level(uint32_t now) const {
        uint32_t elapsed = now - started_;
        if (elapsed > duration_) elapsed = duration_;
        if (fade_ == Fade::out) return from_ * (duration_ - elapsed) / duration_;
        if (fade_ == Fade::painting) return 0;
        if (fade_ == Fade::in) return 1000 * elapsed / duration_;
        return 1000;
    }
    void wake(uint32_t now, uint32_t duration) {
        from_ = first_image_ ? level(now) : 0; duration_ = duration; started_ = now;
        to_animation_ = saver_ = false; fade_ = Fade::out;
    }
    Receiver &assets_;
    Player &player_;
    bool running_ = false, pending_ = false, held_ = false, redraw_ = false;
    bool saver_ = false, to_animation_ = false, clock_started_ = false;
    bool first_image_ = false;
    uint8_t pressed_ = 0;
    Fade fade_ = Fade::none;
    uint32_t started_ = 0, duration_ = 1000, from_ = 1000;
    uint32_t idle_ms_ = 0, last_input_ = 0;
};
} // namespace cntrl::splash
