#pragma once
#include "midi_bridge.h"

namespace cntrl {
namespace midi {
struct BeatPosition {
    uint64_t value = 0;
    uint32_t restart = 0;
    bool running = false;
};
}
// Transport is retried per destination. Expired clock ticks are never queued.
class ClockGenerator {
public:
    void set_tempo(uint32_t quarter_us) {
        phase_ = phase_ * quarter_us / quarter_us_;
        beat_remainder_ = beat_remainder_ * quarter_us / quarter_us_;
        quarter_us_ = quarter_us;
    }
    unsigned bpm() const { return (60000000u + quarter_us_ / 2) / quarter_us_; }
    unsigned period_us() const { return quarter_us_; }
    bool running() const { return running_; }
    midi::BeatPosition beat_position(uint32_t now) {
        advance_beats(now);
        return {beats_, restart_, running_};
    }
    unsigned skipped = 0, blocked = 0, late_us = 0;

    void start(uint32_t now, unsigned outputs, bool resume) {
        if (running_ || stopping()) return;
        start_pending_ = outputs; stop_pending_ = 0; resume_ = resume;
        running_ = true; active_ = outputs; previous_ = now; phase_ = 0;
        beat_previous_ = now;
        if (!resume) { beats_ = beat_remainder_ = 0; ++restart_; }
        skipped = blocked = late_us = 0;
    }
    void stop() {
        stop_pending_ = active_; start_pending_ = 0;
        active_ = 0; running_ = false;
    }
    unsigned stopping() const {
        return stop_pending_;
    }
    void poll(uint32_t now, unsigned available, midi::Sink &sink) {
        advance_beats(now);
        bool tick = false;
        active_ &= available;
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
        start_pending_ &= available; stop_pending_ &= available;
        for (unsigned i = 0, mask = active_ | start_pending_ | stop_pending_; mask; ++i, mask >>= 1) {
            unsigned bit = 1u << i;
            if (!(available & bit)) continue;
            const uint8_t pending = stop_pending_ & bit ? 0xfc : start_pending_ & bit ? (resume_ ? 0xfb : 0xfa) : 0;
            if (pending && sink.submit(i, {15, {pending, 0, 0}})) { start_pending_ &= ~bit; stop_pending_ &= ~bit; }
            if (tick && (active_ & bit) && (((start_pending_ | stop_pending_) & bit) || !sink.submit(i, {15, {0xf8, 0, 0}}))) ++blocked;
        }
    }
private:
    uint64_t beats_ = 0, beat_remainder_ = 0;
    uint32_t beat_previous_ = 0, restart_ = 0;
    void advance_beats(uint32_t now) {
        uint64_t elapsed = uint32_t(now - beat_previous_); beat_previous_ = now;
        if (!running_) return;
        uint64_t numerator = (elapsed % quarter_us_) * (uint64_t(1) << 32) + beat_remainder_;
        beats_ += (elapsed / quarter_us_) * (uint64_t(1) << 32) + numerator / quarter_us_;
        beat_remainder_ = numerator % quarter_us_;
    }
    unsigned start_pending_ = 0, stop_pending_ = 0;
    bool resume_ = false;
    uint64_t phase_ = 0;
    uint32_t previous_ = 0, quarter_us_ = 500000;
    unsigned active_ = 0;
    bool running_ = false;
};
}
