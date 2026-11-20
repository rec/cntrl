#pragma once
#ifdef CNTRL_DIAGNOSTICS
#include "panorama.h"

// Compare conservative two-column updates at stock and faster panel refresh.
class ScrollTimingProbe {
public:
    bool active() const { return stage_ != Stage::off; }
    void arm() {
        stage_ = Stage::loading;
        column_ = trial_ = 0;
        running_ = false;
    }
    void stop() {
        if (active()) {
            set_display_fast_refresh(false);
        }
        stage_ = Stage::off;
    }
    void start() {
        if (stage_ == Stage::waiting) {
            running_ = true;
            stage_ = Stage::begin;
        }
    }
    void poll(uint32_t now) {
        if (stage_ == Stage::loading) {
            if (column_ == 0) {
                initialize_display_panorama();
                set_display_fast_refresh(trial_ >= 2);
                ring_ = progress_ = staged_ = 0;
            }
            write_pattern(column_ / 162, column_ % 162, false);
            if (++column_ == 486) {
                since_ = now;
                stage_ = running_ ? Stage::settling : Stage::waiting;
            }
            return;
        }
        if (stage_ == Stage::begin) {
            since_ = now;
            stage_ = Stage::settling;
            return;
        }
        if (stage_ == Stage::anchor) {
            // This poll is after the blocking SSA transfers have returned.
            // Starting the quiet interval here also handles a delayed poll.
            since_ = now;
            stage_ = progress_ == 232 ? Stage::holding : Stage::quiet;
            return;
        }
        if (stage_ == Stage::holding) {
            if (uint32_t(now - since_) >= 500000) {
                if (++trial_ == 6) {
                    trial_ = 0;
                    running_ = false;
                }
                column_ = 0;
                stage_ = Stage::loading;
            }
            return;
        }
        if (stage_ == Stage::settling || stage_ == Stage::quiet) {
            const uint32_t wait = stage_ == Stage::settling ? 400000 : (trial_ < 2 ? 20000 : 10000);
            if (uint32_t(now - since_) < wait) {
                return;
            }
            stage_ = Stage::writing;
        }
        if (stage_ != Stage::writing) {
            return;
        }
        const bool reverse = trial_ % 2;
        const unsigned column = (ring_ + (reverse ? 161 - staged_ : 160 + staged_)) % 162;
        for (unsigned panel = 0; panel < 3; ++panel) {
            write_pattern(panel, column, true);
        }
        if (++staged_ == 2) {
            ring_ = (ring_ + (reverse ? 160 : 2)) % 162;
            scroll_display_panorama(ring_);
            staged_ = 0;
            progress_ += 2;
            stage_ = Stage::anchor;
        }
    }
private:
    enum class Stage { off, loading, waiting, begin, settling, writing, anchor, quiet, holding };
    static void write_pattern(unsigned panel, unsigned x, bool marker) {
        uint8_t pixels[256];
        for (unsigned y = 0; y < 128; ++y) {
            const uint16_t pixel = marker ? (y % 16 < 8 ? 0xffff : 0) : (y % 16 >= 8 ? 0x7bef : 0);
            pixels[2 * y] = uint8_t(pixel >> 8);
            pixels[2 * y + 1] = uint8_t(pixel);
        }
        write_display_column(panel, x, pixels);
    }
    Stage stage_ = Stage::off;
    unsigned column_ = 0, trial_ = 0, ring_ = 0, progress_ = 0, staged_ = 0;
    uint32_t since_ = 0;
    bool running_ = false;
};
#endif
