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
            if (long_) emit(long_release_event());
            else if (taps_ == 1 && !delayed()) emit(1);
            else if (taps_ == 2 && !long_double() && !triple()) emit(5);
            else if (taps_ == 3 && !long_triple()) emit(10);
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
        // A debounced press at exactly the deadline starts a new tap sequence.
        if (waiting_ && uint32_t(event_time - released_) >= u16(settings_ + gesture_debounce_offset + 4)) {
            if (taps_ == 2) { emit(4); emit(5); }
            else { emit(0); emit(1); }
            waiting_ = false; taps_ = 0;
        }
        if (stable_ && taps_ > 1) {
            const bool held_action = taps_ == 2 ? long_double() : long_triple();
            if (held_action && !long_
                && uint32_t(event_time - pressed_) >= u16(settings_ + gesture_debounce_offset + 2)) {
                long_ = true; emit(taps_ == 2 ? 7 : 11);
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
                taps_ = waiting_ ? taps_ + 1 : 1; waiting_ = false;
                if (taps_ == 2) { if (!long_double() && !triple()) emit(4); }
                else if (taps_ == 3) { if (!long_triple()) emit(9); }
                else if (!delayed()) emit(0);
            } else if (long_) {
                emit(long_release_event()); long_ = false; taps_ = 0;
            } else if ((taps_ == 1 && multiple()) || (taps_ == 2 && triple())) {
                waiting_ = true; released_ = event_time;
            } else {
                if (taps_ == 1) {
                    if (delayed()) emit(0);
                    emit(1);
                } else if (taps_ == 2) {
                    if (long_double()) emit(4);
                    emit(5);
                } else {
                    if (long_triple()) emit(9);
                    emit(10);
                }
                taps_ = 0;
            }
        }
    }
private:
    const uint8_t *settings_ = nullptr;
    uint32_t changed_ = 0, pressed_ = 0, released_ = 0, repeated_ = 0;
    unsigned taps_ = 0;
    bool candidate_ = false, stable_ = false, waiting_ = false, long_ = false;
    bool long_double() const { return u16(settings_ + 12) != 65535; }
    bool long_triple() const { return u16(settings_ + 20) != 65535; }
    bool triple() const { return u16(settings_ + 16) != 65535 || long_triple(); }
    bool multiple() const { return u16(settings_ + 6) != 65535 || long_double() || triple(); }
    unsigned long_release_event() const { return taps_ == 1 ? 3 : taps_ == 2 ? 8 : 12; }
    bool delayed() const {
        return u16(settings_ + 2) != 65535 || u16(settings_ + 10) != 65535 || multiple();
    }
};
} // namespace cntrl
