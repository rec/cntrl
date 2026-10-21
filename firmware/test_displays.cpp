#include "usb_probe/displays.h"
#include <Arduino.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

struct Transfer {
    std::vector<uint8_t> bytes;
    std::vector<bool> data;
};

static bool levels[55] = {};
static const uint8_t chip_selects[] = {15, 24, 27};
static const uint8_t data_commands[] = {34, 23, 25};
static const uint8_t resets[] = {21, 22, 20};
static std::vector<Transfer> transfers[3];
static std::vector<bool> reset_levels[3];
static std::vector<int> brightness;
static std::vector<unsigned long> waits;
static unsigned bits = 0;
static uint8_t byte = 0;

void digitalWrite(uint8_t pin, uint8_t value) {
    // Only known display signals may be driven by this implementation.
    assert(pin == 11 || pin == 13 || pin == 15 || pin == 24 || pin == 27
        || pin == 34 || pin == 23 || pin == 25
        || pin == 21 || pin == 22 || pin == 20);
    for (unsigned i = 0; i < 3; ++i) {
        if (pin == resets[i]) {
            reset_levels[i].push_back(value);
        }
        if (pin == chip_selects[i] && levels[pin] != bool(value)) {
            assert(!levels[13]); // Clock must idle LOW at transaction boundaries.
            assert(bits == 0);
            if (!value) {
                transfers[i].emplace_back();
            }
        }
    }
    if (pin == 13 && value && !levels[13]) {
        int selected = -1;
        for (unsigned i = 0; i < 3; ++i) {
            if (!levels[chip_selects[i]]) {
                assert(selected == -1); // Never address two displays together.
                selected = i;
            }
        }
        assert(selected >= 0);
        byte = uint8_t((byte << 1) | levels[11]);
        if (++bits == 8) {
            transfers[selected].back().bytes.push_back(byte);
            transfers[selected].back().data.push_back(levels[data_commands[selected]]);
            bits = 0;
            byte = 0;
        }
    }
    levels[pin] = value;
}

void digitalWriteFast(uint8_t pin, uint8_t value) {
    digitalWrite(pin, value);
}

void pinMode(uint8_t pin, uint8_t mode) {
    assert(mode == OUTPUT);
    if (pin != 11) {
        assert(levels[pin]); // D/C and reset latches preloaded before enable.
    }
}

void delay(unsigned long milliseconds) {
    waits.push_back(milliseconds);
}

void delayMicroseconds(unsigned int microseconds) {
    assert(microseconds == 1);
}

void analogWrite(uint8_t pin, int value) {
    assert(pin == 6);
    brightness.push_back(value);
}

int main() {
    // Mirrors the sketch's already-deselected panels and LOW clock.
    for (uint8_t pin : chip_selects) {
        levels[pin] = true;
    }
    initialize_displays();
    assert((brightness == std::vector<int>{5, 63}));
    assert((waits == std::vector<unsigned long>{100, 100, 200,
        150, 500, 10, 100, 150, 500, 10, 100, 150, 500, 10, 100}));
    const uint8_t commands[] = {0x01, 0x11, 0xb1, 0xb2, 0xb3, 0xb4,
        0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0x20, 0x3a,
        0xe0, 0xe1, 0x36, 0x13, 0x2a, 0x2b, 0x2c, 0x29};
    std::array<std::vector<uint8_t>, 3> images;
    for (unsigned i = 0; i < 3; ++i) {
        assert(levels[chip_selects[i]]);
        assert((reset_levels[i] == std::vector<bool>{true, false, true}));
        assert(transfers[i].size() == sizeof(commands));
        for (unsigned j = 0; j < sizeof(commands); ++j) {
            const Transfer &transfer = transfers[i][j];
            assert(transfer.bytes[0] == commands[j]);
            assert(!transfer.data[0]);
            for (unsigned k = 1; k < transfer.data.size(); ++k) {
                assert(transfer.data[k]);
            }
        }
        assert((transfers[i][16].bytes == std::vector<uint8_t>{0x36, 0xa8}));
        assert((transfers[i][18].bytes == std::vector<uint8_t>{0x2a, 0, 1, 0, 160}));
        assert((transfers[i][19].bytes == std::vector<uint8_t>{0x2b, 0, 2, 0, 129}));
        const auto &pixels = transfers[i][20].bytes;
        assert(pixels.size() == 1 + 160 * 128 * 2);
        unsigned white = 0;
        for (unsigned j = 1; j < pixels.size(); j += 2) {
            assert(pixels[j] == pixels[j + 1]);
            assert(pixels[j] == 0 || pixels[j] == 255);
            white += pixels[j] == 255;
        }
        assert(white > 500 && white < 2000);
        assert(pixels[1] == 0); // Background is cleared, not stale panel memory.
        assert(pixels[1 + 2 * (32 * 160 + 52)] == 255); // Top row of C.
        assert(pixels[1 + 2 * (32 * 160 + 50)] == 0);
        images[i] = pixels;
    }
    assert(images[0] != images[1] && images[1] != images[2] && images[0] != images[2]);
    assert(!levels[13]); // Ready for the next switch-reader load/clock cycle.
}
