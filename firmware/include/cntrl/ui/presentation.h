#pragma once
#include <cntrl/hardware.h>
#include <algorithm>

namespace cntrl::ui {
// Management views express context, selection and detail as logical panes.
// Spread them across available displays, or merge text with the selection first.
template<unsigned Count>
void present(Hardware &hardware, const Pane (&parts)[Count], unsigned primary) {
    const unsigned displays = hardware.display_count();
    if (!displays) return;
    if (displays >= Count) {
        unsigned part = 0;
        for (unsigned display = 0; display < displays; ++display) {
            const unsigned destination = Count == 1 ? displays / 2 : (part * (displays - 1) + (Count - 1) / 2) / (Count - 1);
            if (part < Count && display == destination) hardware.show(display, parts[part++]);
            else { Pane blank; blank.graphics = parts[primary].graphics; hardware.show(display, blank); }
        }
        return;
    }
    for (unsigned display = 0; display < displays; ++display) {
        Pane pane; unsigned lines = 0;
        pane.graphics = parts[primary].graphics;
        auto append = [&](unsigned part) {
            for (const auto &line : parts[part].lines) if (line.text[0] && lines < max_lines) {
                auto &out = pane.lines[lines++]; out = line; out.y = -1; out.graphics.top = 0;
            }
        };
        if (!display) append(primary);
        unsigned destination = displays > 1 ? 1 : 0;
        for (unsigned part = 0; part < Count; ++part) if (part != primary) {
            if (destination == display) append(part);
            destination = (destination + 1) % displays;
        }
        pane.graphics.pad = 0;
        const unsigned size = lines * 16 <= hardware.display_height() ? 16 : 8;
        for (unsigned i = 0; i < lines; ++i) {
            auto &line = pane.lines[i]; line.graphics.size = uint8_t(size); line.graphics.line_height = uint16_t(size);
            line.graphics.pad = 0; line.x = uint16_t(std::min<unsigned>(line.x, hardware.display_width() / 8));
        }
        hardware.show(display, pane);
    }
}
} // namespace cntrl::ui
