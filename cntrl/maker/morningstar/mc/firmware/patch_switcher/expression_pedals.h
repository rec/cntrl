#pragma once
#include <Arduino.h>
#include <cntrl/expression_pedals.h>
#include "filesystem.h"

// Electrical bias, pin mode and ADC access belong to this board.
class ExpressionPedals {
public:
    ExpressionPedals() : pedals_(4, calibrations_, filename, "C", 8, 2) {}
    cntrl::ExpressionPedals &controls() { return pedals_; }
    template<class TipPin, class Available, class Send>
    void poll(uint32_t now, TipPin tip_pin, Available available, Send send) {
        pedals_.poll(now, [&](unsigned i) { return tip_pin(i) >= 0; },
            [&](unsigned i, unsigned phase) {
                int pin = tip_pin(i);
                if (!phase) { pinMode(pin, INPUT_PULLDOWN); return 0; }
                int reading = analogRead(pin);
                pinMode(pin, phase == 1 ? INPUT_PULLUP : INPUT);
                return reading;
            }, available, send);
    }
private:
    static void filename(unsigned jack, char *path, unsigned capacity) {
        std::snprintf(path, capacity, "/cntrl/expression-%u.cal", jack + 1);
    }
    cntrl::device::SdFile calibrations_;
    cntrl::ExpressionPedals pedals_;
};
