#pragma once
#include <Arduino.h>

inline void initialize_switches() {
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
}

inline uint8_t read_buttons() {
    // Stock reader: gate high, load low/high, gate low, read then clock.
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
        delayMicroseconds(3);
    }
    return value;
}
