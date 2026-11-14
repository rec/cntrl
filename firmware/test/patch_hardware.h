#include "input_hardware.h"
#pragma once
#include <cntrl/controller.h>
#include "midi_endpoints.h"

// Host-only sink: a successful fake write is delivered immediately. Production
// targets supply the MIDI scheduler and pin real external documents instead.
struct TestPatchHardware : TestInputHardware, cntrl::PatchOutputs, cntrl::PatchSources {
    TestPatchHardware() { if (!cntrl::memory_budget) cntrl::memory_budget = 192 * 1024; }
    unsigned display_count() const override { return 3; }
    const char *control_label(cntrl::ControlRole role) const override {
        const char *labels[] = {"A", "B", "C", "D", "E", "F"}; return labels[unsigned(role)];
    }
    cntrl::midi::Outputs midi_outputs() const override { return test_midi_outputs; }
    unsigned memory_capacity() const override { return 645776; }
    unsigned memory_runtime() const override { return 240000; }
    cntrl::Controller *controller = nullptr;
    uint32_t names_signature() const override { return 0; }
    bool available(const char *output) const override { return supports(output); }
    bool send_patch(unsigned, const char *output, const uint8_t *bytes, unsigned length) override {
        return send(output, bytes, length);
    }
    cntrl::midi::Bridge::Delivery patch_delivery(unsigned) const override { return cntrl::midi::Bridge::Delivery::complete; }
    void cancel_patch(unsigned) override {}
    bool acquire(uint16_t, const cntrl::Document *&, uint32_t &) override { return false; }
    bool state(unsigned operation, uint32_t value) override {
        if (operation == 1) { set_brightness(uint8_t(value)); return true; }
        if (operation == 2) { set_midi_interface(value != 0); return true; }
        return controller && controller->navigate(operation);
    }
    bool stock(uint8_t buttons, cntrl::PatchExecutor &) override { return stock_firmware(buttons); }
    void validation(bool begin) override { if (controller) controller->validation(begin); }
    bool preview_state(unsigned operation, uint32_t value) override { return !controller || controller->preview_state(operation, value); }
    bool selection(unsigned operation, const cntrl::Document *&document, unsigned &node) override {
        return controller && controller->selection(operation, document, node);
    }
    void selection_complete(unsigned operation) override { if (controller) controller->selection_complete(operation); }
};

// Existing controller assertions describe completed writes. Drive the shared
// cooperative executor to that boundary without sleeping or device access.
class TestController : public cntrl::Controller {
public:
    explicit TestController(TestPatchHardware &hardware) : cntrl::Controller(hardware, hardware, hardware) {
        hardware.controller = this;
    }
    void action(unsigned button) { cntrl::Controller::action(button); drain(); }
    void button(unsigned button, bool pressed, uint32_t now) {
        now_ = now; cntrl::Controller::button(button, pressed, now); drain();
    }
    void tick(uint32_t now) { now_ = now; cntrl::Controller::tick(now); drain(); }
private:
    uint32_t now_ = 0;
    void drain() {
        for (unsigned i = 0; i < cntrl::memory_budget && executor().status() == cntrl::PatchExecutor::Status::pending; ++i)
            cntrl::Controller::tick(now_);
    }
};
