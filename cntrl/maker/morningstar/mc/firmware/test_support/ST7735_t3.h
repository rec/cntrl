#pragma once
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ILI9341_fonts.h>

// Host test double, not a font implementation. Real font drawing is ARM-built
// and checked on the pedal. Glyph rectangles exercise our incremental scheduler.
class ST7735_t3 {
public:
    ST7735_t3(uint8_t cs, uint8_t dc, uint8_t reset) {
        assert(cs == 255 && dc == 255 && reset == 255);
    }
    void setClipRect() { left_ = top_ = 0; right_ = _width; bottom_ = _height; }
    void setClipRect(unsigned x, unsigned y, unsigned width, unsigned height) {
        left_ = x; top_ = y; right_ = x + width; bottom_ = y + height;
    }
    void setOrigin() {}
    void disableScroll() {}
    void setFrameBuffer(uint16_t *pixels) { pixels_ = pixels; }
    void useFrameBuffer(bool enabled) { assert(enabled && pixels_); }
    uint16_t *getFrameBuffer() { return pixels_; }
    void setFont() { font_scale_ = 1; }
    void setFont(const ILI9341_t3_font_t &font) { font_scale_ = std::max(1u, unsigned(font.cap_height) / 8); }
    void setTextWrap(bool enabled) { assert(!enabled); }
    void setTextColor(uint16_t color) { color_ = background_ = color; }
    void setTextColor(uint16_t color, uint16_t background) {
        color_ = color; background_ = background;
    }
    void clearChangedRange() {}
    void fillScreen(uint16_t color) {
        for (unsigned i = 0; i < unsigned(_width * _height); ++i) pixels_[i] = color;
    }
    void setTextSize(unsigned scale) { assert(scale == 1); }
    void setCursor(unsigned x, unsigned y) { x_ = x; y_ = y; }
    void getTextBounds(const char *text, int16_t x, int16_t y, int16_t *left, int16_t *top, uint16_t *width, uint16_t *height) {
        *left = x; *top = y; *width = std::strlen(text) * 6 * font_scale_; *height = 8 * font_scale_;
    }
    void write(uint8_t character) {
        unsigned scale_ = font_scale_; // Simulated glyph dimensions, not pixel enlargement.
        ++glyphs;
        for (unsigned y = 0; y < 8 * scale_ && y_ + y < unsigned(_height); ++y)
            for (unsigned x = 0; x < 6 * scale_ && x_ + x < unsigned(_width); ++x)
                if (x_ + x >= left_ && x_ + x < right_ && y_ + y >= top_ && y_ + y < bottom_) {
                    if (character != ' ' && x < 5 * scale_ && y < 7 * scale_)
                        pixels_[(y_ + y) * _width + x_ + x] = color_;
                    else if (background_ != color_)
                        pixels_[(y_ + y) * _width + x_ + x] = background_;
                }
        x_ += 6 * scale_;
    }
    inline static unsigned glyphs = 0;
protected:
    int16_t _width = 0, _height = 0, _screenWidth = 0, _screenHeight = 0;
private:
    uint16_t *pixels_ = nullptr;
    uint16_t color_ = 0xffff, background_ = 0;
    unsigned x_ = 0, y_ = 0, font_scale_ = 1;
    unsigned left_ = 0, top_ = 0, right_ = 160, bottom_ = 128;
};
