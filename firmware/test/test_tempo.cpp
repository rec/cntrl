#include <cntrl/tempo.h>
#include <cassert>
#include <cstdint>

int main() {
    cntrl::TapTempo tap;
    tap.configure(4, 2, 100, 3000);
    assert(!tap.period() && !tap.flash(0, 300));
    tap.tap(100); assert(!tap.period());
    tap.tap(600); assert(tap.period() == 500);
    tap.tap(650); assert(tap.period() == 500); // Reject a stray double tap.
    tap.tap(1100); tap.tap(1610); tap.tap(2100);
    assert(tap.period() == 500);
    assert(tap.flash(2399, 300) && !tap.flash(2400, 300));
    assert(tap.flash(2600, 300));
    tap.tap(10000); assert(tap.period() == 500);
    tap.tap(11000); assert(tap.period() == 1000); // Long gap started a fresh sequence.
    tap.configure(2, 3, 100, 3000);
    tap.tap(UINT32_MAX - 100); tap.tap(399);
    assert(tap.period() == 1000); // Retain the previous result until enough new taps.
    tap.tap(899); assert(tap.period() == 500);
    assert(tap.flash(1198, 300) && !tap.flash(1199, 300));

    cntrl::ClockTempo clock;
    clock.configure(24, 1, 1000000);
    assert(!clock.period_us() && !clock.active());
    for (unsigned i = 0; i <= 24; ++i) {
        uint32_t now = 1000000 + i * 500000 / 24;
        clock.clock(1, now);
        clock.clock(2, now); // A second clock stream cannot double the tempo.
    }
    assert(clock.period_us() == 500000 && clock.active());
    clock.poll(2500000); assert(clock.active());
    clock.poll(2500001); assert(!clock.active() && clock.period_us() == 500000);
    clock.poll(1500000); assert(!clock.active()); // No false revival after timer rollover.
    clock.clock(2, UINT32_MAX - 100000);
    for (unsigned i = 1; i <= 24; ++i) clock.clock(2, UINT32_MAX - 100000 + i * 10000);
    assert(clock.active() && clock.period_us() == 240000);
    clock.configure(96, 1, 1000000);
    for (unsigned i = 0; i <= 96; ++i) clock.clock(3, 2000000 + i * 500000 / 96);
    assert(clock.period_us() == 500000); // Configurable clocks per quarter.
}
