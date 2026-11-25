#pragma once
#include <Arduino.h>
#include <cntrl/expression_pedals.h>
#include "filesystem.h"
#include "target_capabilities.h"

// Electrical bias, pin mode and ADC access belong to this board.
class ExpressionPedals {
public:
    ExpressionPedals() : pedals_(cntrl::target::port_count, filesystem_, 8, 2) {}
    cntrl::ExpressionPedals &controls() { return pedals_; }
    template<class TipPin, class Available, class Send>
    void poll(uint32_t now, TipPin tip_pin, Available available, Send send) {
        if (!adc_configured_) {
            analogReadResolution(12);
            adc_configured_ = true;
        }
        pedals_.poll(now, [&](unsigned i) { return tip_pin(i) >= 0; },
            [&](unsigned i, unsigned phase) {
                const cntrl::device::CpuWorkScope work(cntrl::device::cpu_load, cntrl::CpuWork::input);
                int pin = tip_pin(i);
                if (!phase) { pinMode(pin, INPUT_PULLDOWN); return 0; }
                int reading = analogRead(pin);
                pinMode(pin, phase == 1 ? INPUT_PULLUP : INPUT);
                return reading;
            }, available, send);
    }
private:
    cntrl::device::SdFileSystem filesystem_;
    cntrl::ExpressionPedals pedals_;
    bool adc_configured_ = false;
};
