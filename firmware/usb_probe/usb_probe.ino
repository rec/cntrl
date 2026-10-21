#include <Arduino.h>

// No GPIO or storage initialization. USB is initialized by the Teensy core.
void setup() {}

void loop() {
    static uint32_t last_report = 0;
    const uint32_t now = millis();
    if (Serial && now - last_report >= 1000) {
        last_report = now;
        Serial.println("cntrl experimental firmware");
    }
}
