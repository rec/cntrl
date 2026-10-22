#include <Arduino.h>
#include "buttons.h"
#include "displays.h"
#include "switches.h"

void setup() {
    initialize_switches();
    initialize_displays();
}

void loop() {
    static ButtonState buttons[6];
    static uint32_t last_scan = 0;
    static uint32_t last_report = 0;
    static uint32_t scans = 0;
    static uint8_t raw = 0xff;
    const uint32_t now = millis();
    if (uint32_t(now - last_scan) >= 3) {
        last_scan = now;
        raw = read_buttons();
        ++scans;
        for (unsigned i = 0; i < 6; ++i) {
            const uint8_t events = buttons[i].update(!(raw & (1u << button_bits[i])), now);
            if (Serial) {
                const char *names[] = {"pressed", "released", "held", "double pressed"};
                for (unsigned j = 0; j < 4; ++j) {
                    if (events & (1u << j)) {
                        Serial.print("button ");
                        Serial.print(char('A' + i));
                        Serial.print(' ');
                        Serial.print(names[j]);
                        Serial.print(" ms=");
                        Serial.println(now);
                    }
                }
            }
        }
    }
    if (Serial && uint32_t(now - last_report) >= 100) {
        last_report = now;
        Serial.print("cntrl raw ms=");
        Serial.print(now);
        Serial.print(" scans=");
        Serial.print(scans);
        Serial.print(" bits[7:0]=");
        for (int bit = 7; bit >= 0; --bit) {
            Serial.print((raw >> bit) & 1);
        }
        Serial.println();
    }
}
