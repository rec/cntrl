#include "usb_probe/displays.h"
#include <Arduino.h>
#include <SPI.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>
#ifdef CNTRL_SCROLL_PROBE
#include "scroll_probe/io.h"
#endif
#ifdef CNTRL_PANORAMA
#include "patch_switcher/panorama.h"
#endif

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
SPIClass SPI;
static bool spi_started = false, spi_active = false;
static bool clock_gpio = true, data_gpio = true;
static unsigned transactions = 0;
static std::vector<unsigned> selections;

void SPIClass::begin() {
    assert(!spi_active);
    for (uint8_t pin : chip_selects) assert(levels[pin]);
    assert(!levels[13]);
    spi_started = true;
    clock_gpio = data_gpio = false;
}

void SPIClass::beginTransaction(SPISettings settings) {
    assert(spi_started && !spi_active);
    assert(settings.order == MSBFIRST && settings.mode == SPI_MODE0);
    assert(settings.clock == 8000000);
    spi_active = true;
    ++transactions;
}

uint8_t SPIClass::transfer(uint8_t value) {
    assert(spi_active && !clock_gpio && !data_gpio);
    for (int bit = 7; bit >= 0; --bit) {
        digitalWrite(11, (value >> bit) & 1);
        digitalWrite(13, HIGH);
        digitalWrite(13, LOW);
    }
    return 0;
}

void SPIClass::endTransaction() {
    assert(spi_active && !levels[13] && bits == 0);
    for (uint8_t pin : chip_selects) assert(levels[pin]);
    spi_active = false;
}

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
                selections.push_back(i);
            }
        }
    }
    if (pin == 13 && value && !levels[13]) {
        unsigned selected = 0;
        for (unsigned i = 0; i < 3; ++i) {
            if (!levels[chip_selects[i]]) selected |= 1u << i;
        }
        assert(selected);
        byte = uint8_t((byte << 1) | levels[11]);
        if (++bits == 8) {
            for (unsigned i = 0; i < 3; ++i) if (selected & (1u << i)) {
                transfers[i].back().bytes.push_back(byte);
                transfers[i].back().data.push_back(levels[data_commands[i]]);
            }
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
    if (pin == 12 || pin == 13) {
        assert(!spi_active && !levels[13]);
        for (uint8_t selected : chip_selects) assert(levels[selected]);
        assert(mode == (pin == 12 ? INPUT : OUTPUT));
        if (pin == 12) data_gpio = true;
        else clock_gpio = true;
        return;
    }
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
    assert(transactions == 1 && clock_gpio && data_gpio && !spi_active);
    assert((brightness == std::vector<int>{0, 192}));
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
    const uint8_t segment[] = {0xff, 0xff, 0, 0};
    unsigned before = unsigned(transfers[0].size());
    write_display_segment(0, 10, 20, segment, 2);
    assert(transactions == 2 && clock_gpio && data_gpio && !spi_active);
    assert(transfers[0].size() == before + 3);
    assert((transfers[0][before].bytes == std::vector<uint8_t>{0x2a, 0, 11, 0, 12}));
    assert((transfers[0][before + 1].bytes == std::vector<uint8_t>{0x2b, 0, 22, 0, 22}));
    assert((transfers[0][before + 2].bytes == std::vector<uint8_t>{0x2c, 0xff, 0xff, 0, 0}));
    assert(!levels[13] && levels[chip_selects[0]]);
    write_display_segment(0, 159, 0, segment, 2);
    write_display_segment(3, 0, 0, segment, 2);
    assert(transfers[0].size() == before + 3);
    assert(transactions == 2); // Invalid requests must not take over the clock.
    unsigned left = unsigned(transfers[0].size()), centre = unsigned(transfers[1].size());
    unsigned right = unsigned(transfers[2].size());
    write_display_group(0b101, 0, 127, segment, 2);
    assert(transactions == 3 && clock_gpio && data_gpio && !spi_active);
    assert(transfers[0].size() == left + 3 && transfers[2].size() == right + 3);
    assert(transfers[1].size() == centre);
    for (unsigned i = 0; i < 3; ++i) {
        assert(transfers[0][left + i].bytes == transfers[2][right + i].bytes);
        assert(transfers[0][left + i].data == transfers[2][right + i].data);
    }
    write_display_group(0, 0, 0, segment, 2);
    write_display_group(8, 0, 0, segment, 2);
    assert(transactions == 3);

    brightness.clear();
    initialize_displays(false);
    assert((brightness == std::vector<int>{0})); // Application must reveal its own image.
#ifdef CNTRL_SCROLL_PROBE
    for (auto &panel : transfers) panel.clear();
    initialize_scroll();
    for (const auto &panel : transfers) {
        assert(panel.size() == 3);
        assert((panel[0].bytes == std::vector<uint8_t>{0x36, 0x88}));
        assert((panel[1].bytes == std::vector<uint8_t>{0x33, 0, 1, 0, 160, 0, 1}));
        assert((panel[2].bytes == std::vector<uint8_t>{0x37, 0, 1}));
    }
    selections.clear();
    scroll_panels(1, 1, false);
    assert((selections == std::vector<unsigned>{0}));
    assert((transfers[0].back().bytes == std::vector<uint8_t>{0x37, 0, 160}));
    assert(transfers[1].size() == 3 && transfers[2].size() == 3);
    selections.clear();
    scroll_panels(7, 160, false);
    assert((selections == std::vector<unsigned>{0, 1, 2}));
    for (const auto &panel : transfers)
        assert((panel.back().bytes == std::vector<uint8_t>{0x37, 0, 1}));
    selections.clear();
    scroll_panels(7, 1, true);
    assert((selections == std::vector<unsigned>{2, 1, 0}));
    for (const auto &panel : transfers)
        assert((panel.back().bytes == std::vector<uint8_t>{0x37, 0, 160}));
    selections.clear();
    scroll_panels(5, 1, true);
    assert((selections == std::vector<unsigned>{2, 0}));
    uint8_t column[256] = {};
    write_scroll_columns(2, 159, column, 1);
    auto &panel = transfers[2];
    assert((panel[panel.size() - 3].bytes == std::vector<uint8_t>{0x2a, 0, 2, 0, 129}));
    assert((panel[panel.size() - 2].bytes == std::vector<uint8_t>{0x2b, 0, 160, 0, 160}));
    assert(panel.back().bytes.size() == 257 && panel.back().bytes[0] == 0x2c);
    assert(clock_gpio && data_gpio && !spi_active && !levels[13]);
    before = transactions;
    write_scroll_columns(3, 0, column, 1); write_scroll_columns(0, 160, column, 1);
    write_scroll_columns(0, 159, column, 2); write_scroll_columns(0, 0, column, 0);
    scroll_panels(0, 0, false); scroll_panels(8, 0, true);
    assert(transactions == before);
    uint8_t stripe[32 * 256];
    for (unsigned i = 0; i < sizeof(stripe); ++i) stripe[i] = uint8_t(i);
    unsigned previous = transfers[1].size();
    before = transactions;
    write_scroll_columns(1, 128, stripe, 32);
    const auto &batch = transfers[1];
    assert(transactions == before + 32 && batch.size() == previous + 32 * 3);
    for (unsigned x = 0; x < 32; ++x) {
        unsigned first = previous + x * 3;
        assert((batch[first].bytes == std::vector<uint8_t>{0x2a, 0, 2, 0, 129}));
        assert((batch[first + 1].bytes == std::vector<uint8_t>{0x2b, 0, uint8_t(129 + x), 0, uint8_t(129 + x)}));
        assert(batch[first + 2].bytes.size() == 257 && batch[first + 2].bytes[0] == 0x2c);
        for (unsigned i = 0; i < 256; ++i)
            assert(batch[first + 2].bytes[i + 1] == stripe[x * 256 + i]);
    }
    assert(clock_gpio && data_gpio && !spi_active && !levels[13]);
#endif
#ifdef CNTRL_PANORAMA
    for (auto &panel : transfers) panel.clear();
    initialize_display_panorama();
    for (const auto &panel : transfers) {
        assert((panel[0].bytes == std::vector<uint8_t>{0x36, 0x88}));
        assert((panel[1].bytes == std::vector<uint8_t>{0x33, 0, 0, 0, 162, 0, 0}));
        assert((panel[2].bytes == std::vector<uint8_t>{0x37, 0, 0}));
    }
    uint8_t column[256];
    for (unsigned i = 0; i < 256; ++i) column[i] = uint8_t(i);
    write_display_column(2, 159, column);
    assert((transfers[2][4].bytes == std::vector<uint8_t>{0x2b, 0, 160, 0, 160}));
    for (unsigned i = 0; i < 256; ++i) assert(transfers[2].back().bytes[i + 1] == column[i]);
    scroll_display_panorama(72);
    for (const auto &panel : transfers)
        assert((panel.back().bytes == std::vector<uint8_t>{0x37, 0, 90}));
    write_display_column(0, 160, column);
    assert((transfers[0][transfers[0].size() - 2].bytes == std::vector<uint8_t>{0x2b, 0, 161, 0, 161}));
    write_display_column(0, 161, column);
    assert((transfers[0][transfers[0].size() - 2].bytes == std::vector<uint8_t>{0x2b, 0, 0, 0, 0}));
    scroll_display_panorama(1);
    for (const auto &panel : transfers)
        assert((panel.back().bytes == std::vector<uint8_t>{0x37, 0, 161}));
    // Splash output must restore landscape addressing and remove scroll offset.
    write_display_group(7, 0, 0, column, 1);
    for (const auto &panel : transfers) {
        unsigned n = panel.size();
        assert((panel[n - 6].bytes == std::vector<uint8_t>{0x33, 0, 1, 0, 160, 0, 1}));
        assert((panel[n - 5].bytes == std::vector<uint8_t>{0x37, 0, 1}));
        assert((panel[n - 4].bytes == std::vector<uint8_t>{0x36, 0xa8}));
    }
    assert(clock_gpio && data_gpio && !spi_active);
#endif
}
