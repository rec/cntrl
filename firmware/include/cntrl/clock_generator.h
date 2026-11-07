#pragma once
#include "midi_bridge.h"
#include <vector>

namespace cntrl {
// Transport is retried per destination. Expired clock ticks are never queued.
class ClockGenerator {
public:
    void set_tempo(uint32_t quarter_us) {
        phase_ = phase_ * quarter_us / quarter_us_;
        quarter_us_ = quarter_us;
    }
    unsigned bpm() const { return (60000000u + quarter_us_ / 2) / quarter_us_; }
    bool running() const { return running_; }
    unsigned skipped = 0, blocked = 0, late_us = 0;

    void start(uint32_t now, unsigned outputs, bool resume) {
        if (running_ || stopping() || !outputs) return;
        unsigned routes = 0;
        for (unsigned mask = outputs; mask; mask >>= 1) ++routes;
        pending_.assign(routes, 0);
        running_ = true; active_ = outputs; previous_ = now; phase_ = 0;
        skipped = blocked = late_us = 0;
        for (unsigned i = 0; i < pending_.size(); ++i)
            if (outputs & (1u << i)) pending_[i] = resume ? 0xfb : 0xfa;
    }
    void stop() {
        for (unsigned i = 0; i < pending_.size(); ++i)
            if (active_ & (1u << i)) pending_[i] = 0xfc;
        active_ = 0; running_ = false;
    }
    unsigned stopping() const {
        unsigned mask = 0;
        for (unsigned i = 0; i < pending_.size(); ++i)
            if (pending_[i] == 0xfc) mask |= 1u << i;
        return mask;
    }
    void poll(uint32_t now, unsigned available, midi::Sink &sink) {
        bool tick = false;
        active_ &= available;
        if (!active_) running_ = false;
        if (running_) {
            phase_ += uint64_t(uint32_t(now - previous_)) * 24;
            if (phase_ >= quarter_us_) {
                unsigned due = unsigned(phase_ / quarter_us_);
                skipped += due - 1;
                unsigned late = unsigned((phase_ - quarter_us_) / 24);
                if (late > late_us) late_us = late;
                phase_ %= quarter_us_;
                tick = true;
            }
        }
        previous_ = now;
        for (unsigned i = 0; i < pending_.size(); ++i) {
            unsigned bit = 1u << i;
            if (!(available & bit)) { pending_[i] = 0; continue; }
            if (pending_[i] && sink.submit(i, {15, {pending_[i], 0, 0}})) pending_[i] = 0;
            if (tick && (active_ & bit) && (pending_[i] || !sink.submit(i, {15, {0xf8, 0, 0}}))) ++blocked;
        }
    }
private:
    std::vector<uint8_t> pending_;
    uint64_t phase_ = 0;
    uint32_t previous_ = 0, quarter_us_ = 500000;
    unsigned active_ = 0;
    bool running_ = false;
};
}
