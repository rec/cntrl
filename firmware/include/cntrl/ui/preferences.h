#pragma once
#include "device_preferences.h"
#include <cntrl/document.h>

namespace cntrl::ui {
constexpr unsigned page_preference = GlobalSettings::count;
static_assert(page_preference + PageSettings::count <= 64);
inline constexpr uint8_t preference_faces[] = {17, 16, 8, 19};
inline constexpr const char *preference_face_names[] = {"Arimo", "AtkinsonNextSemiBold", "CourierPrime", "TamzenBold7x13"};
inline constexpr unsigned preference_color_shift[] = {11, 5, 0};
inline constexpr unsigned preference_color_max[] = {31, 63, 31};
inline constexpr const char *preference_color_channel[] = {"Red", "Green", "Blue"};
#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(adjust_preference_color)
#endif
inline uint16_t adjust_color(uint16_t color, unsigned component, int delta) {
    const unsigned shift = preference_color_shift[component];
    const unsigned maximum = preference_color_max[component];
    const int value = int((color >> shift) & maximum) + delta;
    return uint16_t((color & ~(maximum << shift)) | (unsigned(std::clamp(value, 0, int(maximum))) << shift));
}
#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(preference_color_caption)
#endif
inline void color_caption(char *text, unsigned capacity, uint16_t color) {
    std::snprintf(text, capacity, "#%02X%02X%02X", unsigned((color >> 11) * 255 / 31),
        unsigned(((color >> 5) & 63) * 255 / 63), unsigned((color & 31) * 255 / 31));
}

inline constexpr Preference utility_preferences[] = {
    {page_preference + PageSettings::clock_timeout, PreferenceKind::milliseconds, 1, 65535, "Time", "Clock timeout (ms)", {}},
    {page_preference + PageSettings::clocks_per_quarter, PreferenceKind::integer, 1, 960, "Time", "Clocks per quarter note", {}},
    {page_preference + PageSettings::clock_beats, PreferenceKind::integer, 1, 16, "Time", "Beats to average", {}},
    {page_preference + PageSettings::clock_options, PreferenceKind::boolean, 0, 1, "Time", "Auto-select clock", {}},
    {page_preference + PageSettings::tap_intervals, PreferenceKind::integer, 1, 8, "Time", "Tap intervals to average", {}},
    {page_preference + PageSettings::minimum_taps, PreferenceKind::integer, 2, 9, "Time", "Minimum taps", {}},
    {page_preference + PageSettings::minimum_interval, PreferenceKind::milliseconds, 1, 65535, "Time", "Minimum tap interval (ms)", {}},
    {page_preference + PageSettings::reset_gap, PreferenceKind::milliseconds, 1, 65535, "Time", "Tap reset gap (ms)", {}},
    {page_preference + PageSettings::slot1, PreferenceKind::integer, 20, 600, "Tempo presets", "Tempo 1 (BPM)", {}},
    {page_preference + PageSettings::slot2, PreferenceKind::integer, 0, 600, "Tempo presets", "Tempo 2 (BPM)", {}},
    {page_preference + PageSettings::slot3, PreferenceKind::integer, 0, 600, "Tempo presets", "Tempo 3 (BPM)", {}},
    {page_preference + PageSettings::slot4, PreferenceKind::integer, 0, 600, "Tempo presets", "Tempo 4 (BPM)", {}},
    {page_preference + PageSettings::flash_fraction, PreferenceKind::integer, 1, 1000, "Time display", "Flash fraction (per 1000)", {}},
    {page_preference + PageSettings::diameter, PreferenceKind::integer, 1, 128, "Time display", "Flasher diameter (px)", {}},
    {page_preference + PageSettings::annulus, PreferenceKind::integer, 1, 64, "Time display", "Flasher ring width (px)", {}},
    {page_preference + PageSettings::title_height, PreferenceKind::integer, 8, 128, "Time display", "Title height (px)", {}},
    {page_preference + PageSettings::tempo_height, PreferenceKind::integer, 8, 128, "Time display", "Tempo height (px)", {}},
    {page_preference + PageSettings::padding, PreferenceKind::integer, 0, 64, "Time display", "Padding (px)", {}},
    {page_preference + PageSettings::digits, PreferenceKind::integer, 1, 4, "Time display", "Tempo digits", {}},
    {page_preference + PageSettings::decimals, PreferenceKind::integer, 0, 2, "Time display", "Tempo decimal places", {}},
    {page_preference + PageSettings::title_font, PreferenceKind::font, 0, 3, "Page fonts", "Title font", {}},
    {page_preference + PageSettings::tempo_font, PreferenceKind::font, 0, 3, "Page fonts", "Tempo font", {}},
    {page_preference + PageSettings::status_font, PreferenceKind::font, 0, 3, "Page fonts", "Status font", {}},
    {page_preference + PageSettings::off_color, PreferenceKind::color, 0, 65535, "Time colors", "Flasher off color", {}},
    {page_preference + PageSettings::on_color, PreferenceKind::color, 0, 65535, "Time colors", "Flasher on color", {}},
    {page_preference + PageSettings::ring_color, PreferenceKind::color, 0, 65535, "Time colors", "Flasher ring color", {}},
    {page_preference + PageSettings::background, PreferenceKind::color, 0, 65535, "Time colors", "Page background color", {}},
    {page_preference + PageSettings::refresh, PreferenceKind::milliseconds, 1, 65535, "Status", "Status refresh (ms)", {}},
};
inline constexpr unsigned pedal_preferences = sizeof(preferences) / sizeof(preferences[0]);
inline constexpr unsigned layout_preference_index = pedal_preferences + sizeof(utility_preferences) / sizeof(utility_preferences[0]);
inline constexpr unsigned setup_preferences = layout_preference_index + 1;
inline constexpr Preference layout_preference{64, PreferenceKind::integer, 0, 0, "Layout", "Page layout", {}};
inline const Preference &preference_at(unsigned index) {
    if (index == layout_preference_index) return layout_preference;
    return index < pedal_preferences ? preferences[index] : utility_preferences[index - pedal_preferences];
}
inline bool optional_tempo_preference(unsigned field) {
    return field >= page_preference + PageSettings::slot2 && field <= page_preference + PageSettings::slot4;
}
#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(preference_face_index)
#endif
inline unsigned preference_face_index(unsigned face) {
    for (unsigned i = 0; i < sizeof(preference_faces); ++i)
        if (preference_faces[i] == face) return i;
    return 0;
}

#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(setup_preference_value)
#endif
inline uint32_t setup_preference(const Document &document, unsigned field) {
    Document setup = document;
    setup.bind_globals(nullptr);
    if (field >= page_preference) {
        const auto word = PageSettings::Word(field - page_preference);
        const auto value = setup.page_settings()[word];
        return word == PageSettings::clock_options ? !(value & 2) : value;
    }
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
