#pragma once
#include <cstdint>

namespace cntrl {
// All configured apps are polled. Only the selected app receives buttons and
// contributes windows to the display.
struct App {
    virtual ~App() = default;
    virtual bool busy() const = 0;
    virtual void tick(uint32_t now) = 0;
    virtual void button(unsigned button, bool pressed, uint32_t now) = 0;
    virtual void refresh() = 0;
};
}
