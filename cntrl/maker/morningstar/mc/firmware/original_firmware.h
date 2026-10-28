#pragma once
#include <cntrl/hardware.h>
#include <cstdio>

// Target-specific module: the original image exists only on this controller.
class OriginalFirmware {
public:
    OriginalFirmware(cntrl::Hardware &hardware, bool (*start)()) : hardware_(hardware), start_(start) {}
    bool open() const { return open_; }
    void enter() {
        open_ = true;
        cntrl::Pane views[3];
        for (auto &view : views) for (auto &line : view.lines) {
            line.graphics.size = line.graphics.line_height = 16;
            line.wrap = true;
        }
        const char *text[3][2] = {{"Original", "firmware"}, {"C start", "F back"}, {"A+F returns", "to patch mode"}};
        for (unsigned i = 0; i < 3; ++i) {
            for (unsigned j = 0; j < 2; ++j) std::snprintf(views[i].lines[j].text, 64, "%s", text[i][j]);
            hardware_.show(i, views[i]);
        }
    }
    void leave() { open_ = false; }
    void start() { if (open_ && !start_()) hardware_.error("Firmware switch failed"); }
private:
    cntrl::Hardware &hardware_;
    bool (*start_)();
    bool open_ = false;
};
