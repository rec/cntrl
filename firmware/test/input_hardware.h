#pragma once
#include <cntrl/hardware.h>

// Explicit host fixture identifiers; production adapters declare their own.
struct TestInputHardware : cntrl::Hardware {
    unsigned input_count() const override { return 6; }
    const char *input_label(unsigned index) const override {
        static constexpr const char *labels[] = {"A", "B", "C", "D", "E", "F"};
        return index < 6 ? labels[index] : "";
    }
    unsigned control_input(cntrl::ControlRole role) const override { return unsigned(role); }
};
