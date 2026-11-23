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
