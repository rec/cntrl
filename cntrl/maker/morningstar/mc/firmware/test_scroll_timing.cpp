#include "patch_switcher/scroll_timing.h"
#include <cassert>
#include <vector>

static uint32_t now = UINT32_MAX - 5000;
static unsigned writes = 0, initialized = 0;
static bool measure = false;
static unsigned expected_column = 0;
static std::vector<unsigned> offsets, lights;

void initialize_display_panorama() { ++initialized; }
void timing_scroll_panel(unsigned panel, unsigned offset) {
    assert(panel == 1);
    offsets.push_back(offset);
}
void timing_backlight(unsigned value) { lights.push_back(value); }
void write_display_column(unsigned panel, unsigned column, const uint8_t *pixels) {
    assert(panel < 3 && column < 162);
    ++writes;
    if (!measure) return;
    assert(panel == 1 && column == expected_column);
    for (unsigned y = 0; y < 128; ++y) {
        uint8_t expected = y % 16 < 12 ? 255 : 0;
        assert(pixels[2 * y] == expected && pixels[2 * y + 1] == expected);
    }
}

static void paint(ScrollTimingProbe &probe) {
    unsigned before = writes;
    for (unsigned i = 0; i < 486; ++i) { now += 10; probe.poll(now); }
    assert(writes == before + 486);
}

int main() {
    ScrollTimingProbe probe;
    probe.poll(now); assert(writes == 0);
    probe.arm(); paint(probe);
    assert(probe.active());
    probe.poll(now + 2000000); assert(offsets.empty());
    probe.start();
    constexpr unsigned delays[] = {2000, 5000};
    for (unsigned trial = 0; trial < 32; ++trial) {
        paint(probe);
        unsigned before = writes;
        uint32_t trigger = now + 800000 + trial * 1999;
        measure = true; expected_column = trial < 16 ? 0 : 159;
        probe.poll(trigger - 1); assert(offsets.size() == trial && writes == before);
        probe.poll(trigger);
        assert(offsets.size() == trial + 1 && offsets.back() == (trial < 16 ? 3 : 159));
        unsigned delay = delays[trial % 2];
        if (delay) {
            assert(writes == before);
            probe.poll(trigger + delay - 1); assert(writes == before);
            probe.poll(trigger + delay);
        }
        assert(writes == before + 1 && lights.back() == 192);
        probe.poll(trigger + 699999); assert(writes == before + 1);
        now = trigger + 700000; probe.poll(now);
        measure = false;
    }
    paint(probe); // Return to PRESS C, ready for another recording.
    assert(initialized == 34);
    unsigned before = writes;
    probe.poll(now + 2000000); assert(writes == before);
    probe.start(); paint(probe);
    probe.stop(); probe.poll(now + 2000000);
    assert(!probe.active() && writes == before + 486);
}
