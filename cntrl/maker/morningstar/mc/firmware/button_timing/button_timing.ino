#include <Arduino.h>
#include "buttons.h"
#include "switches.h"
#include "pair_timing.h"

ButtonState a_button, f_button;
PairTiming timing;
uint32_t last_scan = 0;
unsigned trials = 0;
bool connected = false;

void setup() {
    initialize_switches();
    // Displays remain deselected and dark; this diagnostic only reads switches.
    analogWrite(6, 0);
}

void loop() {
    bool current = bool(Serial);
    if (current && !connected) {
        Serial.println("CNTRL A+F timing: press both, then release both; no hold required.");
        Serial.println("1 ms scans, 15 ms debounce; gaps measure recognized presses.");
    }
    connected = current;
    uint32_t now = millis();
    if (uint32_t(now - last_scan) < 1) return;
    last_scan = now;
    uint8_t raw = read_buttons();
    a_button.update(!(raw & (1u << button_bits[0])), now);
    f_button.update(!(raw & (1u << button_bits[5])), now);
    PairSample sample = timing.update(a_button.pressed, f_button.pressed, now);
    if (sample.ready && current) {
        Serial.printf("Trial %u: first=%c gap_ms=%lu\n", ++trials,
            sample.first, (unsigned long)sample.gap_ms);
    }
}
