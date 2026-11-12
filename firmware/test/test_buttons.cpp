#include <cntrl/buttons.h>
#include <cntrl/switch_gestures.h>

#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <vector>

void test_configured_debounce() {
    for (uint16_t delay : std::initializer_list<uint16_t>{5, 20, 40}) {
        uint8_t settings[cntrl::gesture_record_size] = {};
        for (unsigned i = 0; i < cntrl::gesture_debounce_offset; ++i) settings[i] = 255;
        settings[cntrl::gesture_debounce_offset] = uint8_t(delay);
        cntrl::ButtonState button;
        cntrl::SwitchGestures gesture;
        gesture.reset(settings);
        std::vector<unsigned> events;
        auto sample = [&](bool down, uint32_t now) {
            auto edges = button.update(down, now, cntrl::u16(settings + cntrl::gesture_debounce_offset));
            if (edges & cntrl::press_event) gesture.input(true, now);
            if (edges & cntrl::release_event) gesture.input(false, now);
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

void test_fast_edges_suppress_press_and_release_bounce() {
    for (uint32_t delay : {5, 20, 40}) {
        cntrl::ButtonState button;
        auto sample = [&](bool down, uint32_t now) { return button.update(down, now, delay, true); };
        assert(sample(true, 100) == cntrl::press_event);
        assert(sample(false, 101) == cntrl::no_event);
        assert(sample(true, 102) == cntrl::no_event);
        assert(sample(false, 103) == cntrl::no_event);
        assert(sample(true, 104) == cntrl::no_event);
        assert(button.pressed);
        assert(sample(true, 104 + delay) == cntrl::no_event);
        assert(sample(false, 200) == cntrl::release_event);
        assert(sample(true, 201) == cntrl::no_event);
        assert(sample(false, 202) == cntrl::no_event);
        assert(sample(true, 203) == cntrl::no_event);
        assert(sample(false, 204) == cntrl::no_event);
        assert(!button.pressed);
        assert(sample(false, 204 + delay) == cntrl::no_event);
        assert(sample(true, 300) == (cntrl::press_event | cntrl::double_press_event));
        assert(sample(true, 800) == cntrl::hold_event);
        assert(sample(false, 801) == cntrl::release_event);
        // Bounce past the initial deadline extends settling rather than rearming.
        cntrl::ButtonState late_bounce;
        assert(late_bounce.update(true, 100, delay, true) == cntrl::press_event);
        assert(late_bounce.update(false, 100 + delay - 1, delay, true) == cntrl::no_event);
        assert(late_bounce.update(true, 100 + delay, delay, true) == cntrl::no_event);
        assert(late_bounce.update(false, 101 + delay, delay, true) == cntrl::no_event);
        assert(late_bounce.update(true, 102 + delay, delay, true) == cntrl::no_event);
        assert(late_bounce.update(true, 102 + 2 * delay, delay, true) == cntrl::no_event);
        assert(late_bounce.update(false, 103 + 3 * delay, delay, true) == cntrl::release_event);
    }
    // A release before press settling is accepted once it stays stable.
    cntrl::ButtonState short_tap;
    assert(short_tap.update(true, 0, 20, true) == cntrl::press_event);
    assert(short_tap.update(false, 5, 20, true) == cntrl::no_event);
    assert(short_tap.update(false, 24, 20, true) == cntrl::no_event);
    assert(short_tap.update(false, 25, 20, true) == cntrl::release_event);
    // Rearming and both immediate edges work across clock wrap.
    cntrl::ButtonState rollover;
    assert(rollover.update(true, UINT32_MAX - 10, 20, true) == cntrl::press_event);
    assert(rollover.update(false, UINT32_MAX - 9, 20, true) == cntrl::no_event);
    assert(rollover.update(true, UINT32_MAX - 8, 20, true) == cntrl::no_event);
    assert(rollover.update(true, 11, 20, true) == cntrl::no_event);
    assert(rollover.update(false, 12, 20, true) == cntrl::release_event);
}

int main() {
    test_configured_debounce();
    test_fast_edges_suppress_press_and_release_bounce();
    cntrl::ButtonState button;
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

    cntrl::ButtonState rollover;
    assert(!rollover.update(true, UINT32_MAX - 5));
    assert(!rollover.update(true, 8));
    assert(rollover.update(true, 9));

    cntrl::ButtonState hold;
    assert(hold.update(true, 0) == cntrl::no_event);
    assert(hold.update(true, 15) == cntrl::press_event);
    assert(hold.update(true, 514) == cntrl::no_event);
    assert(hold.update(true, 515) == cntrl::hold_event);
    assert(hold.update(true, 1000) == cntrl::no_event);
    assert(hold.update(false, 1001) == cntrl::no_event);
    assert(hold.update(false, 1016) == cntrl::release_event);
    assert(hold.update(true, 1020) == cntrl::no_event);
    assert(hold.update(true, 1035) == cntrl::press_event); // Holds do not start a double.

    cntrl::ButtonState double_press;
    assert(double_press.update(true, 0) == cntrl::no_event);
    assert(double_press.update(true, 15) == cntrl::press_event);
    assert(double_press.update(false, 30) == cntrl::no_event);
    assert(double_press.update(false, 45) == cntrl::release_event);
    assert(double_press.update(true, 330) == cntrl::no_event);
    assert(double_press.update(true, 345) == (cntrl::press_event | cntrl::double_press_event));
    assert(double_press.update(false, 350) == cntrl::no_event);
    assert(double_press.update(false, 365) == cntrl::release_event);
    assert(double_press.update(true, 380) == cntrl::no_event);
    assert(double_press.update(true, 395) == cntrl::press_event); // No overlapping pairs.

    cntrl::ButtonState expired;
    expired.update(true, 0);
    expired.update(true, 15);
    expired.update(false, 30);
    expired.update(false, 45);
    expired.update(true, 331);
    assert(expired.update(true, 346) == cntrl::press_event);

    cntrl::ButtonState hold_rollover;
    hold_rollover.update(true, UINT32_MAX - 100);
    assert(hold_rollover.update(true, UINT32_MAX - 85) == cntrl::press_event);
    assert(hold_rollover.update(true, 413) == cntrl::no_event);
    assert(hold_rollover.update(true, 414) == cntrl::hold_event);

    cntrl::ButtonState double_rollover;
    double_rollover.update(true, UINT32_MAX - 100);
    double_rollover.update(true, UINT32_MAX - 85);
    double_rollover.update(false, UINT32_MAX - 60);
    assert(double_rollover.update(false, UINT32_MAX - 45) == cntrl::release_event);
    double_rollover.update(true, 100);
    assert(double_rollover.update(true, 115) == (cntrl::press_event | cntrl::double_press_event));

    // Every chord, including all eight protocol inputs held together, retains independent edges.
    for (unsigned mask = 1; mask < 256; ++mask) {
        cntrl::ButtonState chord[8];
        for (unsigned i = 0; i < 8; ++i) {
            const bool down = mask & (1u << i);
            assert(chord[i].update(down, 0) == cntrl::no_event);
            assert(chord[i].update(down, 15) == (down ? cntrl::press_event : cntrl::no_event));
            assert(chord[i].update(down, 515) == (down ? cntrl::hold_event : cntrl::no_event));
            assert(chord[i].update(false, 520) == cntrl::no_event);
            assert(chord[i].update(false, 535) == (down ? cntrl::release_event : cntrl::no_event));
        }
    }
}
