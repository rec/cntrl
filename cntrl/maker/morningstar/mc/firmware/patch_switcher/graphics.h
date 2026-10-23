#pragma once
#include <ST7735_t3.h>

// RAM-only use of the pinned library. Never initialize or flush its hardware.
class Graphics : public ST7735_t3 {
public:
    Graphics() : ST7735_t3(255, 255, 255) {
        _width = _screenWidth = 160;
        _height = _screenHeight = 128;
        setClipRect();
        setOrigin();
        disableScroll();
        setFrameBuffer(pixels_);
        useFrameBuffer(true); // Supplied buffer: no dynamic allocation.
        setFont();
        setTextWrap(false);
        setTextColor(0xffff, 0);
        clearChangedRange();
    }
private:
    alignas(4) uint16_t pixels_[160 * 128] = {};
};
