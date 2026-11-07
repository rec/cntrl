#include "usb_probe/buttons.h"
#include <cntrl/switch_gestures.h>

#include <cassert>
#include <cstdint>
#include <vector>

void test_configured_debounce() {
    for (uint16_t delay : {5, 20, 40}) {
        uint8_t settings[20] = {};
        for (unsigned i = 0; i < 12; ++i) settings[i] = 255;
        settings[12] = uint8_t(delay);
        ButtonState button;
        cntrl::SwitchGestures gesture;
        gesture.reset(settings);
        std::vector<unsigned> events;
        auto sample = [&](bool down, uint32_t now) {
            auto edges = button.update(down, now, cntrl::u16(settings + 12));
            if (edges & press_event) gesture.input(true, now);
            if (edges & release_event) gesture.input(false, now);
            gesture.tick(now, true, [&](unsigned event) { events.push_back(event); });
        };
        sample(true, 0); sample(false, 2); sample(true, 4);
        sample(true, 3 + delay); assert(events.empty());
        sample(true, 4 + delay);
        assert((events == std::vector<unsigned>{0}));
        sample(false, 100); sample(true, 102); sample(false, 104);
        sample(false, 103 + delay); assert(events.size() == 1);
        sample(false, 104 + delay);
        assert((events == std::vector<unsigned>{0, 1}));
    }
}

int main() {
    test_configured_debounce();
    ButtonState button;
    assert(!button.update(false, 0));
    assert(!button.update(true, 3));
    assert(!button.update(false, 6));
    assert(!button.update(true, 9));
    assert(!button.update(true, 23));
    assert(button.update(true, 24));
    assert(button.pressed);
    assert(!button.update(true, 100));
    assert(!button.update(false, 103));
    assert(!button.update(false, 117));
    assert(button.update(false, 118));
    assert(!button.pressed);

    ButtonState rollover;
    assert(!rollover.update(true, UINT32_MAX - 5));
    assert(!rollover.update(true, 8));
    assert(rollover.update(true, 9));

    // Each physical bit selects exactly its corresponding label.
    for (unsigned i = 0; i < 6; ++i) {
        const uint8_t raw = uint8_t(0xffu ^ (1u << button_bits[i]));
        for (unsigned j = 0; j < 6; ++j) {
            ButtonState independent;
            const bool pressed = !(raw & (1u << button_bits[j]));
            assert(!independent.update(pressed, 0));
            assert(independent.update(pressed, 15) == (i == j));
            assert(independent.pressed == (i == j));
        }
    }

    ButtonState hold;
    assert(hold.update(true, 0) == no_event);
    assert(hold.update(true, 15) == press_event);
    assert(hold.update(true, 514) == no_event);
    assert(hold.update(true, 515) == hold_event);
    assert(hold.update(true, 1000) == no_event);
    assert(hold.update(false, 1001) == no_event);
    assert(hold.update(false, 1016) == release_event);
    assert(hold.update(true, 1020) == no_event);
    assert(hold.update(true, 1035) == press_event); // Holds do not start a double.

    ButtonState double_press;
    assert(double_press.update(true, 0) == no_event);
    assert(double_press.update(true, 15) == press_event);
    assert(double_press.update(false, 30) == no_event);
    assert(double_press.update(false, 45) == release_event);
    assert(double_press.update(true, 330) == no_event);
    assert(double_press.update(true, 345) == (press_event | double_press_event));
    assert(double_press.update(false, 350) == no_event);
    assert(double_press.update(false, 365) == release_event);
    assert(double_press.update(true, 380) == no_event);
    assert(double_press.update(true, 395) == press_event); // No overlapping pairs.

    ButtonState expired;
    expired.update(true, 0);
    expired.update(true, 15);
    expired.update(false, 30);
    expired.update(false, 45);
    expired.update(true, 331);
    assert(expired.update(true, 346) == press_event);

    ButtonState hold_rollover;
    hold_rollover.update(true, UINT32_MAX - 100);
    assert(hold_rollover.update(true, UINT32_MAX - 85) == press_event);
    assert(hold_rollover.update(true, 413) == no_event);
    assert(hold_rollover.update(true, 414) == hold_event);

    ButtonState double_rollover;
    double_rollover.update(true, UINT32_MAX - 100);
    double_rollover.update(true, UINT32_MAX - 85);
    double_rollover.update(false, UINT32_MAX - 60);
    assert(double_rollover.update(false, UINT32_MAX - 45) == release_event);
    double_rollover.update(true, 100);
    assert(double_rollover.update(true, 115) == (press_event | double_press_event));

    // Every chord, including all six held together, retains independent edges.
    for (unsigned mask = 1; mask < 64; ++mask) {
        ButtonState chord[6];
        for (unsigned i = 0; i < 6; ++i) {
            const bool down = mask & (1u << i);
            assert(chord[i].update(down, 0) == no_event);
            assert(chord[i].update(down, 15) == (down ? press_event : no_event));
            assert(chord[i].update(down, 515) == (down ? hold_event : no_event));
            assert(chord[i].update(false, 520) == no_event);
            assert(chord[i].update(false, 535) == (down ? release_event : no_event));
        }
    }
}
