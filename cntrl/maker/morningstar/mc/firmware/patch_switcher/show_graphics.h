#pragma once
#include "graphics.h"
#include "bitmap_fonts.h"
#include <st7735_t3_font_Arial.h>
#include <st7735_t3_font_ComicSansMS.h>
#include <cntrl/hardware.h>

class ShowGraphics : public Graphics {
public:
    static const ILI9341_t3_font_t *native_font(unsigned face, unsigned size) {
        if (face >= 3) return bitmap_font(face, size);
        if (face != 1 && face != 2) return nullptr;
        switch (size) {
        case 8: return face == 1 ? &Arial_8 : &ComicSansMS_8;
        case 9: return face == 1 ? &Arial_9 : &ComicSansMS_9;
        case 10: return face == 1 ? &Arial_10 : &ComicSansMS_10;
        case 11: return face == 1 ? &Arial_11 : &ComicSansMS_11;
        case 12: return face == 1 ? &Arial_12 : &ComicSansMS_12;
        case 13: return face == 1 ? &Arial_13 : &ComicSansMS_13;
        case 14: return face == 1 ? &Arial_14 : &ComicSansMS_14;
        case 16: return face == 1 ? &Arial_16 : &ComicSansMS_16;
        case 18: return face == 1 ? &Arial_18 : &ComicSansMS_18;
        case 20: return face == 1 ? &Arial_20 : &ComicSansMS_20;
        case 24: return face == 1 ? &Arial_24 : &ComicSansMS_24;
        case 28: return face == 1 ? &Arial_28 : &ComicSansMS_28;
        case 32: return face == 1 ? &Arial_32 : &ComicSansMS_32;
        case 40: return face == 1 ? &Arial_40 : &ComicSansMS_40;
        case 48: return face == 1 ? &Arial_48 : &ComicSansMS_48;
        case 60: return face == 1 ? &Arial_60 : &ComicSansMS_60;
        case 72: return face == 1 ? &Arial_72 : &ComicSansMS_72;
        case 96: return face == 1 ? &Arial_96 : &ComicSansMS_96;
        default: return nullptr;
        }
    }

    static bool supports_font(unsigned face, unsigned size) {
        return face == 0 ? size >= 8 && size <= 96 && size % 8 == 0
                         : native_font(face, size) != nullptr;
    }

    void configure(const cntrl::ScreenGraphics &settings) {
        setClipRect();
        fillScreen(settings.background);
        setClipRect(settings.pad, settings.pad, 160 - 2 * settings.pad, 128 - 2 * settings.pad);
        select_font(settings.font, settings.size);
        setTextColor(settings.foreground, settings.background);
    }
    void select_font(unsigned face, unsigned size) {
        setTextSize(face == 0 ? size / 8 : 1);
        if (face == 0) setFont();
        else setFont(*native_font(face, size));
    }
};
