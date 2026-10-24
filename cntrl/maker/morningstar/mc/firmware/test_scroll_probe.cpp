#include "scroll_probe/pattern.h"
#include <array>
#include <cassert>

void verify_windows(unsigned gap) {
    using namespace scroll_probe;
    assert(panel_origin(0, gap) == 0);
    assert(panel_origin(1, gap) - panel_width == gap);
    assert(panel_origin(2, gap) - (panel_origin(1, gap) + panel_width) == gap);
    unsigned stride = 1;
    for (unsigned mode = 0; mode < 6; ++mode) {
        assert(stride == 1u << mode);
        std::array<std::array<unsigned, 160>, 3> ram;
        unsigned offset = mode; // Also exercise unaligned starts after speed changes.
        for (unsigned panel = 0; panel < 3; ++panel)
            for (unsigned x = 0; x < 160; ++x)
                ram[panel][(offset + x) % 160] = (panel_origin(panel, gap) + offset + x) % image_width;
        for (unsigned frame = 0; frame < 2 * image_width; ++frame) {
            if (frame % 97 == 0) {
                gap = gap ? 0 : gap_width;
                // Repaint at the current ring position, as A does on the pedal.
                for (unsigned panel = 0; panel < 3; ++panel)
                    for (unsigned x = 0; x < 160; ++x)
                        ram[panel][(offset + x) % 160] = (panel_origin(panel, gap) + offset + x) % image_width;
            }
            for (unsigned step = 0; step < stride; ++step) {
                offset = (offset + 1) % image_width;
                for (unsigned panel = 0; panel < 3; ++panel) {
                    ram[panel][(offset + 159) % 160] = entering_world_x(panel, offset, gap);
                }
            }
            for (unsigned panel = 0; panel < 3; ++panel) {
                for (unsigned x = 0; x < 160; ++x)
                    assert(ram[panel][(x + offset) % 160] == (panel_origin(panel, gap) + x + offset) % image_width);
            }
            assert(start_line(offset) >= 1 && start_line(offset) <= 160);
        }
        stride = next_stride(stride);
    }
    assert(stride == 1);
}

int main() {
    using namespace scroll_probe;
    assert(start_line(0) == 1 && start_line(1) == 160 && start_line(160) == 1);
    for (unsigned slot = 0; slot < 3; ++slot) {
        assert(panel_index(slot, false) == slot);
        assert(panel_index(slot, true) == 2 - slot);
    }
    verify_windows(0);
    verify_windows(gap_width);
    uint8_t pixels[256];
    column(63, pixels);
    for (unsigned y = 0; y < height; ++y)
        assert((unsigned(pixels[2 * y]) << 8 | pixels[2 * y + 1]) == pixel(63, y));
    for (unsigned y = 0; y < height; ++y) assert(pixel(0, y) == 0xffff);
    for (unsigned x = 0; x < image_width; ++x) assert(pixel(x, 0) == 0xffff);
    assert(pixel(1, 1) == pixel(1 + image_width, 1));
}
