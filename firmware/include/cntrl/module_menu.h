#pragma once
#include "hardware.h"
#include <cstdio>

namespace cntrl {
// The target supplies the installed module names and handles the selection.
class ModuleMenu {
public:
    ModuleMenu(Hardware &hardware, const char *const *names, unsigned count)
        : hardware_(hardware), names_(names), count_(count) {}
    bool open() const { return open_; }
    void leave() { open_ = false; }
    void enter() { open_ = locked_ = true; selected_ = 0; render(); }
    void poll(uint8_t held) { if (!held) locked_ = false; }
    // -1: no selection; -2: return to performance; otherwise installed index.
    int button(unsigned button, bool down) {
        if (!open_ || !down || locked_) return -1;
        if (button == 5) { open_ = false; return -2; }
        if (button == 2) { open_ = false; return int(selected_); }
        if (button < 2) {
            selected_ = (selected_ + count_ + (button == 0 ? -1 : 1)) % count_;
            render();
        }
        return -1;
    }
private:
    Hardware &hardware_;
    const char *const *names_;
    unsigned count_, selected_ = 0;
    bool open_ = false, locked_ = false;
    void render() {
        Pane views[3];
        for (auto &view : views) for (auto &line : view.lines)
            line.graphics.size = line.graphics.line_height = 16;
        std::snprintf(views[0].lines[0].text, 64, "A/B browse");
        std::snprintf(views[0].lines[1].text, 64, "C OK, F back");
        std::snprintf(views[1].lines[0].text, 64, "Modules");
        std::snprintf(views[1].lines[1].text, 64, "%s", names_[selected_]);
        for (unsigned i = 0; i < 3; ++i) {
            for (auto &line : views[i].lines) line.wrap = true;
            hardware_.show(i, views[i]);
        }
    }
};
}
