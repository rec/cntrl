#pragma once
#include <cntrl/hardware.h>
#include "messages.h"
#include "utf8.h"
#include <cstdio>
#include <cstring>

namespace cntrl::ui {
inline void copy_caption(TextLine &line, const char *text) {
#ifdef CNTRL_EMULATOR
    line.translation_id = uint16_t(i18n::identity(text));
    line.translation_short = false;
#endif
    utf8::copy(line.text, sizeof(line.text), text);
}

class Prompt {
public:
    explicit Prompt(const Hardware &hardware) : hardware_(hardware) {}
    Prompt &clear() { bytes_[0] = 0; return *this; }
    Prompt &add(ControlRole role, const char *action, const char *separator = " ") {
        const char *label = hardware_.control_label(role);
        if (label[0]) append(label, action, separator);
        return *this;
    }
    Prompt &pair(ControlRole first, ControlRole second, const char *action, const char *separator = " ") {
        const char *a = hardware_.control_label(first), *b = hardware_.control_label(second);
        if (!a[0]) return add(second, action, separator);
        if (!b[0]) return add(first, action, separator);
        char labels[64]; std::snprintf(labels, sizeof(labels), "%s/%s", a, b); append(labels, action, separator); return *this;
    }
    const char *text() const { return bytes_; }
    Prompt &chord(uint8_t mask, const char *action) {
        char buttons[64] = {};
        for (unsigned i = 0; i < hardware_.input_count(); ++i) if (mask & (1u << i)) {
            const auto used = std::strlen(buttons);
            std::snprintf(buttons + used, sizeof(buttons) - used, "%s%s", used ? "+" : "", hardware_.input_label(i));
        }
        append(buttons, action, " "); return *this;
    }
private:
    const Hardware &hardware_;
    char bytes_[64] = {};
    void append(const char *label, const char *action, const char *separator) {
        const auto used = std::strlen(bytes_);
        i18n::format(bytes_ + used, unsigned(sizeof(bytes_) - used), i18n::Message::control_action,
                     used ? separator : "", label, action);
    }
};
} // namespace cntrl::ui
