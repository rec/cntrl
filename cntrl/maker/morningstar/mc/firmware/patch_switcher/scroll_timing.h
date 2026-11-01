#pragma once
#include "panorama.h"

// Write markers into settled hidden columns before revealing them at each edge.
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
                scroll_display_panorama(origins_[trial_]);
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
        const uint32_t hold = stage_ == Stage::settling || stage_ == Stage::returned ? 1000000 : 2000000;
        if (uint32_t(now - since_) < hold) {
            return;
        }
        since_ = now;
        const unsigned origin = origins_[trial_];
        switch (stage_) {
        case Stage::settling:
            for (unsigned panel = 0; panel < 3; ++panel) {
                write_pattern(panel, (origin + 160) % 162, 1);
            }
            stage_ = Stage::first_hidden;
            break;
        case Stage::first_hidden:
            for (unsigned panel = 0; panel < 3; ++panel) {
                write_pattern(panel, (origin + 161) % 162, 2);
            }
            stage_ = Stage::second_hidden;
            break;
        case Stage::second_hidden:
            scroll_display_panorama((origin + 1) % 162);
            stage_ = Stage::first_right;
            break;
        case Stage::first_right:
            scroll_display_panorama((origin + 2) % 162);
            stage_ = Stage::second_right;
            break;
        case Stage::second_right:
            scroll_display_panorama(origin);
            stage_ = Stage::returned;
            break;
        case Stage::returned:
            scroll_display_panorama((origin + 161) % 162);
            stage_ = Stage::first_left;
            break;
        case Stage::first_left:
            scroll_display_panorama((origin + 160) % 162);
            stage_ = Stage::second_left;
            break;
        case Stage::second_left:
            if (++trial_ == 3) {
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
        off, loading, waiting, begin, settling, first_hidden, second_hidden,
        first_right, second_right, returned, first_left, second_left,
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
    static constexpr unsigned origins_[] = {0, 70, 161};
};
