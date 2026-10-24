// Reuse the tested initialization, pin handoff and SPI transaction helpers.
#include <displays.cpp>
#include "io.h"
#include "pattern.h"

void initialize_scroll() {
    const Panel panels[] = {{15, 34, 21}, {24, 23, 22}, {27, 25, 20}};
    const uint8_t native[] = {0x88}; // Preserve MY/BGR, disable row/column exchange.
    const uint8_t area[] = {0, 1, 0, 160, 0, 1}; // Account for both hidden gate lines.
    const uint8_t start[] = {0, 1};
    begin_transfer();
    for (const Panel &panel : panels) {
        command(panel, 0x36, native, sizeof(native));
        command(panel, 0x33, area, sizeof(area));
        command(panel, 0x37, start, sizeof(start));
    }
    end_transfer();
}

void scroll_panels(uint8_t selected, unsigned offset) {
    if (!selected || selected > 7) return;
    const Panel panels[] = {{15, 34, 21}, {24, 23, 22}, {27, 25, 20}};
    const uint8_t start[] = {0, uint8_t(scroll_probe::start_line(offset))};
    begin_transfer();
    for (unsigned i = 0; i < 3; ++i)
        if (selected & (1u << i)) command(panels[i], 0x37, start, sizeof(start));
    end_transfer();
}

void write_scroll_columns(unsigned index, unsigned column, const uint8_t *pixels, unsigned count) {
    if (index >= 3 || column >= 160 || !count || count > 160 - column) return;
    const Panel panels[] = {{15, 34, 21}, {24, 23, 22}, {27, 25, 20}};
    const Panel &panel = panels[index];
    const uint8_t columns[] = {0, 2, 0, 129};
    for (unsigned x = 0; x < count; ++x) {
        const uint8_t rows[] = {0, uint8_t(column + x + 1), 0, uint8_t(column + x + 1)};
        begin_transfer();
        command(panel, 0x2a, columns, sizeof(columns));
        command(panel, 0x2b, rows, sizeof(rows));
        digitalWrite(panel.dc, LOW); digitalWrite(panel.cs, LOW);
        send_byte(0x2c); digitalWrite(panel.dc, HIGH);
        for (unsigned i = 0; i < 256; ++i) send_byte(pixels[x * 256 + i]);
        digitalWrite(panel.cs, HIGH);
        end_transfer();
    }
}
