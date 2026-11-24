#pragma once
#include "device_preferences.h"
#include <cntrl/document.h>

namespace cntrl::ui {
#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(setup_preference_value)
#endif
inline uint32_t setup_preference(const Document &document, unsigned field) {
    Document setup = document;
    setup.bind_globals(nullptr);
    switch (field) {
    case 0: return setup.brightness();
    case 1: return setup.screensaver_timeout() / 1000;
    case 2: return setup.is_midi_interface();
    case 3: return setup.usb_device_midi();
    case 4: return setup.usb_host_midi();
    case 5: return setup.forward_device_to_host();
    case 6: return setup.fast_debounce();
    case 7: return setup.frame_buffers();
    case 8: return setup.use_animations();
    case 9: return setup.animation_color_cycle();
    case 10: return setup.pickup();
    case 11: return setup.pickup_pairing_ms();
    case 12: case 13: return setup.scroll_ms(field == 13);
    case 14: case 15: case 16: return setup.scroll_direction(field - 14);
    case 17: return setup.navigation_hold_ms();
    case 18: return setup.navigation_repeat_ms();
    case 19: case 20: case 21: case 22: return setup.trs_modes()[field - 19];
    case 23: case 24: case 25: case 26: return setup.auxiliary_combined(field - 23);
    case 28: return setup.page_settings()[PageSettings::clock_outputs];
    case 29: return setup.pulse_period_ms();
    case 30: return setup.pulse_minimum();
    case 31: return setup.pulse_maximum();
    case 32: return setup.pulse_delay_ms();
    case 33: return setup.loop_stop_reset_controls();
    default: return 0;
    }
}
} // namespace cntrl::ui
