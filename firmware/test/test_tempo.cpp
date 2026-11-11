#include <cntrl/tempo.h>
#include <cassert>
#include <cstdint>

int main() {
    cntrl::TapTempo tap;
    tap.configure(4, 2, 100, 3000);
    assert(!tap.period() && !tap.flash(0, 400));
    tap.tap(100); assert(!tap.period());
    tap.tap(600); assert(tap.period() == 500);
    tap.tap(650); assert(tap.period() == 500); // Reject a stray double tap.
    tap.tap(1100); tap.tap(1610); tap.tap(2100);
    assert(tap.period() == 500);
    assert(tap.flash(2299, 400) && !tap.flash(2300, 400));
    assert(tap.flash(2600, 400));
    tap.tap(10000); assert(tap.period() == 500);
    tap.tap(11000); assert(tap.period() == 1000); // Long gap started a fresh sequence.
    assert(tap.flash(11399, 400) && !tap.flash(11400, 400));
    assert(tap.flash(12000, 400));
    assert(tap.flash(12249, 250) && !tap.flash(12250, 250));
    tap.configure(2, 3, 100, 3000);
    tap.tap(UINT32_MAX - 100); tap.tap(399);
    assert(tap.period() == 1000); // Retain the previous result until enough new taps.
    tap.tap(899); assert(tap.period() == 500);
    assert(tap.flash(1098, 400) && !tap.flash(1099, 400));

    cntrl::TapTempo fast;
    fast.configure(4, 2, 100, 3000);
    for (uint32_t now = 100; now <= 660; now += 140) fast.tap(now);
    assert(fast.period() == 140); // Approximately 429 BPM.
    fast.tap(2848); // Under three seconds, but far beyond the established beat.
    assert(fast.period() == 140); // Previously this single tap changed it to 92 BPM.
    fast.tap(3348);
    assert(fast.period() == 500); // Two fresh taps can establish a slower tempo.
    fast.tap(4348);
    assert(fast.period() == 750); // Exactly two beats is still part of the sequence.
    fast.tap(5849);
    assert(fast.period() == 750);
    fast.tap(6049);
    assert(fast.period() == 200); // Two fresh taps can also establish a faster tempo.

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
    clock.transport(3,0xfc,2500000);
    clock.poll(5000000);
    assert(!clock.beat_position(5000000).running && clock.available());
    clock.transport(3,0xfb,5000000);
    assert(clock.available() && !clock.beat_position(5000000).running);
    clock.clock(3,5000100);
    assert(clock.beat_position(5000100).running);
    clock.transport(3,0xfb,5000200);
    clock.poll(6000201);
    assert(!clock.available()); // Continue without subsequent clock is loss.
}
