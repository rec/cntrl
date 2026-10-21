#include <Arduino.h>
#include "buttons.h"
#include "displays.h"

void setup() {
    // Preload output latches before enabling drivers. Unlike digitalWrite,
    // constant-pin digitalWriteFast sets the latch even in input mode.
    // Keep displays deselected before driving their shared clock on pin 13.
    digitalWriteFast(15, HIGH);
    digitalWriteFast(24, HIGH);
    digitalWriteFast(27, HIGH);
    pinMode(15, OUTPUT);
    pinMode(24, OUTPUT);
    pinMode(27, OUTPUT);
    digitalWriteFast(28, HIGH);
    digitalWriteFast(29, HIGH);
    digitalWriteFast(13, LOW);
    pinMode(28, OUTPUT);
    pinMode(29, OUTPUT);
    pinMode(13, OUTPUT);
    pinMode(12, INPUT);
    initialize_displays();
}

uint8_t read_buttons() {
    // Stock reader 0x2648: gate high, load low/high, gate low, read then clock.
    digitalWriteFast(29, HIGH);
    digitalWriteFast(28, LOW);
    delayMicroseconds(3);
    digitalWriteFast(28, HIGH);
    delayMicroseconds(3);
    digitalWriteFast(29, LOW);
    uint8_t value = 0;
    for (int bit = 7; bit >= 0; --bit) {
        value |= digitalReadFast(12) << bit;
        digitalWriteFast(13, HIGH);
        delayMicroseconds(3);
        digitalWriteFast(13, LOW);
        // Explicit low time also accommodates the faster inlined GPIO writes.
        delayMicroseconds(3);
    }
    return value;
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
