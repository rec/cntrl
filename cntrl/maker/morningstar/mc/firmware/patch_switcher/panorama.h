#pragma once
#include <cstdint>

constexpr unsigned display_gap = 72;
constexpr unsigned display_pitch = 160 + display_gap;
// The rotated controller has 162 RAM rows but only 160 visible columns.
constexpr unsigned display_ram_columns = 162;
constexpr unsigned mirrored_button(unsigned button) {
    return button / 3 * 3 + 2 - button % 3;
}
constexpr unsigned transition_progress(uint32_t elapsed_ms, unsigned duration_ms) {
    if (elapsed_ms >= duration_ms) return display_pitch;
    unsigned frames = (duration_ms * 60 + 999) / 1000;
    return (elapsed_ms * 60 / 1000) * display_pitch / frames;
}

void initialize_display_panorama();
void set_display_fast_refresh(bool enabled);
void write_display_column(unsigned panel, unsigned column, const uint8_t *pixels);
void scroll_display_panorama(unsigned offset);
void scroll_display_panel(unsigned panel, unsigned offset);
