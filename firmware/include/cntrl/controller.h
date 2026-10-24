#pragma once
#include "document.h"
#include <cstdio>

namespace cntrl {
enum class Chord { none, ab, ad, be, de };

class Controller {
public:
    explicit Controller(Hardware &hardware) : hardware_(hardware) {}
    void deactivate() { document_ = Document(); held_ = 0; pending_ = -1; locked_ = false; }
    void cancel_gesture() { held_ = 0; pending_ = -1; locked_ = false; }
    void refresh() { cancel_gesture(); if (document_.valid()) render(); }
    void activate(const Document &document) {
        document_ = document;
        hardware_.set_brightness(document.brightness());
        hardware_.set_splash(document.splash(), document.screensaver_timeout());
        selected_ = depth_ = 0;
        for (auto &c : cursors_) c = 0;
        for (auto &p : current_) p = -1;
        for (auto &p : last_sent_) p = -1;
        center_ = -1;
        held_ = 0; pending_ = -1; locked_ = false;
        render();
    }
    // A..F use indices 0..5; timestamps belong to debounced edges.
    Chord button(unsigned button, bool pressed, uint32_t now) {
        if (button >= 6) return Chord::none;
        tick(now);
        if (button == 2 || button == 5) {
            if (pressed) action(button);
            return Chord::none;
        }
        const unsigned mask = 1u << button;
        if (pressed) {
            if (held_ & mask) return Chord::none;
            held_ |= mask;
            if (locked_) return Chord::none;
            if (pending_ >= 0) {
                // Recognized and unrecognized pairs both consume their singles.
                unsigned pair = (1u << unsigned(pending_)) | mask;
                pending_ = -1;
                locked_ = true;
                switch (pair) {
                    case 3: return Chord::ab;
                    case 9: return Chord::ad;
                    case 18: return Chord::be;
                    case 24: return Chord::de;
                    default: return Chord::none;
                }
            } else {
                pending_ = int(button);
                since_ = now;
            }
        } else {
            held_ &= ~mask;
            if (pending_ == int(button)) {
                pending_ = -1;
                action(button);
            }
            if (!held_) locked_ = false;
        }
        return Chord::none;
    }
    void tick(uint32_t now) {
        if (pending_ >= 0 && uint32_t(now - since_) > (document_.valid() ? document_.chord_ms() : 100)) {
            unsigned button = unsigned(pending_);
            pending_ = -1;
            action(button);
        }
    }
    void action(unsigned button) {
        if (!document_.valid()) return;
        if (button == 3 || button == 4) {
            selected_ = (selected_ + document_.categories() + (button == 3 ? -1 : 1)) % document_.categories();
        } else if (button == 0 || button == 1) {
            unsigned count = u16(document_.category(selected_) + 66);
            cursors_[selected_] = (cursors_[selected_] + count + (button == 0 ? -1 : 1)) % count;
        } else if (button == 2) {
            unsigned p = candidate();
            unsigned instrument = u16(document_.patch(p));
            if (!send(p)) return;
            if (depth_ == history_capacity) {
                for (unsigned i = 1; i < depth_; ++i) history_[i - 1] = history_[i];
                --depth_;
            }
            history_[depth_++] = {uint16_t(p), current_[instrument]};
            current_[instrument] = int(p);
            center_ = int(p);
            hardware_.transition(false);
        } else if (button == 5) {
            if (!depth_) return;
            const Action &a = history_[depth_ - 1];
            if (a.previous >= 0 && !send(unsigned(a.previous))) return;
            current_[u16(document_.patch(a.patch))] = a.previous;
            if (a.previous >= 0) center_ = a.previous;
            selected_ = u16(document_.patch(a.patch) + 2);
            cursors_[selected_] = a.patch - u16(document_.category(selected_) + 64);
            if (!--depth_) center_ = -1;
            hardware_.transition(true);
        }
        render();
    }
    unsigned depth() const { return depth_; }
private:
    struct Action { uint16_t patch; int previous; };
    Hardware &hardware_;
    Document document_;
    Action history_[history_capacity] = {};
    uint16_t cursors_[max_categories] = {};
    int current_[max_instruments] = {};
    // Unlike undo state, this changes only after a successful MIDI send.
    int last_sent_[max_instruments] = {};
    unsigned selected_ = 0, depth_ = 0, held_ = 0;
    int center_ = -1, pending_ = -1;
    uint32_t since_ = 0;
    bool locked_ = false;
    unsigned candidate() const { return u16(document_.category(selected_) + 64) + cursors_[selected_]; }
    bool send(unsigned index) {
        const uint8_t *p = document_.patch(index);
        const uint8_t *i = document_.instrument(u16(p));
        uint8_t data[14];
        unsigned n = 0;
        auto cc = [&](uint8_t number, uint8_t value) {
            if (value != 255) { data[n++] = 0xb0 | i[64]; data[n++] = number; data[n++] = value; }
        };
        cc(0, p[5]); cc(32, p[6]);
        data[n++] = 0xc0 | i[64]; data[n++] = p[4];
        cc(7, p[7]); cc(10, p[8]);
        if (hardware_.send(reinterpret_cast<const char *>(i + 32), data, n)) {
            last_sent_[u16(p)] = int(index);
            return true;
        }
        hardware_.error("MIDI output unavailable");
        return false;
    }
    void pane(unsigned slot, int index) {
        Pane view;
        view.graphics = document_.graphics(slot);
        view.patch_color = view.graphics.foreground;
        if (index >= 0) {
            const uint8_t *p = document_.patch(unsigned(index));
            int sent = last_sent_[u16(p)];
            if (slot == 2 && sent >= 0
                && !std::memcmp(p + 4, document_.patch(unsigned(sent)) + 4, 3))
                view.patch_color = view.graphics.half;
            std::snprintf(view.category, sizeof(view.category), "%s", document_.category(u16(p + 2)));
            std::snprintf(view.patch, sizeof(view.patch), "%s", p + 12);
            view.category_x = u16(document_.category(u16(p + 2)) + 68 + slot * 2);
            view.patch_x = u16(p + 76 + slot * 2);
        }
        hardware_.show(slot, view);
    }
    void render() {
        pane(0, depth_ ? history_[depth_ - 1].previous : -1);
        pane(1, center_);
        pane(2, int(candidate()));
    }
};
} // namespace cntrl
