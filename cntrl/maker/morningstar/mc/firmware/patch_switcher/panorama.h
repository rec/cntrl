#pragma once
#include <cstdint>

constexpr unsigned display_gap = 72;
constexpr unsigned display_pitch = 160 + display_gap;
constexpr unsigned transition_frames = 24;

void initialize_display_panorama();
void write_display_column(unsigned panel, unsigned column, const uint8_t *pixels);
void scroll_display_panorama(unsigned offset);

struct PanoramaSample {
    int pane;
    unsigned x;
};

// Pane -1 is the incoming undo pane; pane 3 is the incoming send pane.
inline PanoramaSample panorama_sample(unsigned panel, unsigned x, unsigned progress, bool undo) {
    int world = int(panel * display_pitch + x) + (undo ? -int(progress) : int(progress));
    int pane = world >= 0 ? world / int(display_pitch) : (world - int(display_pitch) + 1) / int(display_pitch);
    return {pane, unsigned(world - pane * int(display_pitch))};
}
