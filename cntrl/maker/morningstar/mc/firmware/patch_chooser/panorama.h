#pragma once
#include <cstdint>
#include "target_capabilities.h"

constexpr unsigned display_gap = cntrl::target::viewports[0].gap;
constexpr unsigned display_pitch = cntrl::target::viewports[0].width + display_gap;
// The rotated controller has 162 RAM rows but only 160 visible columns.
constexpr unsigned display_ram_columns = 162;
struct alignas(32) DisplayBand {
    uint8_t pixels[8 * 128 * 2];
    unsigned panel, column, columns, first, count;
    uint8_t padding[12]; // Keep each DMA band on a complete 32-byte cache line.
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
