// SPDX-License-Identifier: GPL-2.0-or-later
#include "demo.h"

#include "i_video.h"
#include "lcd.h"
#include "fetch.h"
#include "r_strip.h"

byte *I_VideoBuffer;
uint16_t pump_lut[256];

static uint16_t frame[DEMO_WIDTH * DEMO_HEIGHT];
static int target_row[LCD_H];
static int window_y, window_bottom;
static int pixel_x, pixel_y, pixel_step;
static const uint8_t gamma5[32], gamma6[64];

void I_InitGraphics(void)
{
    for (int y = 0; y < LCD_H; ++y)
        target_row[y] = -1;
    for (int y = 0; y < DEMO_HEIGHT; ++y)
        target_row[(2 * y + 1) * LCD_H / (2 * DEMO_HEIGHT)] = y;
}
void I_StartFrame(void) {}
void I_UpdateNoBlit(void) {}
void I_FinishUpdate(void) { demo_present(frame); }
void I_SetPalette(int palette)
{
    R_PaletteChanged();
    pal_read(palette, pump_lut);
    for (unsigned i = 0; i < sizeof(pump_lut) / sizeof(*pump_lut); ++i) {
        const uint16_t color = pump_lut[i];
        pump_lut[i] = gamma5[color >> 11] << 11
                    | gamma6[(color >> 5) & 63] << 5
                    | gamma5[color & 31];
    }
}

void lcd_begin(int x0, int y0, int x1, int y1)
{
    (void)x1;
    pixel_x = x0;
    window_y = pixel_y = y0;
    window_bottom = y1;
    pixel_step = 1;
}

void lcd_push(uint16_t color)
{
    int row = target_row[pixel_y];
    if (row >= 0 && (pixel_step == 2 || !(pixel_x & 1)))
        frame[row * DEMO_WIDTH + pixel_x / 2] = color;
    if (++pixel_y > window_bottom) {
        pixel_y = window_y;
        pixel_x += pixel_step;
    }
}

void lcd_push_n(const uint16_t *pixels, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
        lcd_push(pixels[i]);
}
void lcd_end(void) {}

void lcd_fill(int x0, int y0, int x1, int y1, uint16_t color)
{
    lcd_begin(x0, y0, x1, y1);
    int count = (x1 - x0 + 1) * (y1 - y0 + 1);
    for (int i = 0; i < count; ++i)
        lcd_push(color);
    lcd_end();
}

void pump_begin(int x0, int x1, int y0, int y1, int flags)
{
    lcd_begin(x0, y0, x1, y1);
    pixel_step = flags & PUMP_DOUBLE ? 2 : 1;
}

void pump_submit(const uint8_t *columns, int count)
{
    int pixels = count * (window_bottom - window_y + 1);
    for (int i = 0; i < pixels; ++i)
        lcd_push(pump_lut[columns[i]]);
}

void pump_sync(int max) { (void)max; }
int pump_pending(void) { return 0; }
void pump_end(void) {}

// Gamma 1.25, rounded to the nearest RGB565 channel value:
// round(max * pow(value / max, 1 / 1.25)).
static const uint8_t gamma5[32] = {
    0, 2, 3, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 24, 25, 26, 27, 28, 29, 29, 30, 31,
};
static const uint8_t gamma6[64] = {
    0, 2, 4, 6, 7, 8, 10, 11, 12, 13, 14, 16, 17, 18, 19, 20,
    21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
    37, 38, 38, 39, 40, 41, 42, 43, 44, 45, 46, 46, 47, 48, 49, 50,
    51, 52, 52, 53, 54, 55, 56, 57, 57, 58, 59, 60, 61, 61, 62, 63,
};
