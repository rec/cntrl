#pragma once
#include "renderer.h"

// Optical fixture using the production renderer, cadence and bus operations.
class ScrollTimingProbe {
public:
    bool active() const { return stage_ != 0; }
    void arm(DisplayRenderer &renderer) {
        renderer.invalidate();
        for (unsigned panel = 0; panel < 3; ++panel) renderer.show(panel, pane(panel));
        stage_ = 1; trial_ = 0; hidden_ = 0;
    }
    void stop() { stage_ = 0; }
    void start() { if (stage_ == 2) stage_ = 3; }
    void poll(uint32_t now, DisplayRenderer &renderer) {
        if (!active()) return;
        renderer.draw(now);
        if (!renderer.ready()) return;
        if (stage_ == 1) {
            // Seed only the two hidden columns at the initial zero offset.
            // Fine stripes identify stale RAM exposed without a staged write.
            uint8_t pixels[256];
            for (unsigned y = 0; y < 128; ++y)
                pixels[2 * y] = pixels[2 * y + 1] = y % 8 < 4 ? 255 : 0;
            write_display_column(hidden_ / 2, 160 + hidden_ % 2, pixels);
            if (++hidden_ == 6) stage_ = 2;
        }
        else if (stage_ == 3) {
            const bool undo = trial_ % 2;
            renderer.transition(undo, 500);
            for (unsigned panel = 0; panel < 3; ++panel)
                renderer.show(panel, pane(panel + (undo ? 0 : 1)));
            stage_ = 4;
        } else if (stage_ == 4) {
            since_ = now; stage_ = 5;
        } else if (stage_ == 5 && uint32_t(now - since_) >= 1500) {
            if (++trial_ == 8) { stage_ = 2; trial_ = 0; }
            else stage_ = 3;
        }
    }
    static cntrl::Pane pane(unsigned identity) {
        cntrl::Pane view;
        view.graphics.pad = 0;
        view.graphics.background = 0;
        for (unsigned band = 0; band < 8; ++band) {
            auto &line = view.lines[band];
            line.graphics.pad = 0;
            line.graphics.background = ((identity + band) % 4 < 2) ? 0xffff : 0x4208;
            line.graphics.foreground = 0;
            line.graphics.line_height = 16;
            line.x = 0; line.y = int16_t(band * 16);
            std::snprintf(line.text, sizeof(line.text), "%c01234567890123456789012345", 'A' + identity);
        }
        return view;
    }
private:
    unsigned stage_ = 0, trial_ = 0, hidden_ = 0;
    uint32_t since_ = 0;
};
