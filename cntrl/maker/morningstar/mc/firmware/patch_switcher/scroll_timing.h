#pragma once
#include "panorama.h"

// Isolate scroll application with a single jump followed by a delayed RAM write.
class ScrollTimingProbe {
public:
    bool active() const { return stage_ != Stage::off; }
    void arm() {
        stage_ = Stage::loading;
        column_ = trial_ = 0;
        running_ = false;
    }
    void stop() { stage_ = Stage::off; }
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
            }
            write_pattern(column_ / 162, column_ % 162, 0);
            if (++column_ == 486) {
                scroll_display_panorama(0);
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
        if (stage_ == Stage::off || stage_ == Stage::waiting) {
            return;
        }
        const unsigned delay_index = trial_ / 8;
        const bool reverse = trial_ % 2;
        const unsigned marker_column = reverse ? 159 : 0;
        uint32_t hold = 1000000;
        if (stage_ == Stage::settling) {
            hold = 600000 + trial_ * 1999;
        } else if (stage_ == Stage::jumped) {
            hold = delays_[delay_index];
        } else if (stage_ == Stage::cleared) {
            hold = 100000;
        }
        if (uint32_t(now - since_) < hold) {
            return;
        }
        since_ = now;
        switch (stage_) {
        case Stage::settling:
            scroll_display_panorama(reverse ? 159 : 3);
            stage_ = Stage::jumped;
            break;
        case Stage::jumped:
            for (unsigned panel = 0; panel < 3; ++panel) {
                write_pattern(panel, marker_column, reverse ? 2 : 1);
            }
            stage_ = Stage::marked;
            break;
        case Stage::marked:
            for (unsigned panel = 0; panel < 3; ++panel) {
                write_pattern(panel, marker_column, 0);
            }
            stage_ = Stage::cleared;
            break;
        case Stage::cleared:
            if (++trial_ == 40) {
                trial_ = 0;
                running_ = false;
            }
            column_ = 0;
            stage_ = Stage::loading;
            break;
        default:
            break;
        }
    }
private:
    enum class Stage {
        off, loading, waiting, begin, settling, jumped, marked, cleared,
    };
    static void write_pattern(unsigned panel, unsigned x, unsigned marker) {
        uint8_t pixels[256];
        for (unsigned y = 0; y < 128; ++y) {
            const uint16_t pixel = (marker == 1 && y < 64) || (marker == 2 && y >= 64) ? 0xffff : 0;
            pixels[2 * y] = uint8_t(pixel >> 8);
            pixels[2 * y + 1] = uint8_t(pixel);
        }
        write_display_column(panel, x, pixels);
    }
    Stage stage_ = Stage::off;
    unsigned column_ = 0, trial_ = 0;
    uint32_t since_ = 0;
    bool running_ = false;
    static constexpr uint32_t delays_[] = {5000, 10000, 15000, 20000, 30000};
};
