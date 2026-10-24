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
bool panorama = false, painting = true;
unsigned next_column = 0, offset = 0;
uint32_t last_scan = 0, last_step = 0, steps = 0, streamed = 0, step_us = 0;
char line[64];
unsigned used = 0;

void report() {
    char text[128];
    snprintf(text, sizeof(text), "CNTRL SCROLL %s stage=%s x=%u ssa=%u steps=%lu pixels=%lu us=%lu\n",
        painting ? "PAINTING" : "READY", panorama ? "PAN" : "ONE", offset,
        scroll_probe::start_line(offset), (unsigned long)steps,
        (unsigned long)streamed, (unsigned long)step_us);
    Serial.write(text);
}

void next_stage() {
    panorama = !panorama;
    analogWrite(6, 0);
    initialize_scroll();
    painting = true; next_column = offset = 0;
    steps = streamed = step_us = 0;
}
} // namespace

void setup() {
    analogWrite(6, 0);
    initialize_switches();
    initialize_displays(false);
    initialize_scroll();
}

void loop() {
    uint32_t now = millis();
    if (uint32_t(now - last_scan) >= 3) {
        last_scan = now;
        uint8_t raw = read_buttons();
        for (unsigned i = 0; i < 6; ++i) {
            uint8_t event = buttons[i].update(!(raw & (1u << button_bits[i])), now);
            if (i == 2 && (event & press_event)) next_stage();
        }
    }
    for (unsigned i = 0; i < 64 && Serial.available(); ++i) {
        char c = char(Serial.read());
        if (c == '\r') continue;
        if (c == '\n') {
            line[used] = 0;
            if (!strcmp(line, "CNTRL SCROLL NEXT")) next_stage();
            report(); used = 0;
        } else if (used < sizeof(line) - 1) line[used++] = c;
    }
    uint8_t pixels[256];
    if (painting) {
        unsigned panel = next_column / 160, x = next_column % 160;
        scroll_probe::column(next_column, pixels);
        write_scroll_column(panel, x, pixels);
        if (++next_column == 480) {
            painting = false; last_step = millis(); analogWrite(6, 192);
        }
    } else if (uint32_t(now - last_step) >= 33) {
        last_step = now;
        uint32_t start = micros();
        offset = (offset + 1) % scroll_probe::image_width;
        if (panorama) {
            for (unsigned panel = 0; panel < 3; ++panel) {
                scroll_probe::column(scroll_probe::entering_world_x(panel, offset), pixels);
                write_scroll_column(panel, scroll_probe::entering_column(offset), pixels);
                streamed += sizeof(pixels);
            }
        }
        scroll_panels(panorama ? 7 : 1, offset);
        step_us = micros() - start;
        ++steps;
    }
}
