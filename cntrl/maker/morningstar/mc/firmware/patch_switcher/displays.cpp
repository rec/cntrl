// Keep splash row writes and native UI column writes on the same pin handoff.
#define write_display_group write_landscape_group
#define write_display_segment write_landscape_segment
#include <displays.cpp>
#undef write_display_group
#undef write_display_segment
#include "panorama.h"

namespace {
bool native_mode = false;
const Panel panorama_panels[] = {{15, 34, 21}, {24, 23, 22}, {27, 25, 20}};
}

void initialize_display_panorama() {
    const uint8_t mode[] = {0x88}, area[] = {0, 0, 0, display_ram_columns, 0, 0}, start[] = {0, 0};
    begin_transfer();
    for (const auto &panel : panorama_panels) {
        command(panel, 0x36, mode, 1);
        command(panel, 0x33, area, 6);
        command(panel, 0x37, start, 2);
    }
    end_transfer(); native_mode = true;
}

void write_display_column(unsigned index, unsigned column, const uint8_t *pixels) {
    if (index >= 3 || column >= display_ram_columns) return;
    const auto &panel = panorama_panels[index];
    const uint8_t columns[] = {0, 2, 0, 129};
    const uint8_t row[] = {0, uint8_t((column + 1) % display_ram_columns), 0, uint8_t((column + 1) % display_ram_columns)};
    begin_transfer();
    command(panel, 0x2a, columns, 4); command(panel, 0x2b, row, 4);
    digitalWrite(panel.dc, LOW); digitalWrite(panel.cs, LOW);
    send_byte(0x2c); digitalWrite(panel.dc, HIGH);
    for (unsigned i = 0; i < 256; ++i) send_byte(pixels[i]);
    digitalWrite(panel.cs, HIGH); end_transfer();
}

void scroll_display_panorama(unsigned offset) {
    const uint8_t start[] = {0, uint8_t((display_ram_columns - offset % display_ram_columns) % display_ram_columns)};
    begin_transfer();
    for (const auto &panel : panorama_panels) command(panel, 0x37, start, 2);
    end_transfer();
}

void timing_scroll_panel(unsigned index, unsigned offset) {
    if (index >= 3) return;
    const uint8_t start[] = {0, uint8_t((162 - offset % 162) % 162)};
    begin_transfer();
    command(panorama_panels[index], 0x37, start, 2);
    end_transfer();
}

void timing_backlight(unsigned value) { analogWrite(6, value); }

void write_display_group(uint8_t selected, unsigned x, unsigned y, const uint8_t *pixels, unsigned count) {
    if (native_mode) {
        const uint8_t mode[] = {0xa8}, area[] = {0, 1, 0, 160, 0, 1}, start[] = {0, 1};
        begin_transfer();
        for (const auto &panel : panorama_panels) {
            command(panel, 0x33, area, 6);
            command(panel, 0x37, start, 2); command(panel, 0x36, mode, 1);
        }
        end_transfer(); native_mode = false;
    }
    write_landscape_group(selected, x, y, pixels, count);
}

void write_display_segment(unsigned panel, unsigned x, unsigned y, const uint8_t *pixels, unsigned count) {
    if (panel < 3) write_display_group(1u << panel, x, y, pixels, count);
}
