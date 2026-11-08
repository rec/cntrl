#pragma once
#define CNTRL_USB_BLOCK_SIZE 4096
#include <cntrl/usb_config.h>
#include <array>
#include <cntrl/midi_outputs.h>
#include <cntrl/ui/display.h>
#include <cntrl/ui/controls.h>
namespace cntrl::target {
inline constexpr unsigned memory_capacity = 524288;
inline constexpr unsigned memory_runtime = 294912;
inline constexpr unsigned max_switches = 5;
inline constexpr unsigned scan_interval_ms = 1;
inline constexpr unsigned display_count = 3;
inline constexpr unsigned button_count = 6;
inline constexpr std::array<ui::Viewport, display_count> viewports = {{
    {160, 128, 72, 8},
    {160, 128, 72, 8},
    {160, 128, 72, 8},
}};
inline constexpr std::array<const char *, display_count> display_ids = {{
    "left",
    "center",
    "right",
}};
inline constexpr std::array<const char *, display_count> display_labels = {{
    "Left",
    "Center",
    "Right",
}};
inline constexpr std::array<const char *, button_count> button_labels = {{
    "A",
    "B",
    "C",
    "D",
    "E",
    "F",
}};
inline constexpr unsigned maximum_display_area = 20480;
inline constexpr unsigned total_display_area = 61440;
inline constexpr std::array<midi::OutputConnection, 10> endpoints = {{
    {"din", "DIN", 0, midi::OutputTransport::physical, true, true, midi::no_port},
    {"trs", "TRS", 1, midi::OutputTransport::physical, true, true, midi::no_port},
    {"configurable-1", "TRS 1", 2, midi::OutputTransport::physical, false, true, 0},
    {"configurable-2", "TRS 2", 3, midi::OutputTransport::physical, false, true, 1},
    {"configurable-3", "TRS 3", 4, midi::OutputTransport::physical, false, true, 2},
    {"configurable-4", "TRS 4", 5, midi::OutputTransport::physical, false, true, 3},
    {"usb-device", "USB device", 6, midi::OutputTransport::usb_device, true, true, midi::no_port},
    {"usb-return", "USB return", 7, midi::OutputTransport::usb_device, false, false, midi::no_port},
    {"usb-host", "USB host", 8, midi::OutputTransport::usb_host, true, true, midi::no_port},
    {"usb-host-return", "Hosted MIDI return", 9, midi::OutputTransport::usb_device, false, false, midi::no_port},
}};
inline constexpr midi::Outputs midi_connections{endpoints.data(), 10};
inline constexpr std::array<midi::OutputConnection, 2> cable_endpoints = {{
    endpoints[0],
    endpoints[1],
}};
inline constexpr midi::Outputs cable_connections{cable_endpoints.data(), 2};
inline constexpr unsigned port_count = 4;
inline constexpr std::array<ui::ControlBinding, button_count> control_bindings = {{
    {2, "A"},
    {1, "B"},
    {0, "C"},
    {5, "D"},
    {4, "E"},
    {3, "F"},
}};
inline constexpr ui::Controls controls{
    0,
    1,
    2,
    3,
    4,
    5,
    5,
    3,
    2,
    control_bindings.data(), button_count};
} // namespace cntrl::target
