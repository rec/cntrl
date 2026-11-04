#pragma once
#include "midi_outputs.h"

namespace cntrl {
struct MidiInterfaceControl {
    virtual ~MidiInterfaceControl() = default;
    virtual void configure_interface(bool enabled) = 0;
    virtual void restore_interface() = 0;
};

class MidiInterface {
public:
    struct Status { bool enabled = false, connected = false; bool lit[4] = {}; };
    explicit MidiInterface(MidiInterfaceControl &control) : control_(control) {}
    Status status() const { return {enabled_, connected_, {lit_[0], lit_[1], lit_[2], lit_[3]}}; }
    bool take_redraw() { bool result = dirty_; dirty_ = false; return result; }
    bool open() const { return open_; }
    bool enabled() const { return enabled_; }
    void enter(bool connected, bool enabled) {
        open_ = true; enabled_ = initial_ = enabled; connected_ = connected;
        clear(); dirty_ = true;
    }
    void leave() {
        if (open_ && enabled_ != initial_) control_.restore_interface();
        open_ = enabled_ = false; clear(); dirty_ = true;
    }
    void toggle() {
        if (!open_ || !connected_) return;
        enabled_ = !enabled_; clear(); control_.configure_interface(enabled_); dirty_ = true;
    }
    void connected(bool connected) {
        if (connected_ == connected) return;
        connected_ = connected;
        if (!connected) clear();
        if (open_) dirty_ = true;
    }
    void activity(unsigned connection, bool output, uint32_t now) {
        if (!open_ || !connected_ || !enabled_ || connection >= (output ? midi::output_connections.size() : input_count)) return;
        unsigned i = connection + (output ? input_count : 0);
        last_[i] = now;
        if (!lit_[i]) { lit_[i] = true; dirty_ = true; }
    }
    void tick(uint32_t now) {
        if (!open_) return;
        for (unsigned i = 0; i < port_count; ++i) {
            if (lit_[i] && uint32_t(now - last_[i]) >= pulse_ms) {
                lit_[i] = false; dirty_ = true;
            }
        }
    }
private:
    static constexpr unsigned input_count = 2;
    static constexpr unsigned port_count = input_count + midi::output_connections.size();
    static constexpr uint32_t pulse_ms = 100;
    MidiInterfaceControl &control_;
    bool initial_ = false;
    bool open_ = false, enabled_ = false, connected_ = false, dirty_ = false;
    bool lit_[port_count] = {};
    uint32_t last_[port_count] = {};
    void clear() { for (auto &light : lit_) light = false; }
};
}
