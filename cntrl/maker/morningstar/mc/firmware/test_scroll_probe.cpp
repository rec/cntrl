#include "scroll_probe/pattern.h"
#include <array>
#include <cassert>

int main() {
    using namespace scroll_probe;
    assert(start_line(0) == 1 && start_line(1) == 160 && start_line(160) == 1);
    for (unsigned stride = 1; stride <= 2; ++stride) {
        std::array<std::array<unsigned, 160>, 3> ram;
        for (unsigned panel = 0; panel < 3; ++panel)
            for (unsigned x = 0; x < 160; ++x) ram[panel][x] = panel * 160 + x;
        unsigned offset = 0;
        for (unsigned frame = 0; frame < 2 * image_width; ++frame) {
            for (unsigned step = 0; step < stride; ++step) {
                offset = (offset + 1) % image_width;
                for (unsigned panel = 0; panel < 3; ++panel)
                    ram[panel][entering_column(offset)] = entering_world_x(panel, offset);
            }
            for (unsigned panel = 0; panel < 3; ++panel) {
                for (unsigned x = 0; x < 160; ++x)
                    assert(ram[panel][(x + offset) % 160] == (panel * 160 + x + offset) % image_width);
            }
            assert(start_line(offset) >= 1 && start_line(offset) <= 160);
        }
    }
    uint8_t pixels[256];
    column(63, pixels);
    for (unsigned y = 0; y < height; ++y)
        assert((unsigned(pixels[2 * y]) << 8 | pixels[2 * y + 1]) == pixel(63, y));
    for (unsigned y = 0; y < height; ++y) assert(pixel(0, y) == 0xffff);
    for (unsigned x = 0; x < image_width; ++x) assert(pixel(x, 0) == 0xffff);
    assert(pixel(1, 1) == pixel(1 + image_width, 1));
}
