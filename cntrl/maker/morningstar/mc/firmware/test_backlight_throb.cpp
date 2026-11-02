#include "patch_switcher/backlight_throb.h"
#include <cassert>
#include <cstdint>

int main() {
    BacklightThrob pulse;
    assert(pulse.brightness(-1, 0, 192) == 192);
    const uint32_t start = UINT32_MAX - 400;
    assert(pulse.brightness(0, start, 192) == 128);
    unsigned previous = 128;
    for (unsigned i = 1; i <= 4000; ++i) {
        const unsigned value = pulse.brightness(0, start + i, 192);
        assert(value >= 16 && value <= 128);
        assert(i <= 2000 ? value <= previous : value >= previous);
        if (i <= 20 || i >= 3980) assert(value == 128);
        if (i >= 1980 && i <= 2020) assert(value == 16);
        if (i == 1000 || i == 3000) assert(value == 72);
        previous = value;
    }
    assert(pulse.brightness(0, start + 6000, 192) == 16);
    assert(pulse.brightness(-1, start + 6001, 255) == 255);
    assert(pulse.brightness(0, start + 6500, 192) == 128);
    assert(pulse.brightness(0, start + 8500, 192) == 16);
    assert(pulse.brightness(1, start + 8501, 192) == 128);
    assert(pulse.brightness(1, start + 9501, 192) == 72);
    assert(pulse.brightness(2, start + 9502, 192) == 128);
    assert(pulse.brightness(2, start + 11502, 192) == 16);
    assert(pulse.brightness(-1, start + 11503, 192) == 192);
}
