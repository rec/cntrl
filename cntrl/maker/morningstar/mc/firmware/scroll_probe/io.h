#pragma once
#include <cstdint>

void initialize_scroll();
void scroll_panels(uint8_t selected, unsigned offset);
void write_scroll_column(unsigned panel, unsigned column, const uint8_t *pixels);
