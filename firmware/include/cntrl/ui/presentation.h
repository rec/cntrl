#pragma once
#include <cntrl/hardware.h>
#include <algorithm>
#include <vector>

namespace cntrl::ui {
// Management views express context, selection and detail as logical panes.
// Spread them across available windows, or merge text with the selection first.
template<unsigned Count>
void present(Hardware &hardware, const Pane (&parts)[Count], unsigned primary) {
    const unsigned windows = hardware.display_count();
    if (!windows) return;
    if (windows >= Count) {
        unsigned part = 0;
        for (unsigned window = 0; window < windows; ++window) {
            const unsigned destination = Count == 1 ? windows / 2 : (part * (windows - 1) + (Count - 1) / 2) / (Count - 1);
            if (part < Count && window == destination) hardware.show(window, parts[part++]);
            else { Pane blank; blank.graphics = parts[primary].graphics; hardware.show(window, blank); }
        }
        return;
    }
    std::vector<Pane> merged(windows);
    std::vector<unsigned> lines(windows);
    for (unsigned window = 0; window < windows; ++window) merged[window].graphics = parts[primary].graphics;
    auto append = [&](unsigned part, unsigned window) {
        for (const auto &line : parts[part].lines) if (line.text[0] && lines[window] < max_lines) {
            auto &out = merged[window].lines[lines[window]++]; out = line; out.y = -1; out.graphics.top = 0;
        }
    };
    append(primary, 0);
    unsigned destination = windows > 1 ? 1 : 0;
    for (unsigned part = 0; part < Count; ++part) if (part != primary) {
        append(part, destination); destination = (destination + 1) % windows;
    }
    for (unsigned window = 0; window < windows; ++window) {
        auto &pane = merged[window]; pane.graphics.pad = 0;
        const unsigned size = lines[window] * 16 <= hardware.display_height() ? 16 : 8;
        for (unsigned i = 0; i < lines[window]; ++i) {
            auto &line = pane.lines[i]; line.graphics.size = line.graphics.line_height = size;
            line.graphics.pad = 0; line.x = std::min<unsigned>(line.x, hardware.display_width() / 8);
        }
        hardware.show(window, pane);
    }
}
} // namespace cntrl::ui
