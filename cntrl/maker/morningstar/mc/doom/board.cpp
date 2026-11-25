// SPDX-License-Identifier: GPL-2.0-or-later
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#include "demo.h"
#include "nor.h"
#include "asset_path.h"
#if defined(CNTRL_DOOM_RETURN)
#include "return_button.h"
#include "boot_switch/selection.h"
#endif

namespace {
struct Panel {
    uint8_t cs, dc, reset;
};
constexpr Panel LEFT{15, 34, 21};
constexpr Panel CENTER{24, 23, 22};
constexpr Panel RIGHT{27, 25, 20};
constexpr Panel PANELS[] = {LEFT, CENTER, RIGHT};
constexpr uint8_t BACKLIGHT = 6;
File assets;

struct CardRun {
    uint16_t count, color;
};
#include "control_cards.h"

#if defined(CNTRL_DOOM_RETURN)
[[noreturn]] void return_to_cntrl()
{
    USB1_USBCMD = 2;
    USBPHY1_CTRL_SET = 0x80000000;
    delay(100);
    __disable_irq();
    *reinterpret_cast<volatile uint32_t *>(boot_request_register) = request_cntrl;
    __asm__ volatile("dsb" ::: "memory");
    SCB_AIRCR = 0x05fa0004;
    __asm__ volatile("dsb" ::: "memory");
    while (true) __asm__ volatile("wfi");
}
#endif

void panel_command(const Panel &panel, uint8_t command, const uint8_t *args, unsigned count)
{
    digitalWriteFast(panel.cs, LOW);
    digitalWriteFast(panel.dc, LOW);
    SPI.transfer(command);
    digitalWriteFast(panel.dc, HIGH);
    for (unsigned i = 0; i < count; ++i)
        SPI.transfer(args[i]);
    digitalWriteFast(panel.cs, HIGH);
}

void initialize_panels()
{
    auto command_all = [](uint8_t command, const uint8_t *args = nullptr, unsigned count = 0) {
        for (const Panel &panel : PANELS)
            panel_command(panel, command, args, count);
    };
    for (const Panel &panel : PANELS) {
        digitalWriteFast(panel.cs, HIGH);
        digitalWriteFast(panel.reset, HIGH);
        pinMode(panel.cs, OUTPUT);
        pinMode(panel.reset, OUTPUT);
        pinMode(panel.dc, OUTPUT);
    }
    delay(100);
    for (const Panel &panel : PANELS)
        digitalWriteFast(panel.reset, LOW);
    delay(100);
    for (const Panel &panel : PANELS)
        digitalWriteFast(panel.reset, HIGH);
    delay(200);
    SPI.begin();
    SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    command_all(0x01);
    delay(150);
    command_all(0x11);
    delay(500);
    // ST7735 register values and panel offsets for the MC6 Pro.
    constexpr uint8_t settings[] = {
        0xb1, 3, 5, 0x3c, 0x3c,
        0xb2, 3, 5, 0x3c, 0x3c,
        0xb3, 6, 5, 0x3c, 0x3c, 5, 0x3c, 0x3c,
        0xb4, 1, 3,
        0xc0, 3, 0x28, 8, 4,
        0xc1, 1, 0xc0,
        0xc2, 2, 0x0d, 0,
        0xc3, 2, 0x8d, 0x2a,
        0xc4, 2, 0x8d, 0xee,
        0xc5, 1, 0x10,
        0x20, 0,
        0x3a, 1, 5,
        0xe0, 16, 4, 0x22, 7, 0x0a, 0x2e, 0x30, 0x25, 0x2a,
            0x28, 0x26, 0x2e, 0x3a, 0, 1, 3, 0x13,
        0xe1, 16, 4, 0x16, 6, 0x0d, 0x2d, 0x26, 0x23, 0x27,
            0x27, 0x25, 0x2d, 0x3b, 0, 1, 4, 0x13,
        0x36, 1, 0xa0,
    };
    for (unsigned i = 0; i < sizeof(settings);) {
        uint8_t command = settings[i++];
        uint8_t count = settings[i++];
        command_all(command, settings + i, count);
        i += count;
    }
    command_all(0x13);
    delay(10);
    command_all(0x29);
    SPI.endTransaction();
    delay(100);
}

void begin_frame(const Panel &panel)
{
    constexpr uint8_t cols[] = {0, 1, 0, DEMO_WIDTH};
    constexpr uint8_t rows[] = {0, 2, 0, DEMO_HEIGHT + 1};
    SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    panel_command(panel, 0x2a, cols, sizeof(cols));
    panel_command(panel, 0x2b, rows, sizeof(rows));
    digitalWriteFast(panel.cs, LOW);
    digitalWriteFast(panel.dc, LOW);
    SPI.transfer(0x2c);
    digitalWriteFast(panel.dc, HIGH);
}

void end_frame(const Panel &panel)
{
    digitalWriteFast(panel.cs, HIGH);
    SPI.endTransaction();
}

template <unsigned size>
void write_card(const Panel &panel, const CardRun (&card)[size])
{
    begin_frame(panel);
    for (const CardRun &run : card)
        for (unsigned i = 0; i < run.count; ++i)
            SPI.transfer16(run.color);
    end_frame(panel);
}

uint8_t read_switch_register()
{
    digitalWriteFast(13, LOW);
    pinMode(13, OUTPUT);
    pinMode(12, INPUT);
    digitalWriteFast(29, HIGH);
    digitalWriteFast(28, LOW);
    delayMicroseconds(3);
    digitalWriteFast(28, HIGH);
    delayMicroseconds(3);
    digitalWriteFast(29, LOW);
    uint8_t value = 0;
    for (int bit = 7; bit >= 0; --bit) {
        value |= digitalReadFast(12) << bit;
        digitalWriteFast(13, HIGH);
        delayMicroseconds(3);
        digitalWriteFast(13, LOW);
        delayMicroseconds(3);
    }
    return ~value & 0x3f;
}
}

void setup()
{
    analogWrite(BACKLIGHT, 0);
    digitalWriteFast(28, HIGH);
    digitalWriteFast(29, HIGH);
    pinMode(28, OUTPUT);
    pinMode(29, OUTPUT);
    Serial.begin(115200);
    initialize_panels();
    write_card(LEFT, left_card);
    write_card(RIGHT, right_card);
    if (!SD.begin(BUILTIN_SDCARD)) demo_fail("Could not mount the Doom microSD card");
    assets = SD.open(cntrl::doom_asset_path, FILE_READ);
    if (!assets || assets.size() != NOR_SIZE) demo_fail("Missing or invalid Doom asset image");
#ifdef CNTRL_DOOM_FLASH_CACHE
    if (!demo_verify_flash_cache()) demo_fail("Doom assets do not match the firmware cache");
#endif
    demo_run();
}

void loop() {}

int demo_read_assets(uint32_t offset, void *dest, uint32_t count)
{
    return assets.seek(offset) && assets.read(dest, count) == int(count);
}

uint32_t demo_millis() { return millis(); }
void demo_delay(uint32_t ms) { delay(ms); }

uint8_t demo_buttons()
{
    static uint32_t changed_at[6];
    static uint8_t candidate, stable;
    uint8_t sample = read_switch_register();
    uint32_t now = millis();
    for (unsigned bit = 0; bit < 6; ++bit) {
        unsigned mask = 1u << bit;
        if ((sample ^ candidate) & mask)
            changed_at[bit] = now;
        else if (((sample ^ stable) & mask) && now - changed_at[bit] >= 15)
            stable ^= mask;
    }
    candidate = sample;
#if defined(CNTRL_DOOM_RETURN)
    static ReturnButton return_button;
    switch (return_button.sample(stable & ReturnButton::mask, now)) {
    case ReturnButton::Action::restart: return_to_cntrl();
    case ReturnButton::Action::use: return stable | ReturnButton::mask;
    case ReturnButton::Action::none: return stable & ~ReturnButton::mask;
    }
#endif
    return stable;
}

void demo_present(const uint16_t *pixels)
{
    static bool first_frame = true;
    SPI.begin(); // Restore the clock and data pin mux after switch scans.
    begin_frame(CENTER);
    for (unsigned i = 0; i < DEMO_WIDTH * DEMO_HEIGHT; ++i)
        SPI.transfer16(pixels[i]);
    end_frame(CENTER);
    if (first_frame) {
        first_frame = false;
        analogWrite(BACKLIGHT, 192);
    }
}

void demo_fail(const char *message)
{
    while (true) {
        Serial.println(message);
        for (unsigned i = 0; i < 100; ++i) {
            demo_buttons(); // Keep the hold-to-return switch working after an SD error.
            delay(10);
        }
    }
}
