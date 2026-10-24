#include <Arduino.h>
#include <ST7735_t3.h> // Link the installed classic font, not its display driver.
#include "buttons.h"
#include "displays.h"
#include "switches.h"
#include "io.h"
#include "pattern.h"
#include <cstring>

namespace {
ButtonState buttons[6];
bool painting = true, reverse_order = false;
unsigned pixels_per_update = 1, next_column = 0, offset = 0;
unsigned gap = scroll_probe::gap_width;
uint32_t last_scan = 0, last_step = 0, steps = 0, streamed = 0, step_us = 0;
uint32_t period_us = 0, max_us = 0, overruns = 0;
uint8_t image[scroll_probe::image_width][256];
char line[64];
unsigned used = 0;

void report() {
    char text[256];
    snprintf(text, sizeof(text), "CNTRL SCROLL %s order=%s spi=8 gap=%u rate=60 stride=%u x=%u ssa=%u steps=%lu pixels=%lu us=%lu period=%lu max_us=%lu overruns=%lu\n",
        painting ? "PAINTING" : "READY", reverse_order ? "RTL" : "LTR",
        gap, pixels_per_update, offset,
        scroll_probe::start_line(offset), (unsigned long)steps,
        (unsigned long)streamed, (unsigned long)step_us, (unsigned long)period_us,
        (unsigned long)max_us, (unsigned long)overruns);
    Serial.write(text);
}

void next_speed() {
    pixels_per_update = scroll_probe::next_stride(pixels_per_update);
    max_us = overruns = 0;
}
} // namespace

void setup() {
    analogWrite(6, 0);
    initialize_switches();
    initialize_displays(false);
    initialize_scroll();
    for (unsigned x = 0; x < scroll_probe::image_width; ++x)
        scroll_probe::column(x, image[x]);
}

void loop() {
    uint32_t now = millis();
    if (uint32_t(now - last_scan) >= 3) {
        last_scan = now;
        uint8_t raw = read_buttons();
        for (unsigned i = 0; i < 6; ++i) {
            uint8_t event = buttons[i].update(!(raw & (1u << button_bits[i])), now);
            if (i == 0 && (event & press_event)) {
                gap = gap ? 0 : scroll_probe::gap_width;
                analogWrite(6, 0);
                painting = true; next_column = 0;
                max_us = overruns = 0;
            }
            if (i == 1 && (event & press_event)) {
                reverse_order = !reverse_order;
                max_us = overruns = 0;
            }
            if (i == 2 && (event & press_event)) next_speed();
        }
    }
    for (unsigned i = 0; i < 64 && Serial.available(); ++i) {
        char c = char(Serial.read());
        if (c == '\r') continue;
        if (c == '\n') {
            line[used] = 0;
            if (!strcmp(line, "CNTRL SCROLL NEXT")) next_speed();
            report(); used = 0;
        } else if (used < sizeof(line) - 1) line[used++] = c;
    }
    if (painting) {
        unsigned panel = next_column / 160, x = next_column % 160;
        unsigned world_x = (scroll_probe::panel_origin(panel, gap) + offset + x) % scroll_probe::image_width;
        write_scroll_columns(panel, (offset + x) % 160, image[world_x], 1);
        if (++next_column == 480) {
            scroll_panels(7, offset, reverse_order);
            painting = false; last_step = micros(); analogWrite(6, 192);
        }
    } else if (uint32_t(micros() - last_step) >= 16667) {
        uint32_t start = micros();
        period_us = start - last_step;
        last_step = start;
        for (unsigned pixel = 0; pixel < pixels_per_update; ++pixel) {
            offset = (offset + 1) % scroll_probe::image_width;
            for (unsigned slot = 0; slot < 3; ++slot) {
                unsigned panel = scroll_probe::panel_index(slot, reverse_order);
                unsigned world_x = scroll_probe::entering_world_x(panel, offset, gap);
                write_scroll_columns(panel, (offset + 159) % 160, image[world_x], 1);
                streamed += 256;
            }
        }
        scroll_panels(7, offset, reverse_order);
        step_us = micros() - start;
        if (step_us > max_us) max_us = step_us;
        if (step_us > 16667) ++overruns;
        ++steps;
    }
}
