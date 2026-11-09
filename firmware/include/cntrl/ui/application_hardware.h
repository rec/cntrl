#pragma once
#include <cntrl/hardware.h>
#include <cntrl/midi_outputs.h>

namespace cntrl::ui {
// Platform provides page_host() plus its physical Hardware services. The shared
// facade connects page presentation to the Hardware interface once.
template<class Platform>
class ApplicationHardware : public Hardware {
public:
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_hardware_show)
#endif
    void show(unsigned pane, const Pane &content) override { presentation().show(pane, content); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_hardware_configure_pages)
#endif
    void configure_pages(const Document &document) override { presentation().configure_pages(document); }
    bool refresh_pages() override { return presentation().refresh_pages(); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_hardware_configure_page_settings)
#endif
    void configure_page_settings(const PageSettings &settings) override { presentation().configure_page_settings(settings); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_hardware_browse)
#endif
    void browse_page(bool reverse) override { presentation().browse_page(reverse); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_hardware_tap)
#endif
    void tap_tempo(uint32_t now) override { presentation().tap_tempo(now); }
    void set_target_pulse(unsigned period, uint8_t low, uint8_t high, unsigned delay) override { presentation().set_target_pulse(period, low, high, delay); }
    void transition(bool undo, std::optional<unsigned> duration_ms, int pane = -1, bool vertical = false, bool panorama = true) override {
        presentation().transition(undo, duration_ms, pane, vertical, panorama);
    }
    void set_brightness(uint8_t value, bool temporary = false) override { presentation().set_brightness(value, temporary); }
    void set_splash(bool enabled, uint32_t idle_ms) override { presentation().set_splash(enabled, idle_ms); }
    void set_movies(const uint8_t *startup, unsigned startup_count, const uint8_t *idle, unsigned idle_count) override { presentation().set_movies(startup, startup_count, idle, idle_count); }
    void error(const char *message) override { presentation().error(message); }
    unsigned clock_available() const {
        const auto &platform = static_cast<const Platform &>(*this);
        return midi::available_outputs(platform.midi_outputs(), [&](const char *name) { return platform.available(name); });
    }
    bool keyboard_button(unsigned input, bool pressed) { return presentation().keyboard_button(input, pressed); }
    bool monitor_button(unsigned input, bool pressed) { return presentation().monitor_button(input, pressed); }
    void poll_keyboard() { presentation().poll_keyboard(); }
    void blank_performance() { presentation().blank_performance(); }
    void select_trs_jack() { presentation().select_trs_jack(); }
    bool trs_button(unsigned input) { return presentation().trs_button(input); }
    void enter_page() { presentation().enter_page(); }
    void leave_page() { presentation().leave_page(); }
    bool stop_clock() { return presentation().stop_clock(); }
    bool clock_control(unsigned input, bool pressed) { return presentation().clock_control(input, pressed); }
    void draw() { presentation().draw(); }
    bool expression_button(unsigned input) { return presentation().expression_button(input); }
    void display(uint32_t now, bool transfer_busy) { presentation().display(now, transfer_busy); }
private:
    auto &presentation() { return static_cast<Platform &>(*this).page_host(); }
};
} // namespace cntrl::ui
