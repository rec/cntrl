#pragma once
#include "midi_outputs.h"
#include <cstring>
#include <vector>

namespace cntrl {
class TextMessages {
public:
    static constexpr unsigned maximum = 80, duration_ms = 5000;
    enum class Kind : uint8_t { cue = 1, warning = 2 };
    const char *text() const { return text_; }
    Kind kind() const { return kind_; }
    uint32_t revision() const { return revision_; }
    bool active(uint32_t now) const { return visible_ && uint32_t(now - received_) < duration_ms; }
    void poll(uint32_t now) { visible_ = active(now); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(text_publish)
#endif
    bool publish(Kind kind, const char *text, unsigned length, uint32_t now) {
        if ((kind != Kind::cue && kind != Kind::warning) || !length || length > maximum) return false;
        for (unsigned i = 0; i < length; ++i) if (text[i] < 32 || text[i] > 126) return false;
        std::memcpy(text_, text, length); text_[length] = 0;
        kind_ = kind; received_ = now; visible_ = true; ++revision_;
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(text_command)
#endif
    const char *command(const char *line, unsigned length, uint32_t now) {
        if (length < 10 || std::memcmp(line, "CNTRL TEXT", 10)) return nullptr;
        Kind kind{}; unsigned offset = 0;
        if (length >= 15 && !std::memcmp(line, "CNTRL TEXT CUE ", 15)) { kind = Kind::cue; offset = 15; }
        else if (length >= 19 && !std::memcmp(line, "CNTRL TEXT WARNING ", 19)) { kind = Kind::warning; offset = 19; }
        const bool accepted = offset && publish(kind, line + offset, length - offset, now);
        return accepted ? "CNTRL OK" : "CNTRL ERROR text-message";
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(text_forget)
#endif
    void forget(unsigned input) {
        for (auto i = inputs_.begin(); i != inputs_.end();) {
            if (i->source / 16 == input) i = inputs_.erase(i);
            else ++i;
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(text_byte)
#endif
    void byte(unsigned input, unsigned cable, uint8_t byte, uint32_t now) {
        if (input >= midi::max_routes || cable >= 16 || byte >= 0xf8) return;
        const unsigned source = input * 16 + cable;
        auto state = inputs_.begin();
        while (state != inputs_.end() && state->source != source) ++state;
        if (byte == 0xf0) {
            if (state == inputs_.end()) { inputs_.push_back({}); state = inputs_.end() - 1; }
            *state = {}; state->source = source;
            return;
        }
        if (state == inputs_.end()) return;
        if (byte == 0xf7) {
            if (state->used > sizeof(header_))
                publish(Kind(state->kind), state->text, state->used - sizeof(header_), now);
            inputs_.erase(state); return;
        }
        if (byte >= 0x80 || state->used == sizeof(header_) + maximum
            || (state->used < sizeof(header_) - 1 && byte != header_[state->used])
            || (state->used == sizeof(header_) - 1 && byte != 1 && byte != 2)
            || (state->used >= sizeof(header_) && (byte < 32 || byte > 126))) {
            inputs_.erase(state); return;
        }
        if (state->used == sizeof(header_) - 1) state->kind = byte;
        else if (state->used >= sizeof(header_)) state->text[state->used - sizeof(header_)] = char(byte);
        ++state->used;
    }
private:
    struct Input { unsigned source = 0, used = 0; uint8_t kind = 0; char text[maximum] = {}; };
    std::vector<Input> inputs_;
    char text_[maximum + 1] = {};
    Kind kind_ = Kind::cue;
    uint32_t received_ = 0;
    uint32_t revision_ = 0;
    bool visible_ = false;
    // Development manufacturer, CNTRL, version, kind placeholder.
    inline static constexpr uint8_t header_[] = {0x7d, 0x43, 0x4e, 0x54, 0x52, 0x4c, 1, 0};
};
} // namespace cntrl
