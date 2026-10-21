#include "usb_probe/buttons.h"

#include <cassert>
#include <cstdint>

int main() {
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
}
