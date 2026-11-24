#pragma once
#include "binary_format.h"

namespace cntrl {
struct MemonSettings {
    static constexpr unsigned record_size = 47;
    enum Flag : unsigned { countdown = 1, repeat = 2, timed = 4, cooperative = 8 };

    uint8_t route = 0, channel = 1, velocity = 100, flags = countdown | timed;
    uint8_t chord_size = 4, font = 16, color_offset = 64;
    uint16_t note_ms = 250, gap_ms = 250, error_ms = 2000, timeout_ms = 5000;
    uint8_t notes[4] = {60, 64, 67, 70}, chord[4] = {60, 64, 67, 70};
    uint8_t colors[6][3] = {{136,77,255}, {255,122,26}, {0,184,160}, {255,241,214},
                            {255,255,255}, {0,0,0}};

#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(memon_settings_valid)
#endif
    static bool valid(const uint8_t *p) {
        if (p[4] != 1 || p[5] >= 16 || !p[6] || p[6] > 16 || !p[7] || p[7] > 127
            || p[8] > 31 || !p[9] || p[9] > 4 || p[10] > 127
            || !binary::read16(p + 13) || !binary::read16(p + 17)) return false;
        for (unsigned i = 0; i < 4; ++i) {
            if (p[21 + i] > 127 || p[25 + i] > 127 || (i >= p[9] && p[25 + i])) return false;
            if (i < p[9]) for (unsigned j = 0; j < i; ++j)
                if (p[25 + i] == p[25 + j]) return false;
        }
        return true;
    }

    static MemonSettings read(const uint8_t *p) {
        MemonSettings value;
        value.route = p[5]; value.channel = p[6]; value.velocity = p[7]; value.flags = p[8];
        value.chord_size = p[9]; value.font = p[11]; value.color_offset = p[12];
        value.note_ms = binary::read16(p + 13); value.gap_ms = binary::read16(p + 15);
        value.error_ms = binary::read16(p + 17); value.timeout_ms = binary::read16(p + 19);
        std::memcpy(value.notes, p + 21, 4); std::memcpy(value.chord, p + 25, 4);
        std::memcpy(value.colors, p + 29, sizeof(value.colors));
        return value;
    }
};
}
