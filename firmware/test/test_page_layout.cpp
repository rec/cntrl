#include <cntrl/page_layout.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>

int main() {
    for (unsigned windows : {1u, 2u, 5u, 17u, 255u}) {
        std::vector<uint8_t> bytes(8 + windows * 32 + 2 * (34 + windows));
        std::memcpy(bytes.data(), "CPAG", 4); bytes[4] = 2; bytes[5] = uint8_t(windows); bytes[6] = 1; bytes[7] = 0;
        for (unsigned i = 0; i < windows; ++i) std::snprintf(reinterpret_cast<char *>(bytes.data() + 8 + i * 32), 32, "viewport-%u", i);
        uint8_t *app = bytes.data() + 8 + windows * 32;
        std::strcpy(reinterpret_cast<char *>(app), "Main");
        for (unsigned i = 0; i < windows; ++i) app[34 + i] = uint8_t(windows - i - 1);
        app[32] = 11; app[33] = uint8_t(windows);
        uint8_t *pages = app + 34 + windows; pages[32] = 1; pages[33] = uint8_t(windows);
        cntrl::PageLayout layout{bytes.data()};
        assert(layout.windows() == windows && layout.controllers() == 1 && layout.pages() == 2 && layout.length() == bytes.size());
        assert(std::strcmp(layout.name(0), "Main") == 0 && layout.page(0)[1] == windows);
        assert(std::strcmp(layout.window_name(0), "viewport-0") == 0);
        for (unsigned occupied = 0; occupied <= windows; ++occupied) for (unsigned logical = 0; logical < windows; ++logical)
            assert(layout.controller_window(0, logical, occupied) == (logical < windows - occupied ? int(logical) : -1));
        assert(layout.controller_window(1, 0, 0) == -1); assert(layout.controller_window(0, windows, 0) == -1);
    }
}
