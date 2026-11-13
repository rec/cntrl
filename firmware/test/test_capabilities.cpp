#include "target_capabilities.h"
#include <cassert>
#include <cstdlib>
#include <cstring>

int main(int argc, char **argv) {
    assert(argc == 6);
    static_assert(cntrl::usb_block_size == 256);
    assert(cntrl::target::memory_capacity == std::strtoul(argv[1], nullptr, 10));
    assert(cntrl::target::viewports[0].width == std::strtoul(argv[2], nullptr, 10));
    assert(cntrl::target::display_count == std::strtoul(argv[3], nullptr, 10));
    assert(cntrl::target::button_count == std::strtoul(argv[4], nullptr, 10));
    assert(cntrl::target::controls.previous_page == std::strtoul(argv[5], nullptr, 10));
    unsigned area = 0;
    for (unsigned i = 0; i < cntrl::target::display_count; ++i) {
        assert(std::strlen(cntrl::target::display_ids[i]) > 0);
        area += cntrl::target::viewports[i].width * cntrl::target::viewports[i].height;
    }
    assert(area == cntrl::target::total_display_area);
    for (const auto &endpoint : cntrl::target::midi_connections) {
        assert(cntrl::target::midi_connections.find(endpoint.name) == &endpoint);
        assert(cntrl::target::midi_connections.route(endpoint.route) == &endpoint);
    }
}
