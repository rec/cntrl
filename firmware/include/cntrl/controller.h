#pragma once
#include "patch_executor.h"
#include "checked_list.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>

namespace cntrl {

class Controller {
public:
    struct ProgramHistory { uint16_t patch; int previous; };
    struct NavigationState {
        uint16_t selected = 0;
        unsigned depth = 0, cycles = 0, instruments = 0;
        int center = -1;
        const uint16_t *cursors = nullptr;
        const ProgramHistory *history = nullptr;
        const int *current = nullptr, *last_sent = nullptr;
        bool valid = true;
        NavigationState() = default;
        NavigationState(const NavigationState &other) { copy(other); }
        NavigationState &operator=(const NavigationState &other) {
            if (this != &other) { release(); copy(other); }
            return *this;
        }
        ~NavigationState() { release(); }
        unsigned encoded_size() const { return 20 + cycles * 2 + instruments * 8 + depth * 6; }
        bool encode(uint8_t *bytes, unsigned size) const {
            if (!valid || size != encoded_size() || (cycles && !cursors) || (depth && !history)
                || (instruments && (!current || !last_sent))) return false;
            put32(bytes, selected); put32(bytes + 4, cycles); put32(bytes + 8, instruments);
            put32(bytes + 12, depth); put32(bytes + 16, uint32_t(center));
            unsigned offset = 20;
            for (unsigned i = 0; i < cycles; ++i, offset += 2) put16(bytes + offset, cursors[i]);
            for (unsigned i = 0; i < instruments; ++i, offset += 8) {
                put32(bytes + offset, uint32_t(current[i])); put32(bytes + offset + 4, uint32_t(last_sent[i]));
            }
            for (unsigned i = 0; i < depth; ++i, offset += 6) {
                put16(bytes + offset, history[i].patch); put32(bytes + offset + 2, uint32_t(history[i].previous));
            }
            return true;
        }
        bool decode(const uint8_t *bytes, unsigned size) {
            if (size < 20) return false;
            const unsigned c = u32(bytes + 4), n = u32(bytes + 8), d = u32(bytes + 12);
            if (c > UINT16_MAX || n > UINT16_MAX
                || uint64_t(20) + uint64_t(c) * 2 + uint64_t(n) * 8 + uint64_t(d) * 6 != size
                || u32(bytes) > UINT16_MAX) return false;
            NavigationState candidate;
            if (!candidate.allocate(c, n, d)) return false;
            candidate.selected = uint16_t(u32(bytes)); candidate.center = int32_t(u32(bytes + 16));
            unsigned offset = 20;
            for (unsigned i = 0; i < c; ++i, offset += 2) candidate.writable_cursors()[i] = u16(bytes + offset);
            for (unsigned i = 0; i < n; ++i, offset += 8) {
                candidate.writable_current()[i] = int32_t(u32(bytes + offset));
                candidate.writable_last_sent()[i] = int32_t(u32(bytes + offset + 4));
            }
            for (unsigned i = 0; i < d; ++i, offset += 6)
                candidate.writable_history()[i] = {u16(bytes + offset), int32_t(u32(bytes + offset + 2))};
            *this = candidate;
            return true;
        }
    private:
        friend class Controller;
        static void put16(uint8_t *bytes, uint16_t value) { bytes[0] = uint8_t(value); bytes[1] = uint8_t(value >> 8); }
        struct Allocation { MemoryBuffer block; unsigned references = 1; };
        Allocation *allocation_ = nullptr;
        bool allocate(unsigned c, unsigned n, unsigned d) {
            cycles = c; instruments = n; depth = d;
            const uint64_t required = uint64_t(c) * 2 + uint64_t(n) * 8 + uint64_t(d) * sizeof(ProgramHistory);
            if (required > UINT_MAX - sizeof(Allocation)) { valid = false; return false; }
            const unsigned bytes = unsigned(required);
            if (!bytes) return true;
            MemoryBuffer block;
            if (!block.resize(sizeof(Allocation) + bytes)) { valid = false; return false; }
            allocation_ = new (block.data) Allocation;
            allocation_->block.swap(block);
            history = reinterpret_cast<ProgramHistory *>(allocation_->block.data + sizeof(Allocation));
            current = reinterpret_cast<const int *>(history + d); last_sent = current + n;
            cursors = reinterpret_cast<const uint16_t *>(last_sent + n);
            return true;
        }
        void copy(const NavigationState &other) {
            selected = other.selected; depth = other.depth; cycles = other.cycles; instruments = other.instruments; center = other.center;
            cursors = other.cursors; history = other.history; current = other.current; last_sent = other.last_sent;
            valid = other.valid; allocation_ = other.allocation_; if (allocation_) ++allocation_->references;
        }
        void release() {
            if (!allocation_ || --allocation_->references) return;
            MemoryBuffer block; block.swap(allocation_->block); allocation_->~Allocation(); allocation_ = nullptr;
        }
        uint16_t *writable_cursors() { return const_cast<uint16_t *>(cursors); }
        ProgramHistory *writable_history() { return const_cast<ProgramHistory *>(history); }
        int *writable_current() { return const_cast<int *>(current); }
        int *writable_last_sent() { return const_cast<int *>(last_sent); }
    };
    NavigationState navigation() const {
        NavigationState state; state.selected = uint16_t(selected_); state.center = center_;
        if (!state.allocate(document_.patch_cycles(), document_.instruments(), depth_)) return state;
        if (document_.patch_cycles()) std::memcpy(state.writable_cursors(), cursors_, document_.patch_cycles() * sizeof(uint16_t));
        unsigned history_index = 0;
        for (const auto &entry : history_) state.writable_history()[history_index++] = entry;
        if (document_.instruments()) {
            std::memcpy(state.writable_current(), current_, document_.instruments() * sizeof(int));
            std::memcpy(state.writable_last_sent(), last_sent_, document_.instruments() * sizeof(int));
        }
        return state;
    }
    bool navigation(const NavigationState &state) {
        if (!state.valid || state.cycles != document_.patch_cycles() || state.instruments != document_.instruments()
            || (state.cycles && !state.cursors) || (state.depth && !state.history)
            || (state.instruments && (!state.current || !state.last_sent))) return false;
        if (state.selected >= document_.patch_cycles() && (document_.patch_cycles() || state.selected)) return false;
        if (state.center < -1 || (state.center >= 0 && unsigned(state.center) >= document_.patches())) return false;
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
        if (!history_.reserve(state.depth)) return false;
        selected_ = state.selected; depth_ = state.depth; center_ = state.center;
        if (document_.patch_cycles()) std::memcpy(cursors_, state.cursors, document_.patch_cycles() * sizeof(uint16_t));
        history_.clear();
        for (unsigned i = 0; i < state.depth; ++i) (void)history_.append(state.history[i]);
        if (document_.instruments()) {
            std::memcpy(current_, state.current, document_.instruments() * sizeof(int));
            std::memcpy(last_sent_, state.last_sent, document_.instruments() * sizeof(int));
        }
        return true;
    }
    Controller(Hardware &hardware, PatchOutputs &outputs, PatchSources &sources, unsigned owner = 8)
        : hardware_(hardware), executor_(hardware, outputs, sources, owner) {}
    ~Controller() { executor_.cancel(); }
    bool prepare(const Document &document) {
        cancel_preparation();
        const unsigned instruments = document.valid() ? document.instruments() : 0;
        const unsigned cycles = document.valid() ? document.patch_cycles() : 0;
        if (instruments == state_instruments_ && cycles == state_cycles_) return true;
        if (!pending_state_.resize(instruments * 2 * sizeof(int) + cycles * 2 * sizeof(uint16_t))) return false;
        state_prepared_ = true;
        return true;
    }
    void cancel_preparation() { pending_state_.clear(); state_prepared_ = false; }
    void deactivate() {
        executor_.cancel(); sending_ = -1; cancel_gesture(); document_ = Document();
        state_.clear(); cancel_preparation(); state_instruments_ = state_cycles_ = 0;
        history_.release();
        selected_ = depth_ = preview_selected_ = preview_depth_ = 0; center_ = -1;
        current_ = last_sent_ = nullptr; cursors_ = preview_cursors_ = nullptr;
    }
    bool busy() const { return executor_.busy(); }
    void runtime_brightness(uint8_t value) { brightness_ = value; }
    PatchExecutor &executor() { return executor_; }
    void validation(bool enabled) {
        previewing_ = enabled;
        if (enabled) {
            preview_selected_ = selected_; preview_depth_ = depth_;
            if (state_cycles_) std::memcpy(preview_cursors_, cursors_, state_cycles_ * sizeof(uint16_t));
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
        if (!can_undo(depth)) { node = 65535; return true; }
        const ProgramHistory &a = history_[depth - 1];
        node = document_.browser_root(unsigned(a.previous));
        if (previewing_) {
            --preview_depth_; preview_selected_ = u16(document_.patch(a.patch) + 2);
            preview_cursors_[preview_selected_] = a.patch - u16(document_.patch_cycle(preview_selected_) + 64);
        }
        return true;
    }
    void selection_complete(unsigned operation) {
        if (operation != 8 || !can_undo(depth_)) return;
        const ProgramHistory &a = history_[depth_ - 1];
        unsigned instrument = u16(document_.patch(a.patch));
        current_[instrument] = a.previous;
        center_ = last_sent_[instrument] = a.previous;
        selected_ = u16(document_.patch(a.patch) + 2);
        cursors_[selected_] = a.patch - u16(document_.patch_cycle(selected_) + 64);
        --depth_;
        history_.pop_back();
        render();
    }
    void cancel_gesture() { release_c(); held_ = 0; reverse_ = false; }
    void refresh() { if (document_.valid()) render(); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(controller_activate)
#endif
    bool activate(const Document &document, bool configure = true) {
        if (!state_prepared_ && !prepare(document)) { hardware_.error("Controller memory unavailable"); return false; }
        executor_.cancel(); sending_ = -1;
        cancel_gesture();
        if (state_prepared_) { state_.swap(pending_state_); cancel_preparation(); }
        state_instruments_ = document.instruments(); state_cycles_ = document.patch_cycles();
        if (state_.data) {
            current_ = reinterpret_cast<int *>(state_.data); last_sent_ = current_ + state_instruments_;
            cursors_ = reinterpret_cast<uint16_t *>(last_sent_ + state_instruments_);
            preview_cursors_ = cursors_ + state_cycles_;
        }
        document_ = document;
        brightness_ = document.brightness(); selected_ = depth_ = 0;
        history_.release();
        for (unsigned i = 0; i < state_cycles_; ++i) cursors_[i] = 0;
        clear_programs(); center_ = -1; held_ = 0; reverse_ = false;
        if (configure) {
            hardware_.counters.reset();
            hardware_.configure_midi_streams(document);
            if (!hardware_.relays.configure(document.relay_records(), document.relay_count())) hardware_.error("Relay setup failed");
            hardware_.configure_trs_ports(document.trs_modes());
            hardware_.configure_page_settings(document.page_settings());
            hardware_.set_target_pulse(document.pulse_period_ms(), document.pulse_minimum(), document.pulse_maximum(), document.pulse_delay_ms());
            hardware_.set_brightness(document.brightness());
            hardware_.set_usb_midi(document.usb_device_midi(), document.usb_host_midi());
            hardware_.set_midi_interface(document.is_midi_interface());
            hardware_.set_animation_color_cycle(document.animation_color_cycle());
            hardware_.set_movies(document.movie_keys(false), document.movies(false), document.movie_keys(true), document.movies(true));
            hardware_.set_splash(document.splash(), document.screensaver_timeout());
        }
        if (configure) hardware_.configure_pages(document);
        render();
        return true;
    }
    // These six application actions are independent of physical inputs. Target
    // control bindings supply the action index for each declared input.
    void button(unsigned button, bool pressed, uint32_t now) {
        if (button >= 6) return;
        const unsigned mask = 1u << button;
        if (pressed) {
            if (held_ & mask) return;
            held_ |= mask;
            if (button == 0) {
                hardware_.tap_tempo(now);
            } else if (button == 3) {
                hardware_.browse_page(reverse_);
            } else if (button == 2) {
                c_held_ = true; action(button);
            } else if (button == 5 && (held_ & ((1u << 1) | (1u << 3) | (1u << 4)))) {
                reverse_ = true;
                if (held_ & (1u << 3)) hardware_.browse_page(true);
                for (unsigned i = 0; i < 2; ++i) {
                    unsigned nav = i ? 4 : 1;
                    if (held_ & (1u << nav)) { repeat_since_[i] = now; repeating_[i] = false; action(nav); }
                }
            } else {
                if (button == 1 || button == 4) {
                    unsigned i = button == 4;
                    repeat_since_[i] = now; repeating_[i] = false;
                }
                action(button);
            }
        } else {
            held_ &= ~mask;
            if (button == 2) release_c();
            if (button == 5 && reverse_) {
                reverse_ = false;
                for (unsigned i = 0; i < 2; ++i) { repeat_since_[i] = now; repeating_[i] = false; }
            }
        }
    }
    void tick(uint32_t now) {
        executor_.tick(now);
        if (sending_ >= 0 && c_held_ && executor_.started() && !flashing_) {
            flashing_ = true; hardware_.set_brightness(255, true);
        }
        finish_execution();
        if (!document_.valid()) return;
        if (counter_revision_ != hardware_.counters.revision()) {
            counter_revision_ = hardware_.counters.revision(); render();
        }
        for (unsigned i = 0; i < 2; ++i) {
            unsigned button = i ? 4 : 1;
            unsigned delay = repeating_[i] ? document_.navigation_repeat_ms() : document_.navigation_hold_ms();
            if ((held_ & (1u << button)) && uint32_t(now - repeat_since_[i]) >= delay && action(button)) {
                repeat_since_[i] = now; repeating_[i] = true;
            }
        }
    }
    bool action(unsigned button) {
        if (executor_.busy() || button >= 6) return false;
        static constexpr unsigned operations[] = {4, 3, 7, 6, 5, 8};
        unsigned operation = operations[button];
        if (reverse_ && (button == 1 || button == 4)) ++operation;
        return executor_.invoke(StatePatch{operation});
    }
    bool navigate(unsigned operation) {
        if (!document_.valid() || !document_.patch_cycles()) return false;
        if (operation == 5 || operation == 6) {
            if (document_.patch_cycles() > 1)
                scroll(2, operation == 6, 0);
            selected_ = (selected_ + document_.patch_cycles() + (operation == 6 ? -1 : 1)) % document_.patch_cycles();
        } else if (operation == 3 || operation == 4) {
            unsigned count = u16(document_.patch_cycle(selected_) + 66);
            if (count > 1)
                scroll(1, operation == 4, 0);
            cursors_[selected_] = uint16_t((cursors_[selected_] + count + (operation == 4 ? -1 : 1)) % count);
        } else if (operation == 7) {
            if (executor_.busy()) return false;
            unsigned p = candidate();
            if (already_sent(p)) return true;
            NavigationState before = navigation();
            if (!before.valid || depth_ == UINT_MAX || !history_.reserve(depth_ + 1)) { hardware_.error("Navigation memory unavailable"); return false; }
            if (u16(document_.patch(p)) == 65535) {
                depth_ = 0;
                history_.clear();
                clear_programs();
            }
            if (!executor_.start(document_, document_.browser_root(p), false, true)) { navigation(before); return false; }
            sending_ = int(p); undoing_ = false;
        } else if (operation == 8) {
            if (executor_.busy()) return false;
            if (!can_undo(depth_)) return true;
            const ProgramHistory &a = history_[depth_ - 1];
            sending_ = int(a.patch); undoing_ = true;
            if (!executor_.start(document_, document_.browser_root(unsigned(a.previous)), false, true)) { sending_ = -1; return false; }
        } else return false;
        render();
        return true;
    }
    unsigned depth() const { return depth_; }
    unsigned selected_patch() const { return candidate(); }
    const Document &document() const { return document_; }
    void external_selection() {
        cancel_gesture(); depth_ = 0; center_ = -1;
        history_.clear();
        clear_programs();
    }
private:
    unsigned counter_revision_ = 0;
    void scroll(unsigned action, bool reverse, int pane = -1) {
        const unsigned direction = document_.scroll_direction(action);
        hardware_.transition(reverse != bool(direction & 1), document_.scroll_ms(direction >= 2), pane, direction >= 2, direction == 0);
    }
    Hardware &hardware_;
    PatchExecutor executor_;
    Document document_;
    CheckedList<ProgramHistory> history_;
    MemoryBuffer state_, pending_state_;
    unsigned state_instruments_ = 0, state_cycles_ = 0;
    bool state_prepared_ = false;
    uint16_t *cursors_ = nullptr, *preview_cursors_ = nullptr;
    int *current_ = nullptr;
    // Unlike undo state, this changes only after a successful MIDI send.
    int *last_sent_ = nullptr;
    unsigned selected_ = 0, depth_ = 0, held_ = 0;
    unsigned preview_selected_ = 0, preview_depth_ = 0;
    int center_ = -1;
    int sending_ = -1;
    uint32_t repeat_since_[2] = {};
    bool repeating_[2] = {}, reverse_ = false;
    bool c_held_ = false, flashing_ = false, undoing_ = false, previewing_ = false;
    uint8_t brightness_ = 192;
    void clear_programs() {
        for (unsigned i = 0; i < state_instruments_; ++i) current_[i] = last_sent_[i] = -1;
    }
    bool move(unsigned operation, unsigned &selected, uint16_t *cursors) const {
        if (!document_.valid() || !document_.patch_cycles()) return false;
        if (operation == 5 || operation == 6)
            selected = (selected + document_.patch_cycles() + (operation == 6 ? -1 : 1)) % document_.patch_cycles();
        else if (operation == 3 || operation == 4) {
            unsigned count = u16(document_.patch_cycle(selected) + 66);
            cursors[selected] = uint16_t((cursors[selected] + count + (operation == 4 ? -1 : 1)) % count);
        } else return false;
        return true;
    }
    void release_c() {
        if (flashing_ && document_.valid()) hardware_.set_brightness(brightness_);
        flashing_ = c_held_ = false;
    }
    unsigned candidate() const { return document_.patch_cycles() ? u16(document_.patch_cycle(selected_) + 64) + cursors_[selected_] : 0; }
    bool can_undo(unsigned depth) const { return depth && history_[depth - 1].previous >= 0; }
    static uint16_t undo_color(uint16_t background) {
        const auto linear = [](unsigned value, unsigned maximum) {
            const float channel = float(value) / maximum;
            return channel <= 0.04045f ? channel / 12.92f : std::pow((channel + 0.055f) / 1.055f, 2.4f);
        };
        const float luminance = 0.2126f * linear(background >> 11, 31)
            + 0.7152f * linear((background >> 5) & 63, 63) + 0.0722f * linear(background & 31, 31);
        const auto contrasts = [luminance](float other) {
            return (std::max(luminance, other) + 0.05f) / (std::min(luminance, other) + 0.05f) >= 2;
        };
        if (contrasts(0.7152f)) return 0x07e0;
        if (contrasts(0.0722f)) return 0x001f;
        return contrasts(0) ? 0 : 0xffff;
    }
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
                history_.clear();
                clear_programs();
                if (executor_.status() == PatchExecutor::Status::unknown) {
                    render(); hardware_.error("Remote state unknown");
                }
            }
            return;
        }
        if (undoing_) { complete_undo(); render(); return; }
        unsigned p = unsigned(sending_), instrument = u16(document_.patch(p));
        if (instrument != 65535) {
            (void)history_.append({uint16_t(p), current_[instrument]}); ++depth_;
            current_[instrument] = last_sent_[instrument] = int(p);
        }
        center_ = int(p); sending_ = -1;
        scroll(0, false); render();
    }
    void complete_undo() {
        const ProgramHistory &a = history_[depth_ - 1];
        unsigned instrument = u16(document_.patch(a.patch));
        current_[instrument] = a.previous;
        center_ = last_sent_[instrument] = a.previous;
        selected_ = u16(document_.patch(a.patch) + 2);
        cursors_[selected_] = a.patch - u16(document_.patch_cycle(selected_) + 64);
        --depth_;
        history_.pop_back();
        sending_ = -1; scroll(0, true);
    }
    void pane(unsigned slot, int index) {
        Pane view;
        view.performance = true;
        view.graphics = document_.graphics(slot);
        if (index >= 0) {
            view.three_bands = true;
            const bool sent = already_sent(unsigned(index));
            view.pending_patch = slot == 0 && !sent ? index : -1;
            document_.page(view, unsigned(index), slot, slot == 0 && sent, &hardware_.counters);
            if (view.lines[0].text[0]) {
                view.heading_band = true;
                auto &heading = view.lines[0].graphics;
                const uint16_t foreground = heading.foreground;
                heading.foreground = heading.background;
                heading.background = foreground;
                if (slot == 0 && !executor_.busy() && can_undo(depth_)) {
                    const unsigned height = hardware_.display_height() / 3;
                    const unsigned pad = 12, diameter = height > 2 * pad ? height - 2 * pad : 0;
                    view.circles[0] = {uint16_t(pad + diameter / 2), uint16_t(pad + diameter / 2), uint16_t(diameter), 0, undo_color(heading.background), 0};
                }
            }
            if (slot == 2) for (auto &line : view.lines) line.stippled = true;
        }
        bool has_text = false;
        for (const auto &line : view.lines) if (line.text[0]) has_text = true;
        if (!has_text) { view.graphics.background = 0; view.pending_patch = -1; }
        hardware_.show(slot, view);
    }
    void render() {
        pane(0, document_.patches() ? int(candidate()) : -1);
        pane(1, center_);
        pane(2, depth_ ? history_[depth_ - 1].previous : -1);
    }
};
static_assert(sizeof(void *) != 4 || sizeof(Controller) == 360, "Update layout.estimate_ram for the Controller ABI");
} // namespace cntrl
