#pragma once
#include "message_format.h"
#include <array>
#include <cstring>

namespace cntrl::ui::flags {
inline constexpr unsigned width = 18, height = 12;
inline constexpr unsigned marker_before = 2, marker_after = 4;
inline constexpr unsigned marker_column = marker_before + height + marker_after, label_gap = 6;
enum Country { uk, france, germany, spain, italy, portugal, netherlands, poland,
    czechia, slovakia, hungary, romania, sweden, denmark, norway, finland, turkey, count };

// Small raster approximations, including the shields where they distinguish flags.
constexpr uint16_t color(unsigned country, int x, int y) {
    constexpr uint16_t white = 0xffff, red = 0xf800, blue = 0x0018,
        green = 0x0400, yellow = 0xffe0;
    switch (country) {
    case uk: {
        const int a = x * 12 - y * 18, b = x * 12 + y * 18 - 198;
        const bool diagonal = (a >= -24 && a <= 24) || (b >= -24 && b <= 24);
        const bool stripe = (a >= -8 && a <= 8) || (b >= -8 && b <= 8);
        if ((x >= 8 && x <= 9) || (y >= 5 && y <= 6)) return red;
        if ((x >= 6 && x <= 11) || (y >= 4 && y <= 7)) return white;
        return stripe ? red : diagonal ? white : blue;
    }
    case france: return x < 6 ? blue : x < 12 ? white : red;
    case germany: return y < 4 ? 0 : y < 8 ? red : yellow;
    case spain:
        if (x >= 5 && x <= 6 && y >= 5 && y <= 7) return red;
        return y < 3 || y >= 9 ? red : yellow;
    case italy: return x < 6 ? green : x < 12 ? white : red;
    case portugal: {
        const int dx = x - 7, dy = y - 6;
        if (x >= 6 && x <= 8 && y >= 4 && y <= 7) return x == 7 && y == 5 ? blue : white;
        if (dx * dx + dy * dy <= 9) return yellow;
        return x < 7 ? green : red;
    }
    case netherlands: return y < 4 ? red : y < 8 ? white : blue;
    case poland: return y < 6 ? white : red;
    case czechia: return x <= (y < 6 ? y : 11 - y) * 2 ? blue : y < 6 ? white : red;
    case slovakia:
        if (x >= 3 && x <= 7 && y >= 3 && y <= 8) {
            if (x == 3 || x == 7 || y == 8 || y == 3) return white;
            return x == 5 || y == 5 ? white : red;
        }
        return y < 4 ? white : y < 8 ? blue : red;
    case hungary: return y < 4 ? red : y < 8 ? white : green;
    case romania: return x < 6 ? blue : x < 12 ? yellow : red;
    case sweden: return x == 5 || x == 6 || y == 5 || y == 6 ? yellow : blue;
    case denmark: return x == 5 || x == 6 || y == 5 || y == 6 ? white : red;
    case norway:
        if (x == 5 || x == 6 || y == 5 || y == 6) return blue;
        return (x >= 4 && x <= 7) || (y >= 4 && y <= 7) ? white : red;
    case finland: return x == 5 || x == 6 || y == 5 || y == 6 ? blue : white;
    case turkey: {
        const int dx = x - 7, dy = y - 6, cut_x = x - 9;
        if (dx * dx + dy * dy <= 16 && cut_x * cut_x + dy * dy > 10) return white;
        return (x == 13 && y >= 4 && y <= 7) || (y == 5 && x >= 12 && x <= 15) ? white : red;
    }
    default: return white;
    }
}

constexpr auto prepare() {
    std::array<std::array<uint16_t, width * height>, count> result{};
    for (unsigned c = 0; c < count; ++c)
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) result[c][y * width + x] = color(c, int(x), int(y));
    return result;
}

inline constexpr auto bitmaps CNTRL_I18N_STORAGE = prepare();
inline constexpr char locales[][6] CNTRL_I18N_STORAGE = {
    "en", "fr", "de", "es", "it", "pt-PT", "nl", "pl", "cs", "sk", "hu", "ro", "sv", "da", "nb", "fi", "tr",
};

#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(flag_identifier)
#endif
inline uint8_t identifier(const char *locale) {
    for (unsigned i = 0; i < count; ++i) if (!std::strcmp(locale, locales[i])) return uint8_t(i + 1);
    return 0;
}
} // namespace cntrl::ui::flags
