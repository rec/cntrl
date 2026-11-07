#include <cntrl/ui/pixel_throb.h>
using cntrl::ui::PixelThrob;
#include <cassert>
#include <cstdint>

int main() {
    PixelThrob pulse;
    pulse.configure(4000, 64, 128, 0);
    assert(pulse.brightness(-1, 0, 192, 0) == 192);
    const uint32_t start = UINT32_MAX - 400;
    assert(pulse.brightness(0, start, 192, 0) == 128);
    unsigned previous = 128;
    for (unsigned i = 1; i <= 4000; ++i) {
        const unsigned value = pulse.brightness(0, start + i, 192, 0);
        assert(value >= 64 && value <= 128);
        assert(i <= 2000 ? value <= previous : value >= previous);
        if (i <= 20 || i >= 3980) assert(value == 128);
        if (i >= 1980 && i <= 2020) assert(value == 64);
        if (i == 1000 || i == 3000) assert(value == 96);
        previous = value;
    }
    assert(pulse.brightness(0, start + 6000, 192, 0) == 64);
    assert(pulse.brightness(-1, start + 6001, 255, 0) == 255);
    assert(pulse.brightness(0, start + 6500, 192, 0) == 128);
    assert(pulse.brightness(0, start + 8500, 192, 0) == 64);
    assert(pulse.brightness(1, start + 8501, 192, 0) == 128);
    assert(pulse.brightness(1, start + 9501, 192, 0) == 96);
    assert(pulse.brightness(2, start + 9502, 192, 0) == 128);
    assert(pulse.brightness(2, start + 11502, 192, 0) == 64);
    assert(pulse.brightness(-1, start + 11503, 192, 0) == 192);
    pulse.configure(2000, 20, 100, 0);
    assert(pulse.brightness(0, 0, 128, 0) == 100);
    assert(pulse.brightness(0, 500, 128, 0) == 60);
    assert(pulse.brightness(0, 1000, 128, 0) == 20);
    assert(pulse.brightness(0, 2000, 128, 0) == 100);
    pulse.configure(4000, 64, 128, 0);
    assert(pulse.brightness(0, 0, 128, 0) == 128);
    assert(pulse.brightness(0, 2000, 128, 0) == 64);
    for (unsigned mask = 1; mask < 64; ++mask)
        assert(pulse.brightness(0, 2000 + mask * 100, 128, uint8_t(mask)) == 128);
    assert(pulse.brightness(0, 10000, 128, 0) == 128);
    assert(pulse.brightness(0, 12000, 128, 0) == 64);
    pulse.configure(4000, 64, 128, 2000);
    const uint32_t ready = UINT32_MAX - 1000;
    assert(pulse.brightness(0, ready, 128, 0) == 128);
    assert(pulse.brightness(0, ready + 1999, 128, 0) == 128);
    assert(pulse.brightness(0, ready + 2000, 128, 0) == 128);
    assert(pulse.brightness(0, ready + 4000, 128, 0) == 64);
    assert(pulse.brightness(0, ready + 6000, 128, 0) == 128);
    assert(pulse.brightness(0, ready + 8000, 128, 0) == 64);
    assert(pulse.brightness(1, ready + 8001, 128, 0) == 128);
    assert(pulse.brightness(1, ready + 10000, 128, 0) == 128);
    assert(pulse.brightness(1, ready + 12001, 128, 0) == 64);
    assert(pulse.brightness(1, ready + 12002, 128, 2) == 128);
    assert(pulse.brightness(1, ready + 15000, 128, 0) == 128);
    assert(pulse.brightness(1, ready + 16999, 128, 0) == 128);
    assert(pulse.brightness(1, ready + 19000, 128, 0) == 64);
    assert(pulse.brightness(-1, ready + 19001, 128, 0) == 128);
    assert(pulse.brightness(1, ready + 20000, 128, 0) == 128);
    assert(pulse.brightness(1, ready + 21999, 128, 0) == 128);

}
