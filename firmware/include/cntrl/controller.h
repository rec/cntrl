#pragma once
#include "patch_executor.h"
#include <cstdio>

namespace cntrl {
enum class Chord { none, ab, ad, be, de };

class Controller {
public:
    struct ProgramHistory { uint16_t patch; int previous; };
    struct NavigationState {
        uint16_t selected = 0, cursors[max_patch_cycles] = {};
        unsigned depth = 0;
        ProgramHistory history[history_capacity] = {};
        int current[max_instruments] = {}, last_sent[max_instruments] = {}, center = -1;
    };
    NavigationState navigation() const {
        NavigationState state; state.selected = uint16_t(selected_); state.depth = depth_; state.center = center_;
        std::memcpy(state.cursors, cursors_, sizeof(cursors_));
        std::memcpy(state.history, history_, sizeof(history_));
        std::memcpy(state.current, current_, sizeof(current_));
        std::memcpy(state.last_sent, last_sent_, sizeof(last_sent_)); return state;
    }
    bool navigation(const NavigationState &state) {
        if (state.selected >= document_.patch_cycles() && (document_.patch_cycles() || state.selected)) return false;
        if (state.depth > history_capacity || state.center < -1 || (state.center >= 0 && unsigned(state.center) >= document_.patches())) return false;
        for (unsigned i = 0; i < document_.patch_cycles(); ++i)
            if (state.cursors[i] >= u16(document_.patch_cycle(i) + 66)) return false;
        for (unsigned i = 0; i < state.depth; ++i) {
            const ProgramHistory &a = state.history[i];
            if (a.patch >= document_.patches() || u16(document_.patch(a.patch)) >= document_.instruments()
                || a.previous < -1 || (a.previous >= 0 && (unsigned(a.previous) >= document_.patches()
                || u16(document_.patch(unsigned(a.previous))) != u16(document_.patch(a.patch))))) return false;
        }
        for (unsigned i = 0; i < document_.instruments(); ++i) {
            for (int p : {state.current[i], state.last_sent[i]})
                if (p < -1 || (p >= 0 && (unsigned(p) >= document_.patches() || u16(document_.patch(unsigned(p))) != i))) return false;
        }
        selected_ = state.selected; depth_ = state.depth; center_ = state.center;
        std::memcpy(cursors_, state.cursors, sizeof(cursors_));
        std::memcpy(history_, state.history, sizeof(history_));
        std::memcpy(current_, state.current, sizeof(current_));
        std::memcpy(last_sent_, state.last_sent, sizeof(last_sent_)); return true;
    }
    Controller(Hardware &hardware, PatchOutputs &outputs, PatchSources &sources)
        : hardware_(hardware), executor_(hardware, outputs, sources, 5) {}
    void deactivate() { executor_.cancel(); sending_ = -1; cancel_gesture(); document_ = Document(); }
    bool busy() const { return executor_.busy(); }
    void runtime_brightness(uint8_t value) { brightness_ = value; }
    PatchExecutor &executor() { return executor_; }
    void validation(bool enabled) {
        previewing_ = enabled;
        if (enabled) {
            preview_selected_ = selected_; preview_depth_ = depth_;
            std::memcpy(preview_cursors_, cursors_, sizeof(cursors_));
        }
    }
    bool preview_state(unsigned operation, uint32_t) {
        if (operation < 3 || operation > 6) return true;
        return move(operation, preview_selected_, preview_cursors_);
    }
    bool selection(unsigned operation, const Document *&document, unsigned &node) {
        document = &document_;
        if (operation == 7) {
            if (!document_.patches()) return false;
            unsigned selected = previewing_ ? preview_selected_ : selected_;
            const uint16_t *cursors = previewing_ ? preview_cursors_ : cursors_;
            unsigned p = u16(document_.patch_cycle(selected) + 64) + cursors[selected];
            node = document_.browser_root(p); return true;
        }
        unsigned depth = previewing_ ? preview_depth_ : depth_;
        if (!depth) { node = 65535; return true; }
        const ProgramHistory &a = history_[depth - 1];
        node = a.previous < 0 ? 65535 : document_.browser_root(unsigned(a.previous));
        if (previewing_) {
            --preview_depth_; preview_selected_ = u16(document_.patch(a.patch) + 2);
            preview_cursors_[preview_selected_] = a.patch - u16(document_.patch_cycle(preview_selected_) + 64);
        }
        return true;
    }
    void selection_complete(unsigned operation) {
        if (operation != 8 || !depth_) return;
        const ProgramHistory &a = history_[depth_ - 1];
        unsigned instrument = u16(document_.patch(a.patch));
        current_[instrument] = a.previous;
        if (a.previous >= 0) last_sent_[instrument] = a.previous;
        selected_ = u16(document_.patch(a.patch) + 2);
        cursors_[selected_] = a.patch - u16(document_.patch_cycle(selected_) + 64);
        --depth_;
    }
    void cancel_gesture() { release_c(); held_ = 0; pending_ = -1; locked_ = false; }
    void refresh() { cancel_gesture(); if (document_.valid()) render(); }
    void activate(const Document &document) {
        executor_.cancel(); sending_ = -1;
        cancel_gesture();
        document_ = document;
        hardware_.set_brightness(document.brightness());
        brightness_ = document.brightness();
        hardware_.set_usb_midi_interface(document.usb_midi_interface());
        hardware_.set_animation_color_cycle(document.animation_color_cycle());
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
        if (button == 2) {
            if (!pressed) release_c();
            else if (!c_held_) { c_held_ = true; action(button); }
            return Chord::none;
        }
        if (button == 5) {
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
        executor_.tick();
        if (sending_ >= 0 && c_held_ && executor_.started() && !flashing_) {
            flashing_ = true; hardware_.set_brightness(255, true);
        }
        finish_execution();
        if (pending_ >= 0 && uint32_t(now - since_) > (document_.valid() ? document_.chord_ms() : 100)) {
            unsigned button = unsigned(pending_);
            pending_ = -1;
            action(button);
        }
    }
    void action(unsigned button) {
        if (executor_.busy()) return;
        static constexpr unsigned operations[] = {4, 3, 7, 6, 5, 8};
        if (button < 6) executor_.invoke(StatePatch{operations[button]});
    }
    bool navigate(unsigned operation) {
        if (!document_.valid() || !document_.patch_cycles()) return false;
        if (operation == 5 || operation == 6) {
            selected_ = (selected_ + document_.patch_cycles() + (operation == 6 ? -1 : 1)) % document_.patch_cycles();
        } else if (operation == 3 || operation == 4) {
            unsigned count = u16(document_.patch_cycle(selected_) + 66);
            cursors_[selected_] = (cursors_[selected_] + count + (operation == 4 ? -1 : 1)) % count;
        } else if (operation == 7) {
            if (executor_.busy()) return false;
            unsigned p = candidate();
            if (already_sent(p)) return true;
            NavigationState before = navigation();
            if (u16(document_.patch(p)) == 65535) {
                depth_ = 0;
                for (auto &v : current_) v = -1;
                for (auto &v : last_sent_) v = -1;
            }
            if (!executor_.start(document_, document_.browser_root(p))) { navigation(before); return false; }
            sending_ = int(p); undoing_ = false;
        } else if (operation == 8) {
            if (executor_.busy()) return false;
            if (!depth_) return true;
            const ProgramHistory &a = history_[depth_ - 1];
            sending_ = int(a.patch); undoing_ = true;
            if (a.previous >= 0) {
                if (!executor_.start(document_, document_.browser_root(unsigned(a.previous)))) { sending_ = -1; return false; }
            } else complete_undo();
        } else return false;
        render();
        return true;
    }
    unsigned depth() const { return depth_; }
    unsigned selected_patch() const { return candidate(); }
    const Document &document() const { return document_; }
    void external_selection() {
        cancel_gesture(); depth_ = 0; center_ = -1;
        for (auto &p : current_) p = -1;
        for (auto &p : last_sent_) p = -1;
    }
private:
    Hardware &hardware_;
    PatchExecutor executor_;
    Document document_;
    ProgramHistory history_[history_capacity] = {};
    uint16_t cursors_[max_patch_cycles] = {};
    uint16_t preview_cursors_[max_patch_cycles] = {};
    int current_[max_instruments] = {};
    // Unlike undo state, this changes only after a successful MIDI send.
    int last_sent_[max_instruments] = {};
    unsigned selected_ = 0, depth_ = 0, held_ = 0;
    unsigned preview_selected_ = 0, preview_depth_ = 0;
    int center_ = -1, pending_ = -1;
    int sending_ = -1;
    uint32_t since_ = 0;
    bool locked_ = false, c_held_ = false, flashing_ = false, undoing_ = false, previewing_ = false;
    uint8_t brightness_ = 192;
    bool move(unsigned operation, unsigned &selected, uint16_t *cursors) const {
        if (!document_.valid() || !document_.patch_cycles()) return false;
        if (operation == 5 || operation == 6)
            selected = (selected + document_.patch_cycles() + (operation == 6 ? -1 : 1)) % document_.patch_cycles();
        else if (operation == 3 || operation == 4) {
            unsigned count = u16(document_.patch_cycle(selected) + 66);
            cursors[selected] = (cursors[selected] + count + (operation == 4 ? -1 : 1)) % count;
        } else return false;
        return true;
    }
    void release_c() {
        if (flashing_ && document_.valid()) hardware_.set_brightness(brightness_);
        flashing_ = c_held_ = false;
    }
    unsigned candidate() const { return document_.patch_cycles() ? u16(document_.patch_cycle(selected_) + 64) + cursors_[selected_] : 0; }
    bool already_sent(unsigned index) const {
        const uint8_t *p = document_.patch(index);
        if (u16(p) == 65535) return false;
        int sent = last_sent_[u16(p)];
        return sent >= 0 && !std::memcmp(p + 4, document_.patch(unsigned(sent)) + 4, 3);
    }
    void finish_execution() {
        if (sending_ < 0 || executor_.busy()) return;
        if (executor_.status() != PatchExecutor::Status::completed) {
            sending_ = -1;
            if (flashing_) { flashing_ = false; hardware_.set_brightness(brightness_); }
            if (executor_.started()) {
                depth_ = 0; center_ = -1;
                for (auto &p : current_) p = -1;
                for (auto &p : last_sent_) p = -1;
                if (executor_.status() == PatchExecutor::Status::unknown) {
                    render(); hardware_.error("Remote state unknown");
                }
            }
            return;
        }
        if (undoing_) { complete_undo(); render(); return; }
        unsigned p = unsigned(sending_), instrument = u16(document_.patch(p));
        if (instrument != 65535) {
            if (depth_ == history_capacity) {
                for (unsigned i = 1; i < depth_; ++i) history_[i - 1] = history_[i];
                --depth_;
            }
            history_[depth_++] = {uint16_t(p), current_[instrument]};
            current_[instrument] = last_sent_[instrument] = int(p);
        }
        center_ = int(p); sending_ = -1;
        hardware_.transition(false, document_.transition_ms()); render();
    }
    void complete_undo() {
        const ProgramHistory &a = history_[depth_ - 1];
        unsigned instrument = u16(document_.patch(a.patch));
        current_[instrument] = a.previous;
        if (a.previous >= 0) { center_ = a.previous; last_sent_[instrument] = a.previous; }
        selected_ = u16(document_.patch(a.patch) + 2);
        cursors_[selected_] = a.patch - u16(document_.patch_cycle(selected_) + 64);
        if (!--depth_) center_ = -1;
        sending_ = -1; hardware_.transition(true, document_.transition_ms());
    }
    void pane(unsigned slot, int index) {
        Pane view;
        view.graphics = document_.graphics(slot);
        if (index >= 0) {
            document_.page(view, unsigned(index), slot, slot == 2 && already_sent(unsigned(index)));
        }
        hardware_.show(slot, view);
    }
    void render() {
        pane(0, depth_ ? history_[depth_ - 1].previous : -1);
        pane(1, center_);
        pane(2, document_.patches() ? int(candidate()) : -1);
    }
};
} // namespace cntrl
