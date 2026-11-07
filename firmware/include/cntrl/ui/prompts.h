#pragma once
#include <cntrl/hardware.h>
#include <cstdio>
#include <cstring>

namespace cntrl::ui {
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
private:
    const Hardware &hardware_;
    char bytes_[64] = {};
    void append(const char *label, const char *action, const char *separator) {
        const auto used = std::strlen(bytes_);
        std::snprintf(bytes_ + used, sizeof(bytes_) - used, "%s%s %s", used ? separator : "", label, action);
    }
};
} // namespace cntrl::ui
