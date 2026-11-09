#include "boot_switch/gesture.h"
#include <cassert>

int main() {
    SwitchGesture<20> shorter;
    shorter.update(0, 0);
    shorter.update(1, 1);
    shorter.update(1, 16);
    shorter.update(3, 21);
    assert(shorter.update(3, 36).activate);
    SwitchGesture<20> shorter_late;
    shorter_late.update(0, 0);
    shorter_late.update(1, 1);
    shorter_late.update(1, 16);
    shorter_late.update(3, 22);
    assert(!shorter_late.update(3, 37).activate);
    for (unsigned first = 1; first <= 2; ++first) {
        SwitchGesture<50> gesture;
        gesture.update(0, 0);
        for (uint32_t t = 1; t < 81; ++t) {
            auto result = gesture.update(uint8_t(t < 51 ? first : 3), t);
            assert(result.activate == (t == 66));
            if (t < 66) assert(!result.pressed);
        }
    }
    SwitchGesture<50> late;
    late.update(0, 0);
    for (uint32_t t = 1; t < 100; ++t)
        assert(!late.update(t < 52 ? 1 : 3, t).activate);

    // Preserve both edges of a short single, shifted by exactly 65 ms.
    SwitchGesture<50> single;
    single.update(0, 0);
    for (uint32_t t = 1; t < 160; ++t) {
        auto result = single.update(t >= 10 && t < 35 ? 1 : 0, t);
        assert(!result.activate);
        assert(result.pressed == (t >= 75 && t < 100 ? 1 : 0));
    }

    SwitchGesture<50> simultaneous;
    simultaneous.update(0, 0);
    for (uint32_t t = 1; t <= 16; ++t)
        assert(simultaneous.update(3, t).activate == (t == 16));

    SwitchGesture<50> startup;
    for (uint32_t t = 0; t < 100; ++t)
        assert(!startup.update(3, t).activate);
    startup.update(0, 100);
    startup.update(0, 115);
    startup.update(3, 116);
    assert(startup.update(3, 131).activate);

    SwitchGesture<50> bounce;
    bounce.update(0, 0);
    for (uint32_t t = 1; t < 30; ++t)
        assert(!bounce.update(t % 2 ? 3 : 0, t).activate);

    SwitchGesture<50> rollover;
    rollover.update(0, UINT32_MAX - 20);
    rollover.update(1, UINT32_MAX - 19);
    rollover.update(1, UINT32_MAX - 4);
    rollover.update(3, 10);
    assert(rollover.update(3, 25).activate);

    // A failed persistence attempt must not replay its swallowed input later.
    simultaneous.cancel();
    for (uint32_t t = 100; t < 250; ++t) {
        auto result = simultaneous.update(0, t);
        assert(!result.activate && !result.pressed);
    }
    simultaneous.update(3, 250);
    assert(simultaneous.update(3, 265).activate);
}
