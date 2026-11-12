#pragma once
#include "checked_list.h"
#include <cstring>
#include <cstdint>

namespace cntrl {
struct KeyboardShortcut {
    char name[32] = {};
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
    bool press(unsigned owner, const KeyboardShortcut &shortcut) {
        if (!connected_) return false;
        unsigned index = 0;
        while (index < held_.size() && held_[index].owner != owner) ++index;
        const bool added = index == held_.size();
        if (added && !held_.append({owner, {}})) return false;
        auto previous = held_[index].report;
        held_[index].report.modifiers = shortcut.modifiers;
        std::memcpy(held_[index].report.keys, shortcut.keys, 6);
        KeyboardReport next;
        if (!report(next)) {
            if (added) held_.erase_if([owner](const Hold &hold) { return hold.owner == owner; });
            else held_[index].report = previous;
            return false;
        }
        return true;
    }
    void release(unsigned owner) {
        for (auto i = held_.begin(); i != held_.end(); ++i)
            if (i->owner == owner) { held_.erase(i); return; }
    }
    void clear() { held_.release(); }
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
    struct Hold { unsigned owner; KeyboardReport report; };
    CheckedList<Hold> held_;
    KeyboardReport sent_;
    bool connected_ = false, sent_valid_ = false;
    bool report(KeyboardReport &next) const {
        unsigned count = 0;
        for (const auto &h : held_) {
            next.modifiers |= h.report.modifiers;
            for (uint8_t key : h.report.keys) {
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
