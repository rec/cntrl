#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>

// Host test double, not a font implementation. Real font drawing is ARM-built
// and checked on the pedal. Glyph rectangles exercise our incremental scheduler.
class ST7735_t3 {
public:
    ST7735_t3(uint8_t cs, uint8_t dc, uint8_t reset) {
        assert(cs == 255 && dc == 255 && reset == 255);
    }
    void setClipRect() {}
    void setOrigin() {}
    void disableScroll() {}
    void setFrameBuffer(uint16_t *pixels) { pixels_ = pixels; }
    void useFrameBuffer(bool enabled) { assert(enabled && pixels_); }
    uint16_t *getFrameBuffer() { return pixels_; }
    void setFont() {}
    void setTextWrap(bool enabled) { assert(!enabled); }
    void setTextColor(uint16_t color, uint16_t background) {
        color_ = color; background_ = background;
    }
    void clearChangedRange() {}
    void fillScreen(uint16_t color) {
        for (unsigned i = 0; i < unsigned(_width * _height); ++i) pixels_[i] = color;
    }
    void setTextSize(unsigned scale) { scale_ = scale; }
    void setCursor(unsigned x, unsigned y) { x_ = x; y_ = y; }
    void write(uint8_t character) {
        ++glyphs;
        for (unsigned y = 0; y < 8 * scale_ && y_ + y < unsigned(_height); ++y)
            for (unsigned x = 0; x < 6 * scale_ && x_ + x < unsigned(_width); ++x)
                pixels_[(y_ + y) * _width + x_ + x] =
                    character != ' ' && x < 5 * scale_ && y < 7 * scale_ ? color_ : background_;
    }
    inline static unsigned glyphs = 0;
protected:
    int16_t _width = 0, _height = 0, _screenWidth = 0, _screenHeight = 0;
private:
    uint16_t *pixels_ = nullptr;
    uint16_t color_ = 0xffff, background_ = 0;
    unsigned x_ = 0, y_ = 0, scale_ = 1;
};
