#pragma once

#include <stdint.h>

// All display chip selects are HIGH on return.
void initialize_displays(bool greeting = true);
// One bounded RGB565 segment, using logical display coordinates.
void write_display_segment(unsigned panel, unsigned x, unsigned y, const uint8_t *pixels, unsigned count);
// Identical pixels broadcast to each selected panel (bits 0..2).
void write_display_group(uint8_t panels, unsigned x, unsigned y, const uint8_t *pixels, unsigned count);
