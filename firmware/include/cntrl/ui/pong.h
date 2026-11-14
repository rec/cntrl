#pragma once
#include <algorithm>
#include <cstdint>

namespace cntrl::ui {
// A fixed 480 x 128 court is split into three independently clipped panes.
struct Pong {
    enum class State : uint8_t { ready, playing, paused, finished };
    enum class Button { left_up, left_down, right_up, right_down, serve };
    static constexpr float width = 480, top = 26, bottom = 113, paddle_height = 26;
    float left = 69.5f, right = 69.5f, x = 240, y = 69.5f, vx = 150, vy = 45;
    uint16_t left_score = 0, right_score = 0;
    State state = State::ready;

#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(pong_button)
#endif
    void button(Button input, bool pressed) {
        if (input != Button::serve) {
            const auto bit = uint8_t(1u << unsigned(input));
            if (pressed) held_ |= bit; else held_ &= uint8_t(~bit);
        } else if (pressed) {
            if (state == State::playing) state = State::paused;
            else {
                if (state == State::finished) { left_score = right_score = 0; round(1); }
                state = State::playing;
            }
        }
    }
    void clock(uint32_t now) { updated_ = now; }
    void leave() {
        held_ = 0;
        if (state == State::playing) state = State::paused;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(pong_advance)
#endif
    void advance(uint32_t now) {
        // Do not fast-forward the game after a suspended host or hidden page.
        if (uint32_t(now - updated_) > 200) updated_ = now - 200;
        while (uint32_t(now - updated_) >= 10) { updated_ += 10; step(); }
    }
private:
    uint32_t updated_ = 0;
    uint8_t held_ = 0;
    void round(float direction) {
        x = width / 2; y = (top + bottom) / 2;
        vx = direction * 150; vy = (left_score + right_score) % 2 ? -45 : 45;
        state = left_score == 11 || right_score == 11 ? State::finished : State::ready;
    }
    void bounce(float paddle, float direction) {
        vx = direction * std::min(260.0f, (vx < 0 ? -vx : vx) + 8);
        vy = (y - paddle) * 7;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(pong_step)
#endif
    void step() {
        constexpr float half = paddle_height / 2, radius = 2;
        left = std::clamp(left + 1.4f * (int(bool(held_ & 2)) - int(bool(held_ & 1))), top + half, bottom - half);
        right = std::clamp(right + 1.4f * (int(bool(held_ & 8)) - int(bool(held_ & 4))), top + half, bottom - half);
        if (state != State::playing) return;
        const float previous_x = x;
        x += vx * .01f; y += vy * .01f;
        if (y < top + radius) { y = 2 * (top + radius) - y; vy = -vy; }
        if (y > bottom - radius) { y = 2 * (bottom - radius) - y; vy = -vy; }
        if (vx < 0 && previous_x >= 13 && x <= 13 && y >= left - half - radius && y <= left + half + radius) {
            x = 26 - x; bounce(left, 1);
        }
        if (vx > 0 && previous_x <= width - 13 && x >= width - 13 && y >= right - half - radius && y <= right + half + radius) {
            x = 2 * (width - 13) - x; bounce(right, -1);
        }
        if (x < -radius) { ++right_score; round(-1); }
        else if (x > width + radius) { ++left_score; round(1); }
    }
};
} // namespace cntrl::ui
