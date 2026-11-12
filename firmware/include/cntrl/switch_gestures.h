#pragma once
#include "document.h"

namespace cntrl {
// Recognition produces at most one repeat per service call. Edges and timing
// use unsigned elapsed times so the millisecond clock may wrap normally.
// Inputs are confirmed edges. The input adapter owns the configured debounce.
class SwitchGestures {
public:
    void reset(const uint8_t *settings = nullptr) {
        *this = SwitchGestures(); settings_ = settings;
    }
    bool configured() const { return settings_ != nullptr; }
    // Cancel undecided taps, but pair an action already recognized on press.
    template<class Emit> void cancel(Emit emit) {
        if (settings_ && stable_) {
            if (second_) {
                if (long_) emit(8);
                else if (!long_double()) emit(5);
            }
            else if (long_) emit(3);
            else if (!delayed()) emit(1);
        }
        reset();
    }
    void input(bool down, uint32_t now) {
        if (down != candidate_) { candidate_ = down; changed_ = now; }
    }
    template<class Emit> void tick(uint32_t now, bool repeat_ready, Emit emit) {
        if (!settings_) return;
        // Use the confirmed edge time even when service runs later.
        const uint32_t event_time = stable_ != candidate_ ? changed_ : now;
        // A second debounced press at exactly the deadline is a new tap.
        if (waiting_ && uint32_t(event_time - released_) >= u16(settings_ + gesture_debounce_offset + 4)) {
            emit(0); emit(1); waiting_ = false;
        }
        if (stable_ && second_) {
            if (long_double() && !long_
                && uint32_t(event_time - pressed_) >= u16(settings_ + gesture_debounce_offset + 2)) {
                long_ = true; emit(7);
            }
        } else if (stable_) {
            if (!long_ && (u16(settings_ + 2) != 65535 || u16(settings_ + 10) != 65535)
                && uint32_t(event_time - pressed_) >= u16(settings_ + gesture_debounce_offset + 2)) {
                long_ = true; repeated_ = event_time; emit(2);
                if (repeat_ready) emit(6);
            } else if (long_ && repeat_ready && uint32_t(event_time - repeated_) >= u16(settings_ + gesture_debounce_offset + 6)) {
                repeated_ = event_time; emit(6);
            }
        }
        if (stable_ != candidate_) {
            stable_ = candidate_;
            if (stable_) {
                pressed_ = event_time; long_ = false;
                second_ = waiting_; waiting_ = false;
                if (second_) { if (!long_double()) emit(4); }
                else if (!delayed()) emit(0);
            } else if (second_) {
                if (long_) emit(8);
                else {
                    if (long_double()) emit(4);
                    emit(5);
                }
                second_ = long_ = false;
            } else if (long_) {
                emit(3); long_ = false;
            } else if (u16(settings_ + 6) != 65535 || long_double()) {
                waiting_ = true; released_ = event_time;
            } else {
                if (delayed()) emit(0);
                emit(1);
            }
        }
    }
private:
    const uint8_t *settings_ = nullptr;
    uint32_t changed_ = 0, pressed_ = 0, released_ = 0, repeated_ = 0;
    bool candidate_ = false, stable_ = false, waiting_ = false, second_ = false, long_ = false;
    bool long_double() const { return u16(settings_ + 12) != 65535; }
    bool delayed() const {
        return u16(settings_ + 2) != 65535 || u16(settings_ + 6) != 65535 || u16(settings_ + 10) != 65535 || long_double();
    }
};
} // namespace cntrl
