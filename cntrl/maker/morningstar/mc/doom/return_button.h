// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>

class ReturnButton {
public:
    enum class Action { none, use, restart };
    static constexpr uint8_t mask = 1u << 5; // F on the MC6 Pro switch register.
    static constexpr uint32_t hold_ms = 2000;

    Action sample(bool pressed, uint32_t now)
    {
        if (pressed == held_)
            return held_ && now - started_ >= hold_ms ? Action::restart : Action::none;
        held_ = pressed;
        if (pressed) {
            started_ = now;
            return Action::none;
        }
        return now - started_ >= hold_ms ? Action::restart : Action::use;
    }

private:
    bool held_ = false;
    uint32_t started_ = 0;
};
