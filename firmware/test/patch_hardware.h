#pragma once
#include <cntrl/controller.h>

// Host-only sink: a successful fake write is delivered immediately. Production
// targets supply the MIDI scheduler and pin real external documents instead.
struct TestPatchHardware : cntrl::Hardware, cntrl::PatchOutputs, cntrl::PatchSources {
    cntrl::Controller *controller = nullptr;
    bool available(const char *output) const override { return supports(output); }
    bool send_patch(unsigned, const char *output, const uint8_t *bytes, unsigned length) override {
        return send(output, bytes, length);
    }
    cntrl::midi::Bridge::Delivery patch_delivery(unsigned) const override { return cntrl::midi::Bridge::Delivery::complete; }
    void cancel_patch(unsigned) override {}
    bool acquire(const char *, const cntrl::Document *&, uint32_t &) override { return false; }
    bool state(unsigned operation, uint32_t value) override {
        if (operation == 1) { set_brightness(uint8_t(value)); return true; }
        if (operation == 2) { set_usb_midi_interface(value != 0); return true; }
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
    cntrl::Chord button(unsigned button, bool pressed, uint32_t now) {
        auto result = cntrl::Controller::button(button, pressed, now); drain(); return result;
    }
    void tick(uint32_t now) { cntrl::Controller::tick(now); drain(); }
private:
    void drain() {
        for (unsigned i = 0; i < cntrl::max_document && executor().status() == cntrl::PatchExecutor::Status::pending; ++i)
            cntrl::Controller::tick(0);
    }
};
