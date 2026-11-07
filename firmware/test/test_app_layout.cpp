#include <cntrl/app_layout.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>

int main() {
    for (unsigned windows : {1u, 2u, 5u, 17u, 255u}) {
        std::vector<uint8_t> bytes(8 + windows * 32 + (32 + windows) + 4);
        std::memcpy(bytes.data(), "CLAY", 4); bytes[4] = 1; bytes[5] = 2; bytes[6] = windows; bytes[7] = 2;
        for (unsigned i = 0; i < windows; ++i) std::snprintf(reinterpret_cast<char *>(bytes.data() + 8 + i * 32), 32, "viewport-%u", i);
        uint8_t *app = bytes.data() + 8 + windows * 32;
        std::strcpy(reinterpret_cast<char *>(app), "Main");
        for (unsigned i = 0; i < windows; ++i) app[32 + i] = windows - i - 1;
        uint8_t *pages = app + 32 + windows; pages[0] = 1; pages[1] = windows;
        cntrl::AppLayout layout{bytes.data()};
        assert(layout.windows() == windows && layout.apps() == 1 && layout.pages() == 2 && layout.length() == bytes.size());
        assert(std::strcmp(layout.app_name(0), "Main") == 0 && layout.page(0)[1] == windows);
        assert(std::strcmp(layout.window_name(0), "viewport-0") == 0);
        for (unsigned occupied = 0; occupied <= windows; ++occupied) for (unsigned logical = 0; logical < windows; ++logical)
            assert(layout.app_window(0, logical, occupied) == (logical < windows - occupied ? int(logical) : -1));
        assert(layout.app_window(1, 0, 0) == -1); assert(layout.app_window(0, windows, 0) == -1);
    }
}
