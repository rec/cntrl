// Keep splash row writes and native UI column writes on the same pin handoff.
#define write_display_group write_landscape_group
#define write_display_segment write_landscape_segment
#include <displays.cpp>
#undef write_display_group
#undef write_display_segment
#include "panorama.h"
#include <EventResponder.h>

namespace {
bool native_mode = false;
DMAMEM DisplayBand bands[2];
unsigned active_band = 2, queued_band = 2;
bool dma_error = false;
EventResponder dma_complete;
const Panel panorama_panels[] = {{15, 34, 21}, {24, 23, 22}, {27, 25, 20}};
}

DisplayBand *acquire_display_band() {
    if (queued_band < 2) return nullptr;
    for (unsigned i = 0; i < 2; ++i) if (i != active_band) return &bands[i];
    return nullptr;
}
void queue_display_band(DisplayBand &band) { queued_band = unsigned(&band - bands); }
bool display_spi_busy() { return active_band < 2; }
bool display_dma_pending() { return display_spi_busy() || queued_band < 2; }
bool take_display_dma_error() { bool result = dma_error; dma_error = false; return result; }
void poll_display_dma() {
    if (!display_spi_busy() || !dma_complete) return;
    dma_complete.clearEvent();
    digitalWrite(panorama_panels[bands[active_band].panel].cs, HIGH);
    end_transfer();
    active_band = 2;
}
void start_display_dma() {
    if (display_spi_busy() || queued_band >= 2) return;
    active_band = queued_band; queued_band = 2;
    const auto &band = bands[active_band];
    const auto &panel = panorama_panels[band.panel];
    const unsigned row = (band.column + 1) % display_ram_columns;
    const uint8_t columns[] = {0, uint8_t(2 + band.first), 0, uint8_t(1 + band.first + band.count)};
    const uint8_t rows[] = {0, uint8_t(row), 0, uint8_t(row + band.columns - 1)};
    begin_transfer();
    command(panel, 0x2a, columns, 4); command(panel, 0x2b, rows, 4);
    digitalWrite(panel.dc, LOW); digitalWrite(panel.cs, LOW);
    send_byte(0x2c); digitalWrite(panel.dc, HIGH);
    // The library flushes the DMA buffer's cache and signals after the last RX
    // byte. Keep CS and the SPI pin mux until that completion is observed.
    if (!SPI.transfer(band.pixels, nullptr, band.columns * band.count * 2, dma_complete)) {
        digitalWrite(panel.cs, HIGH); end_transfer();
        active_band = queued_band = 2; dma_error = true;
    }
}

void initialize_display_panorama() {
    const uint8_t mode[] = {0x80}, area[] = {0, 0, 0, display_ram_columns, 0, 0}, start[] = {0, 0};
    begin_transfer();
    for (const auto &panel : panorama_panels) {
        command(panel, 0x36, mode, 1);
        command(panel, 0x33, area, 6);
        command(panel, 0x37, start, 2);
    }
    end_transfer(); native_mode = true;
    set_display_fast_refresh(true);
}

void set_display_fast_refresh(bool enabled) {
    const uint8_t fast[] = {0, 1, 1};
    const uint8_t stock[] = {5, 60, 60};
    begin_transfer();
    for (const auto &panel : panorama_panels) {
        command(panel, 0xb1, enabled ? fast : stock, sizeof(fast));
    }
    end_transfer();
}

void write_display_column(unsigned index, unsigned column, const uint8_t *pixels) {
    write_display_column_segment(index, column, 0, pixels, 128);
}

void write_display_column_segment(unsigned index, unsigned column, unsigned first, const uint8_t *pixels, unsigned count) {
    if (index >= 3 || column >= display_ram_columns || !count || first + count > 128) return;
    const auto &panel = panorama_panels[index];
    const uint8_t columns[] = {0, uint8_t(2 + first), 0, uint8_t(1 + first + count)};
    const uint8_t row[] = {0, uint8_t((column + 1) % display_ram_columns), 0, uint8_t((column + 1) % display_ram_columns)};
    begin_transfer();
    command(panel, 0x2a, columns, 4); command(panel, 0x2b, row, 4);
    digitalWrite(panel.dc, LOW); digitalWrite(panel.cs, LOW);
    send_byte(0x2c); digitalWrite(panel.dc, HIGH);
    SPI.transfer(pixels, nullptr, count * 2);
    digitalWrite(panel.cs, HIGH); end_transfer();
}

void scroll_display_panorama(unsigned offset) {
    const uint8_t start[] = {0, uint8_t((display_ram_columns - offset % display_ram_columns) % display_ram_columns)};
    begin_transfer();
    for (const auto &panel : panorama_panels) command(panel, 0x37, start, 2);
    end_transfer();
}

void scroll_display_panel(unsigned index, unsigned offset) {
    const uint8_t start[] = {0, uint8_t((display_ram_columns - offset % display_ram_columns) % display_ram_columns)};
    begin_transfer();
    command(panorama_panels[index], 0x37, start, 2);
    end_transfer();
}

void write_display_group(uint8_t selected, unsigned x, unsigned y, const uint8_t *pixels, unsigned count) {
    if (native_mode) {
        set_display_fast_refresh(false);
        const uint8_t mode[] = {0xa0}, area[] = {0, 1, 0, 160, 0, 1}, start[] = {0, 1};
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
