#include <cntrl/ui/pong.h>
#include <cassert>

using cntrl::ui::Pong;

int main() {
    Pong game;
    game.button(Pong::Button::left_up, true);
    for (unsigned now = 200; now <= 1000; now += 200) game.advance(now);
    assert(game.left == Pong::top + Pong::paddle_height / 2);
    game.button(Pong::Button::left_down, true);
    game.advance(1200);
    assert(game.left == Pong::top + Pong::paddle_height / 2);
    game.leave();
    game.button(Pong::Button::serve, true);
    game.x = 158; game.vx = 150; game.vy = 0;
    game.advance(1220);
    assert(game.x > 160 && game.state == Pong::State::playing);
    const float stopped = game.x;
    game.button(Pong::Button::serve, true);
    game.advance(1420);
    assert(game.x == stopped && game.state == Pong::State::paused);
    game.button(Pong::Button::serve, true);
    game.x = 14; game.y = game.left; game.vx = -150;
    game.advance(1440);
    assert(game.vx > 0 && game.x >= 13);
    game.x = 466; game.y = game.right; game.vx = 150;
    game.advance(1460);
    assert(game.vx < 0 && game.x <= 467);
    game.y = Pong::top + 2; game.vy = -100; game.x = 240;
    game.advance(1480);
    assert(game.vy > 0 && game.y >= Pong::top + 2);
    game.x = 480; game.y = Pong::top + 3; game.vx = 150; game.vy = 0;
    game.left_score = 10;
    game.advance(1520);
    assert(game.left_score == 11 && game.state == Pong::State::finished);
    game.button(Pong::Button::serve, true);
    assert(game.left_score == 0 && game.right_score == 0 && game.state == Pong::State::playing);
    game.button(Pong::Button::right_down, true);
    game.leave();
    const float right = game.right;
    game.advance(1720);
    assert(game.right == right && game.state == Pong::State::paused);
    game.clock(UINT32_MAX - 5);
    game.button(Pong::Button::serve, true);
    const float before_wrap = game.x;
    game.advance(15);
    assert(game.x > before_wrap);
}
