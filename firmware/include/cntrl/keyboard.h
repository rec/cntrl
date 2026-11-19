#pragma once
#include "checked_list.h"
#include <cstring>
#include <cstdint>

namespace cntrl {
enum class KeyboardDelivery { pending, complete, failed };
struct KeyboardKeys {
    uint8_t modifiers = 0, keys[6] = {};
};
struct KeyboardReport {
    uint8_t modifiers = 0, reserved = 0, keys[6] = {};
    bool operator==(const KeyboardReport &other) const {
        if (modifiers != other.modifiers) return false;
        for (unsigned i = 0; i < 6; ++i) if (keys[i] != other.keys[i]) return false;
        return true;
    }
};
struct KeyboardSink {
    virtual ~KeyboardSink() = default;
    virtual bool keyboard_report(const KeyboardReport &) = 0;
};
class Keyboard {
public:
    KeyboardDelivery execute(const KeyboardKeys &shortcut, bool pressed, KeyboardSink &sink) {
        if (!connected_) return KeyboardDelivery::failed;
        if (!sent_valid_) return KeyboardDelivery::pending;
        KeyboardReport keys;
        keys.modifiers = shortcut.modifiers;
        std::memcpy(keys.keys, shortcut.keys, 6);
        bool exists = false;
        for (const auto &held : held_) if (held == keys) exists = true;
        if (pressed == exists) return KeyboardDelivery::complete;
        if (pressed && !held_.append(keys)) return KeyboardDelivery::failed;
        KeyboardReport next;
        const bool valid = report(next, pressed ? nullptr : &keys);
        const bool delivered = valid && (next == sent_ || sink.keyboard_report(next));
        if ((pressed && !delivered) || (!pressed && delivered)) {
            held_.erase_if([&keys](const KeyboardReport &held) { return held == keys; });
        }
        if (!valid) return KeyboardDelivery::failed;
        if (!delivered) return KeyboardDelivery::pending;
        sent_ = next;
        return KeyboardDelivery::complete;
    }
    void clear() { held_.release(); }
    bool released() const { return !connected_ || (sent_valid_ && sent_ == KeyboardReport{}); }
    void poll(bool connected, KeyboardSink &sink) {
        if (connected != connected_) {
            clear(); connected_ = connected; sent_valid_ = false;
        }
        if (!connected_) return;
        KeyboardReport next;
        report(next);
        if ((!sent_valid_ || !(next == sent_)) && sink.keyboard_report(next)) {
            sent_ = next; sent_valid_ = true;
        }
    }
private:
    CheckedList<KeyboardReport> held_;
    KeyboardReport sent_;
    bool connected_ = false, sent_valid_ = false;
    bool report(KeyboardReport &next, const KeyboardReport *omit = nullptr) const {
        unsigned count = 0;
        for (const auto &h : held_) {
            if (omit && h == *omit) continue;
            next.modifiers |= h.modifiers;
            for (uint8_t key : h.keys) {
                if (!key) continue;
                bool found = false;
                for (unsigned i = 0; i < count; ++i) if (next.keys[i] == key) found = true;
                if (found) continue;
                if (count == 6) return false;
                next.keys[count++] = key;
            }
        }
        return true;
    }
};
}
