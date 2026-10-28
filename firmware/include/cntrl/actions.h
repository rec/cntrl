#pragma once
#include "document.h"

namespace cntrl {
struct ActionOutputs {
    virtual ~ActionOutputs() = default;
    virtual bool available(const char *output) const = 0;
    virtual bool send_action(unsigned owner, const char *output, const uint8_t *bytes, unsigned length) = 0;
    virtual midi::Bridge::Delivery action_delivery(unsigned owner) const = 0;
    virtual void cancel_action(unsigned owner) = 0;
};

class Actions {
public:
    Actions(Hardware &hardware, ActionOutputs &outputs) : hardware_(hardware), outputs_(outputs) {}
    bool open() const { return open_; }
    bool closing() const { return closing_; }
    bool abandoned() const { return abandoned_; }
    void enter(const Document &document, uint8_t held) {
        document_ = document; count_ = document.actions(); held_ = disarmed_ = held;
        open_ = dirty_ = true; closing_ = abandoned_ = false;
        for (unsigned i = 0; i < count_; ++i) {
            states_[i] = State(); std::memcpy(states_[i].record, document.action(i), 100);
            std::memcpy(states_[i].output, document.instrument(u16(states_[i].record + 2)) + 32, 32);
            if (!(states_[i].record[1] & 2)) {
                states_[i].record[6] |= document.instrument(u16(states_[i].record + 2))[64];
                states_[i].record[20] |= document.instrument(u16(states_[i].record + 2))[64];
            }
            if (states_[i].record[1] & 2) for (unsigned s = 0; s < 2; ++s) {
                uint8_t *bytes = states_[i].record + 6 + s * 14;
                const uint8_t *patch = document.patch(u16(bytes));
                unsigned channel = document.instrument(u16(patch))[64], n = 0;
                auto cc = [&](uint8_t number, uint8_t value) { if (value != 255) { bytes[n++] = 0xb0 | channel; bytes[n++] = number; bytes[n++] = value; } };
                cc(0, patch[5]); cc(32, patch[6]); bytes[n++] = 0xc0 | channel; bytes[n++] = patch[4]; cc(7, patch[7]); cc(10, patch[8]);
                states_[i].record[4 + s] = uint8_t(n);
            }
            outputs_.cancel_action(i);
        }
        render();
    }
    void button(unsigned button, bool down) {
        if (!open_ || button >= 6) return;
        unsigned bit = 1u << button;
        if (bool(held_ & bit) == down) return;
        held_ = down ? held_ | bit : held_ & ~bit;
        if (!down) disarmed_ &= ~bit;
        if (button == 5 && down) { close(); return; }
        if (closing_) return;
        for (unsigned i = 0; i < count_; ++i) {
            auto &s = states_[i]; if (s.record[0] != button || (disarmed_ & bit)) continue;
            if (s.record[1] & 1) { if (down) s.requested = !s.requested; }
            else s.requested = down;
            dirty_ = true;
        }
    }
    void close() {
        if (!open_) return;
        if (closing_) {
            for (unsigned i = 0; i < count_; ++i) outputs_.cancel_action(i);
            open_ = false; abandoned_ = true; return;
        }
        closing_ = dirty_ = true;
        for (auto &s : states_) s.requested = false;
    }
    void tick() {
        if (!open_) return;
        bool done = true;
        for (unsigned i = 0; i < count_; ++i) {
            auto &s = states_[i];
            bool available = outputs_.available(s.output);
            auto delivery = outputs_.action_delivery(i);
            if ((!available && (s.active || s.pending || s.requested)) || (s.pending && delivery == midi::Bridge::Delivery::cancelled)) {
                outputs_.cancel_action(i); s.active = s.pending = s.requested = false;
                s.unknown = true; disarmed_ |= held_ & (1u << s.record[0]); dirty_ = true;
            }
            if (s.pending && delivery == midi::Bridge::Delivery::complete) { s.pending = false; dirty_ = true; }
            bool next = s.unknown ? false : s.requested;
            if (!s.pending && available && ((s.unknown && s.requested) || next != s.active)) {
                unsigned n = s.record[next ? 4 : 5];
                if (outputs_.send_action(i, s.output, s.record + (next ? 6 : 20), n)) {
                    s.active = next; s.pending = true; s.unknown = false; dirty_ = true;
                }
            }
            if (s.active || s.pending || s.requested) done = false;
        }
        if (closing_ && done) {
            for (unsigned i = 0; i < count_; ++i) if (states_[i].unknown) abandoned_ = true;
            open_ = false; return;
        }
        if (dirty_) render();
    }
private:
    struct State {
        uint8_t record[100] = {};
        char output[32] = {};
        bool requested = false, active = false, pending = false, unknown = false;
    } states_[5];
    Hardware &hardware_; ActionOutputs &outputs_; Document document_;
    unsigned count_ = 0, held_ = 0, disarmed_ = 0;
    bool open_ = false, closing_ = false, dirty_ = false, abandoned_ = false;
    void render() {
        Pane panes[3];
        for (unsigned i = 0; i < count_; ++i) {
            const auto &s = states_[i]; Pane view; view.graphics = document_.graphics(1);
            const char *state = s.unknown ? "Unknown / Unavailable" : s.pending || s.requested != s.active ? "Pending" : s.active ? "On" : "Off";
            document_.page_record(view, u16(s.record + 98), 0, false, state);
            unsigned pane = s.record[0] % 3, group = s.record[0] / 3;
            panes[pane].graphics = view.graphics;
            for (unsigned j = 0; j < 3; ++j) {
                panes[pane].lines[group * 3 + j] = view.lines[j];
                panes[pane].lines[group * 3 + j].y = int16_t(4 + group * 58 + j * 18);
            }
        }
        auto &back = panes[2].lines[15]; back.graphics.size = back.graphics.line_height = 8;
        back.y = int16_t(hardware_.display_height() - 12);
        std::snprintf(back.text, 64, "%s", closing_ ? "Closing: F abandon" : "F Back");
        if (!count_) std::snprintf(panes[1].lines[0].text, 64, "No actions");
        for (unsigned i = 0; i < 3; ++i) hardware_.show(i, panes[i]);
        dirty_ = false;
    }
};
}
