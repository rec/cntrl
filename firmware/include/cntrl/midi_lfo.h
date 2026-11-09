#pragma once
#include "midi_parameter.h"
#include "lfo_sine.h"
#include "clock_generator.h"
#include <cstdint>

namespace cntrl::midi {
struct LfoConfig {
    Parameter target;
    uint8_t shape = 0, off = 0;
    bool once = false, free = false, beats = false;
    int32_t low = 0, high = 127, final_value = 0;
    uint64_t period = 1000000;
    uint32_t denominator = 1, phase = 0, interval = 10000, seed = 0;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(lfo_config_decode)
#endif
    bool decode(const uint8_t *data, unsigned size) {
        if (size != 45) return false;
        unsigned cursor = 0;
        auto integer = [&](unsigned count) {
            uint64_t value = 0;
            for (unsigned i = 0; i < count; ++i) value |= uint64_t(data[cursor++]) << (i * 8);
            return value;
        };
        target.kind = uint8_t(integer(1)); target.channel = uint8_t(integer(1)); target.address = uint16_t(integer(2));
        shape = uint8_t(integer(1)); unsigned a = unsigned(integer(1)), b = unsigned(integer(1)), c = unsigned(integer(1));
        once = a != 0; free = b != 0; beats = c != 0;
        low = int32_t(integer(4)); high = int32_t(integer(4)); period = integer(8);
        denominator = uint32_t(integer(4)); phase = uint32_t(integer(4)); interval = uint32_t(integer(4)); seed = uint32_t(integer(4));
        off = uint8_t(integer(1)); final_value = int32_t(integer(4));
        target.value = low;
        if (!target.valid() || high < target.minimum() || high > target.maximum() || shape > 7 || a > 1 || b > 1 || c > 1
            || (once && free) || off > 2 || (off != 1 && final_value) || (off == 1 && (final_value < target.minimum() || final_value > target.maximum()))
            || (shape != 5 && shape != 7 && seed) || interval < 1000 || interval > 60000000 || !period || !denominator) return false;
        return beats ? denominator <= 65535 && period <= uint64_t(4096) * denominator
            : denominator == 1 && period >= 1000 && period <= 3600000000ull;
    }
};

class Lfo {
public:
    LfoConfig config;
    uint64_t id = 0, next = 0;
    bool generated = false, ended = false, finishing = false, waiting = false;
    void scope(uint64_t now, BeatPosition beat) {
        phase_ = remainder_ = 0; previous_ = now; previous_beat_ = beat.value;
        restart_ = beat.restart; id = 0; generated = ended = finishing = false;
    }
    void advance(uint64_t now, BeatPosition beat, bool enabled) {
        uint64_t elapsed = config.beats ? beat.value - previous_beat_ : now - previous_;
        if (config.beats && (beat.restart != restart_ || beat.value < previous_beat_)) {
            elapsed = 0;
            if (enabled) phase_ = remainder_ = 0;
        }
        previous_ = now; previous_beat_ = beat.value; restart_ = beat.restart;
        waiting = config.beats && !beat.running;
        if ((!enabled && !config.free) || ended) return;
        uint64_t multiplier = config.beats ? config.denominator : (uint64_t(1) << 32);
        if (config.once && elapsed / config.period >= (((uint64_t(1) << 32) + multiplier - 1) / multiplier)) {
            phase_ = uint64_t(1) << 32; remainder_ = 0; return;
        }
        // Split before multiplication. The remainder product is bounded by
        // validated period/denominator limits; unsigned whole-cycle wrap is
        // intentional and gives a 2^32-cycle seeded random sequence.
        uint64_t numerator = (elapsed % config.period) * multiplier + remainder_;
        phase_ += (elapsed / config.period) * multiplier + numerator / config.period;
        remainder_ = numerator % config.period;
    }
    void enable(uint64_t now, BeatPosition beat, uint64_t identity) {
        advance(now, beat, false);
        if (!config.free) phase_ = remainder_ = 0;
        id = identity; next = now; generated = ended = finishing = false;
    }
    bool complete() const { return config.once && phase_ >= (uint64_t(1) << 32); }
    int sample(bool terminal = false) const {
        uint64_t position = terminal ? uint64_t(config.phase) + (uint64_t(1) << 32) - 1 : phase_ + config.phase;
        uint64_t normalized = shape(config.shape, position, config.seed);
        int64_t numerator = int64_t(config.high - config.low) * int64_t(normalized) + (int64_t(1) << 31);
        int64_t rounded = numerator / (int64_t(1) << 32);
        if (numerator < 0 && numerator % (int64_t(1) << 32)) --rounded;
        return int(config.low + rounded);
    }
    static uint64_t shape(unsigned kind, uint64_t phase, uint32_t seed) {
        constexpr uint64_t one = uint64_t(1) << 32;
        uint32_t p = uint32_t(phase), cycle = uint32_t(phase >> 32);
        if (kind == 0) {
            unsigned quadrant = p >> 30;
            uint32_t offset = p & ((1u << 30) - 1);
            if (quadrant & 1) offset = (1u << 30) - offset;
            unsigned index = offset >> 22;
            uint64_t value = lfo_sine[index];
            if (index < 256) value += uint64_t(lfo_sine[index + 1] - lfo_sine[index]) * (offset & ((1u << 22) - 1)) >> 22;
            return quadrant < 2 ? (one >> 1) + value : (one >> 1) - value;
        }
        if (kind == 1) return p < (one >> 1) ? uint64_t(p) * 2 : (one - p) * 2;
        if (kind == 2) return p;
        if (kind == 3) return one - p;
        if (kind == 4) return p < (one >> 1) ? 0 : one;
        if (kind == 5) return random(seed, cycle);
        if (kind == 6) {
            unsigned quarter = p >> 30;
            return quarter == 0 ? uint64_t(p) * 4 : quarter == 1 ? one : quarter == 2 ? one * 3 - uint64_t(p) * 4 : 0;
        }
        uint64_t t2 = uint64_t(p) * p >> 32;
        uint64_t smooth = t2 + ((2 * t2 * (one - p)) >> 32);
        uint64_t a = random(seed, cycle), b = random(seed, cycle + 1);
        uint64_t delta = (a < b ? b - a : a - b) * smooth >> 32;
        return a < b ? a + delta : a - delta;
    }
private:
    uint64_t phase_ = 0, remainder_ = 0, previous_ = 0, previous_beat_ = 0;
    uint32_t restart_ = 0;
    static uint32_t random(uint32_t seed, uint32_t cycle) {
        uint32_t value = seed + cycle * 0x9e3779b9u;
        value ^= value >> 16; value *= 0x7feb352du;
        value ^= value >> 15; value *= 0x846ca68bu;
        return value ^ (value >> 16);
    }
};
} // namespace cntrl::midi
