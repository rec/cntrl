#pragma once
#include <cstdint>
#include <algorithm>
#include "clock_generator.h"

namespace cntrl {
class TapTempo {
public:
    void configure(unsigned window, unsigned taps, uint32_t minimum, uint32_t gap) {
        window_ = window; taps_ = taps; minimum_ = minimum; gap_ = gap;
        count_ = next_ = 0; sum_ = 0; last_known_ = false;
    }
    bool tap(uint32_t now) {
        if (!window_) return false;
        const uint32_t interval = now - last_;
        if (!last_known_ || interval > gap_
            || (count_ && period_ && uint64_t(interval) > uint64_t(period_) * 2)) {
            count_ = next_ = 0; sum_ = 0; last_ = now; last_known_ = true;
            beat_anchor_ = now; beat_known_ = true; return false;
        }
        if (interval < minimum_) return false;
        last_ = now;
        beat_anchor_ = now; beat_known_ = true;
        if (count_ == window_) sum_ -= intervals_[next_];
        else ++count_;
        intervals_[next_] = interval; sum_ += interval;
        next_ = (next_ + 1) % window_;
        if (count_ + 1 < taps_) return false;
        period_ = (sum_ + count_ / 2) / count_;
        return true;
    }
    uint32_t period() const { return period_; }
    bool flash(uint32_t now, unsigned fraction) {
        if (!period_ || !beat_known_) return false;
        uint32_t elapsed = now - beat_anchor_;
        beat_anchor_ += elapsed / period_ * period_;
        return uint64_t(now - beat_anchor_) * 1000 < uint64_t(period_) * fraction;
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
        beat_value_ = 0; beat_remainder_ = 0; ++restart_;
        playing_ = true; first_tick_ = true; waiting_ = false;
        continuing_value_ = continuing_remainder_ = 0; continuing_time_ = 0;
    }
    void clock(unsigned source, uint32_t now) {
        if (!clocks_) return;
        holdover_position(now);
        if (source_ < 0 || stale_ || uint32_t(now - last_) > timeout_) {
            source_ = int(source); last_ = now; count_ = 0; elapsed_ = 0; stale_ = false; first_tick_ = false; waiting_ = false; return;
        }
        if (source_ != int(source)) return;
        if (playing_) {
            if (first_tick_) first_tick_ = false;
            else {
                uint64_t numerator = (uint64_t(1) << 32) + beat_remainder_;
                beat_value_ += numerator / clocks_; beat_remainder_ = unsigned(numerator % clocks_);
            }
        }
        elapsed_ += uint32_t(now - last_);
        last_ = now;
        ++count_;
        if (count_ >= window_ && elapsed_) {
            uint64_t period = elapsed_ * clocks_ / count_;
            if (period_us_) continuing_remainder_ = continuing_remainder_ * period / period_us_;
            else continuing_value_ = beat_value_;
            period_us_ = period;
            elapsed_ = 0; count_ = 0;
        }
    }
    uint64_t period_us() const { return period_us_; }
    bool transport(unsigned source, uint8_t status, uint32_t now) {
        if (status == 0xf8) { clock(source, now); return true; }
        if (status < 0xfa || status > 0xfc || (source_ >= 0 && source_ != int(source) && !stale_)) return false;
        holdover_position(now);
        if (status == 0xfc) beat_value_ = beat_position(now).value;
        source_ = int(source);
        playing_ = status != 0xfc;
        waiting_ = playing_;
        if (status == 0xfa) { beat_value_ = 0; beat_remainder_ = 0; continuing_value_ = continuing_remainder_ = 0; ++restart_; }
        if (playing_) { first_tick_ = true; last_ = now; stale_ = true; }
        return true;
    }
    midi::BeatPosition beat_position(uint32_t now) const {
        uint64_t value = beat_value_;
        if (playing_ && !stale_ && period_us_ && clocks_) {
            uint64_t elapsed = std::min(uint64_t(uint32_t(now - last_)), period_us_ / clocks_);
            value += elapsed * (uint64_t(1) << 32) / period_us_;
        }
        return {value, restart_, playing_ && active()};
    }
    midi::BeatPosition holdover_position(uint32_t now) {
        uint64_t elapsed = uint32_t(now - continuing_time_); continuing_time_ = now;
        if (playing_ && period_us_ && source_ >= 0) {
            uint64_t numerator = elapsed * (uint64_t(1) << 32) + continuing_remainder_;
            continuing_value_ += numerator / period_us_; continuing_remainder_ = numerator % period_us_;
        }
        return {continuing_value_,restart_,playing_ && period_us_ && source_ >= 0};
    }
    void poll(uint32_t now) {
        if (waiting_ && uint32_t(now - last_) > timeout_) waiting_ = false;
        if (!stale_ && source_ >= 0 && uint32_t(now - last_) > timeout_) {
            beat_value_ = beat_position(now).value; stale_ = true; first_tick_ = true;
        }
    }
    bool active() const { return source_ >= 0 && !stale_; }
    bool available() const { return active() || waiting_ || (!playing_ && source_ >= 0); }
private:
    uint64_t beat_value_ = 0;
    uint32_t restart_ = 0;
    unsigned beat_remainder_ = 0;
    bool playing_ = true, first_tick_ = true, waiting_ = false;
    unsigned clocks_ = 0, window_ = 0, count_ = 0;
    int source_ = -1;
    uint32_t timeout_ = 0, last_ = 0;
    uint64_t period_us_ = 0, elapsed_ = 0;
    bool stale_ = true;
    uint64_t continuing_value_ = 0, continuing_remainder_ = 0;
    uint32_t continuing_time_ = 0;
};
} // namespace cntrl
