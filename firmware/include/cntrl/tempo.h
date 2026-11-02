#pragma once
#include <cstdint>

namespace cntrl {
class TapTempo {
public:
    void configure(unsigned window, unsigned taps, uint32_t minimum, uint32_t gap) {
        window_ = window; taps_ = taps; minimum_ = minimum; gap_ = gap;
        count_ = next_ = 0; sum_ = 0; last_known_ = false;
    }
    void tap(uint32_t now) {
        if (!window_) return;
        if (!last_known_ || uint32_t(now - last_) > gap_) {
            count_ = next_ = 0; sum_ = 0; last_ = now; last_known_ = true;
            beat_anchor_ = now; beat_known_ = true; return;
        }
        uint32_t interval = now - last_;
        if (interval < minimum_) return;
        last_ = now;
        beat_anchor_ = now; beat_known_ = true;
        if (count_ == window_) sum_ -= intervals_[next_];
        else ++count_;
        intervals_[next_] = interval; sum_ += interval;
        next_ = (next_ + 1) % window_;
        if (count_ + 1 >= taps_) period_ = (sum_ + count_ / 2) / count_;
    }
    uint32_t period() const { return period_; }
    bool flash(uint32_t now, uint32_t duration) {
        if (!period_ || !beat_known_) return false;
        uint32_t elapsed = now - beat_anchor_;
        beat_anchor_ += elapsed / period_ * period_;
        return uint32_t(now - beat_anchor_) < duration;
    }
private:
    uint32_t intervals_[8] = {}, sum_ = 0, last_ = 0, period_ = 0;
    unsigned window_ = 0, taps_ = 0, count_ = 0, next_ = 0;
    uint32_t minimum_ = 0, gap_ = 0, beat_anchor_ = 0;
    bool last_known_ = false, beat_known_ = false;
};

class ClockTempo {
public:
    void configure(unsigned clocks, unsigned beats, uint32_t timeout) {
        clocks_ = clocks; window_ = clocks * beats; timeout_ = timeout;
        source_ = -1; count_ = 0; stale_ = true;
    }
    void clock(unsigned source, uint32_t now) {
        if (!clocks_) return;
        if (source_ < 0 || stale_ || uint32_t(now - last_) > timeout_) {
            source_ = int(source); last_ = now; count_ = 0; elapsed_ = 0; stale_ = false; return;
        }
        if (source_ != int(source)) return;
        elapsed_ += uint32_t(now - last_);
        last_ = now;
        ++count_;
        if (count_ >= window_ && elapsed_) {
            period_us_ = elapsed_ * clocks_ / count_;
            elapsed_ = 0; count_ = 0;
        }
    }
    uint64_t period_us() const { return period_us_; }
    void poll(uint32_t now) { if (source_ >= 0 && uint32_t(now - last_) > timeout_) stale_ = true; }
    bool active() const { return source_ >= 0 && !stale_; }
private:
    unsigned clocks_ = 0, window_ = 0, count_ = 0;
    int source_ = -1;
    uint32_t timeout_ = 0, last_ = 0;
    uint64_t period_us_ = 0, elapsed_ = 0;
    bool stale_ = true;
};
} // namespace cntrl
