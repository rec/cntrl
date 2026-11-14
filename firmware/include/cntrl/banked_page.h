#pragma once
#include "switches.h"

namespace cntrl {
// Each page owns its bindings and playback engines, including while hidden.
class BankedPage : public CheckedHeapAllocation {
public:
    BankedPage(Hardware &hardware, PatchOutputs &outputs, PatchSources &sources, unsigned page)
        : hardware_(hardware), outputs_(outputs), switches_(hardware, outputs, sources, page, 264 + page * Switches::capacity) {}
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(banked_page_configure)
#endif
    bool configure(const Document &source, unsigned page) {
        if (!storage_.resize(source.length())) return false;
        std::memcpy(storage_.data, source.data(), source.length());
        if (!document_.load(storage_.data, storage_.size, hardware_)) return false;
        const uint8_t *view = document_.banked_pages().find(page);
        if (!view) return false;
        node_ = u16(view + 2);
        record_ = document_.node(node_) + 4;
        bank_ = u16(record_ + 4);
        return true;
    }
    void refresh() { if (active_) switches_.refresh(); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(banked_patch_activate)
#endif
    bool activate(uint32_t held) {
        if (active_) return true;
        held_ = held;
        return active_ = enter();
    }
    bool matches(const Document &document, unsigned node) const {
        return node_ == node && u32(document_.data() + document_.core_offset() + 20) == u32(document.data() + document.core_offset() + 20);
    }
    unsigned revision() const { return switches_.revision(); }
    ExpressionAssignment expression(unsigned input) const { return switches_.expression(input); }
    bool navigate(unsigned operation) {
        if (operation < 14 || operation > 17 || pending_) return false;
        pending_ = operation;
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(banked_page_button)
#endif
    void button(unsigned input, bool down, uint32_t now) {
        if (!active_ || input >= 32) return;
        const uint32_t bit = 1u << input;
        held_ = down ? held_ | bit : held_ & ~bit;
        if (blocked_ & bit) { if (!down) blocked_ &= ~bit; return; }
        if (!down) {
            if (waiting_ & bit) { waiting_ &= ~bit; switches_.button(input, true, now); }
            switches_.button(input, false, now); return;
        }
        const uint8_t *chords = BankedPages::bank(record_, u16(record_ + 2));
        bool chord_input = false;
        for (unsigned i = 0; i < u16(record_ + 6); ++i) {
            const auto *chord = chords + i * 5;
            const uint32_t mask = u32(chord + 1);
            chord_input |= bool(mask & bit);
            if ((held_ & mask) == mask && (waiting_ & (mask & ~bit)) == (mask & ~bit)) {
                waiting_ &= ~mask; blocked_ |= mask;
                if (chord[0] == 9) outputs_.state(9, 0); else navigate(chord[0]);
                return;
            }
        }
        if (chord_input) { waiting_ |= bit; deadlines_[input] = now + 30; }
        else switches_.button(input, true, now);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(banked_page_tick)
#endif
    void tick(uint32_t now, uint32_t held) {
        if (!active_) return;
        for (unsigned i = 0; i < 32; ++i) if ((held_ & ~held) & (1u << i)) button(i, false, now);
        for (unsigned i = 0; i < 32; ++i) if ((waiting_ & (1u << i)) && int32_t(now - deadlines_[i]) >= 0) {
            waiting_ &= ~(1u << i); switches_.button(i, true, now);
        }
        switches_.tick(now);
        if (!pending_) return;
        if (!leaving_) { switches_.close(); leaving_ = true; }
        if (switches_.open()) return;
        const unsigned count = u16(record_ + 2);
        if (pending_ == 14 || pending_ == 15) {
            bank_ = (bank_ + (pending_ == 14 ? 1 : count - 1)) % count; page_ = 0;
        } else {
            const unsigned pages = u16(BankedPages::bank(record_, bank_));
            page_ = (page_ + (pending_ == 16 ? 1 : pages - 1)) % pages;
        }
        pending_ = 0; leaving_ = false; waiting_ = 0;
        if (!enter()) hardware_.error("Bank page memory unavailable");
    }
private:
    Hardware &hardware_;
    PatchOutputs &outputs_;
    MemoryBuffer storage_;
    Document document_;
    Switches switches_;
    const uint8_t *record_ = nullptr;
    unsigned bank_ = 0, page_ = 0, pending_ = 0, node_ = 0;
    uint32_t held_ = 0, blocked_ = 0, waiting_ = 0, deadlines_[32] = {};
    bool leaving_ = false, active_ = false;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(banked_page_enter)
#endif
    bool enter() {
        const unsigned node = u16(BankedPages::bank(record_, bank_) + 2 + page_ * 2);
        switches_.bank_context(bank_, page_);
        return switches_.enter(document_, held_, ControlBindings{document_.node(node) + 4});
    }
};
}
