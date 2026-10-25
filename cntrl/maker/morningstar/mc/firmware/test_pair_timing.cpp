#include "button_timing/pair_timing.h"
#include <cassert>

int main() {
    PairTiming timing;
    assert(!timing.update(false, false, 0).ready);
    assert(!timing.update(true, false, 10).ready);
    auto a_first = timing.update(true, true, 37);
    assert(a_first.ready && a_first.first == 'A' && a_first.gap_ms == 27);
    assert(!timing.update(true, true, 100).ready);
    assert(!timing.update(false, true, 110).ready);
    assert(!timing.update(true, true, 120).ready);
    assert(!timing.update(false, false, 130).ready);

    assert(!timing.update(false, true, 140).ready);
    auto f_first = timing.update(true, true, 145);
    assert(f_first.ready && f_first.first == 'F' && f_first.gap_ms == 5);
    timing.update(false, false, 150);
    auto simultaneous = timing.update(true, true, 160);
    assert(simultaneous.ready && simultaneous.first == '=' && !simultaneous.gap_ms);

    // A non-overlapping tap must not pair with the next attempt.
    timing.update(false, false, 200);
    timing.update(true, false, 210);
    timing.update(false, false, 220);
    timing.update(false, true, 250);
    auto next = timing.update(true, true, 280);
    assert(next.ready && next.first == 'F' && next.gap_ms == 30);

    timing.update(false, false, UINT32_MAX - 10);
    timing.update(true, false, UINT32_MAX - 5);
    auto rollover = timing.update(true, true, 4);
    assert(rollover.ready && rollover.first == 'A' && rollover.gap_ms == 10);
}
