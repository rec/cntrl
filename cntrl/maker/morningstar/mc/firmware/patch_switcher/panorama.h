#pragma once
#include <cstdint>

constexpr unsigned display_gap = 72;
constexpr unsigned display_pitch = 160 + display_gap;
// The rotated controller has 162 RAM rows but only 160 visible columns.
constexpr unsigned display_ram_columns = 162;
constexpr unsigned transition_progress(uint32_t elapsed_ms, unsigned duration_ms) {
    if (elapsed_ms >= duration_ms) return display_pitch;
    unsigned frames = (duration_ms * 30 + 999) / 1000;
    return (elapsed_ms * 30 / 1000) * display_pitch / frames;
}

void initialize_display_panorama();
void write_display_column(unsigned panel, unsigned column, const uint8_t *pixels,
    unsigned first = 0, unsigned count = 128);

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
