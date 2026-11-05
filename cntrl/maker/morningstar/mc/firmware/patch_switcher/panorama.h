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

struct alignas(32) DisplayBand {
    uint8_t pixels[8 * 128 * 2];
    unsigned panel, column, columns, first, count;
};
DisplayBand *acquire_display_band();
void queue_display_band(DisplayBand &band);
void poll_display_dma();
void start_display_dma();
bool display_dma_pending();
bool display_spi_busy();
bool take_display_dma_error();

void initialize_display_panorama();
void set_display_fast_refresh(bool enabled);
void write_display_column_segment(unsigned panel, unsigned column, unsigned first, const uint8_t *pixels, unsigned count);
void write_display_column(unsigned panel, unsigned column, const uint8_t *pixels);
void scroll_display_panorama(unsigned offset);
void scroll_display_panel(unsigned panel, unsigned offset);
