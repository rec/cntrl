#pragma once
#include "hardware.h"
#include <cstdio>

namespace cntrl {
struct MidiInterfaceControl {
    virtual ~MidiInterfaceControl() = default;
    virtual void configure_interface(bool enabled) = 0;
    virtual void restore_interface() = 0;
};

class MidiInterface {
public:
    enum class Port { din_in, trs_in, din_out, trs_out };
    MidiInterface(Hardware &hardware, MidiInterfaceControl &control, ScreenGraphics title = {})
        : hardware_(hardware), control_(control), title_(title) {}
    bool open() const { return open_; }
    bool enabled() const { return enabled_; }
    void enter(bool connected) {
        open_ = true; enabled_ = false; connected_ = connected;
        clear(); control_.configure_interface(false); render();
    }
    void leave() {
        open_ = enabled_ = false; clear(); control_.restore_interface();
    }
    void toggle() {
        if (!open_) return;
        enabled_ = !enabled_; clear(); control_.configure_interface(enabled_); dirty_ = true;
    }
    void connected(bool connected) {
        if (connected_ == connected) return;
        connected_ = connected;
        if (!connected) clear();
        if (open_ && enabled_) dirty_ = true;
    }
    void activity(Port port, uint32_t now) {
        if (!open_ || !enabled_) return;
        unsigned i = unsigned(port);
        last_[i] = now;
        if (!lit_[i]) { lit_[i] = true; dirty_ = true; }
    }
    void tick(uint32_t now) {
        if (!open_) return;
        for (unsigned i = 0; i < 4; ++i) {
            if (lit_[i] && uint32_t(now - last_[i]) >= pulse_ms) {
                lit_[i] = false; dirty_ = true;
            }
        }
        if (dirty_) render();
    }
private:
    static constexpr uint32_t pulse_ms = 100;
    Hardware &hardware_;
    MidiInterfaceControl &control_;
    ScreenGraphics title_;
    bool open_ = false, enabled_ = false, connected_ = false, dirty_ = false;
    bool lit_[4] = {};
    uint32_t last_[4] = {};
    void clear() { for (auto &light : lit_) light = false; }
    void render() {
        Pane views[3];
        for (auto &view : views) {
            view.graphics.foreground = 0xffff; view.graphics.background = 0;
            for (auto &line : view.lines) {
                line.graphics = view.graphics;
                line.centered = true;
            }
        }
        auto &center = views[1];
        auto &title = center.lines[0];
        std::snprintf(title.text, 64, "MIDI interface");
        title.graphics = title_;
        title.graphics.foreground = 0xffff; title.graphics.background = 0;
        title.y = 4;
        auto &host = center.lines[1]; host.y = 36;
        if (enabled_) std::snprintf(host.text, 64, "%s", connected_ ? "Host connected" : "No host");
        auto &toggle = center.lines[2];
        std::snprintf(toggle.text, 64, "%s", enabled_ ? "On" : "Off");
        toggle.graphics.size = toggle.graphics.line_height = 16;
        toggle.y = hardware_.display_height() - 24;
        static const char *const labels[] = {"DIN in", "TRS in", "DIN out", "TRS out"};
        for (unsigned i = 0; i < 4; ++i) {
            auto &light = center.lines[3 + i];
            auto &label = center.lines[7 + i];
            light.centered = label.centered = false;
            light.x = 8 + (i % 2) * 76; label.x = light.x + 12;
            light.y = label.y = 60 + (i / 2) * 20;
            std::snprintf(light.text, 64, "%s", lit_[i] ? "*" : "o");
            light.graphics.foreground = lit_[i] ? 0x07e0 : 0x8410;
            std::snprintf(label.text, 64, "%s", labels[i]);
        }
        auto &back = views[2].lines[0];
        std::snprintf(back.text, 64, "Back"); back.centered = false;
        back.graphics.size = back.graphics.line_height = 16;
        back.x = hardware_.display_width() - 52; back.y = 4;
        for (unsigned i = 0; i < 3; ++i) hardware_.show(i, views[i]);
        dirty_ = false;
    }
};
}
