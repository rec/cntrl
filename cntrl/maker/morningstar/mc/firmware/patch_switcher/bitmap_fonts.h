#pragma once
#include <ST7735_t3.h>
extern const ILI9341_t3_font_t Circuit_12;
extern const ILI9341_t3_font_t Circuit_16;
extern const ILI9341_t3_font_t Circuit_20;
extern const ILI9341_t3_font_t Circuit_24;
extern const ILI9341_t3_font_t Circuit_28;
extern const ILI9341_t3_font_t Circuit_32;
extern const ILI9341_t3_font_t Circuit_40;
extern const ILI9341_t3_font_t Circuit_48;
extern const ILI9341_t3_font_t CircuitBold_12;
extern const ILI9341_t3_font_t CircuitBold_16;
extern const ILI9341_t3_font_t CircuitBold_20;
extern const ILI9341_t3_font_t CircuitBold_24;
extern const ILI9341_t3_font_t CircuitBold_28;
extern const ILI9341_t3_font_t CircuitBold_32;
extern const ILI9341_t3_font_t CircuitBold_40;
extern const ILI9341_t3_font_t CircuitBold_48;
extern const ILI9341_t3_font_t StillTime_12;
extern const ILI9341_t3_font_t StillTime_16;
extern const ILI9341_t3_font_t StillTime_20;
extern const ILI9341_t3_font_t StillTime_24;
extern const ILI9341_t3_font_t StillTime_28;
extern const ILI9341_t3_font_t StillTime_32;
extern const ILI9341_t3_font_t StillTime_40;
extern const ILI9341_t3_font_t StillTime_48;
extern const ILI9341_t3_font_t MetalLord_12;
extern const ILI9341_t3_font_t MetalLord_16;
extern const ILI9341_t3_font_t MetalLord_20;
extern const ILI9341_t3_font_t MetalLord_24;
extern const ILI9341_t3_font_t MetalLord_28;
extern const ILI9341_t3_font_t MetalLord_32;
extern const ILI9341_t3_font_t MetalLord_40;
extern const ILI9341_t3_font_t MetalLord_48;
inline const ILI9341_t3_font_t *bitmap_font(unsigned face, unsigned size) {
    switch (face) {
    case 3: switch (size) {
        case 12: return &Circuit_12;
        case 16: return &Circuit_16;
        case 20: return &Circuit_20;
        case 24: return &Circuit_24;
        case 28: return &Circuit_28;
        case 32: return &Circuit_32;
        case 40: return &Circuit_40;
        case 48: return &Circuit_48;
        default: return nullptr;
    }
    case 4: switch (size) {
        case 12: return &CircuitBold_12;
        case 16: return &CircuitBold_16;
        case 20: return &CircuitBold_20;
        case 24: return &CircuitBold_24;
        case 28: return &CircuitBold_28;
        case 32: return &CircuitBold_32;
        case 40: return &CircuitBold_40;
        case 48: return &CircuitBold_48;
        default: return nullptr;
    }
    case 5: switch (size) {
        case 12: return &StillTime_12;
        case 16: return &StillTime_16;
        case 20: return &StillTime_20;
        case 24: return &StillTime_24;
        case 28: return &StillTime_28;
        case 32: return &StillTime_32;
        case 40: return &StillTime_40;
        case 48: return &StillTime_48;
        default: return nullptr;
    }
    case 6: switch (size) {
        case 12: return &MetalLord_12;
        case 16: return &MetalLord_16;
        case 20: return &MetalLord_20;
        case 24: return &MetalLord_24;
        case 28: return &MetalLord_28;
        case 32: return &MetalLord_32;
        case 40: return &MetalLord_40;
        case 48: return &MetalLord_48;
        default: return nullptr;
    }
    default: return nullptr;
    }
}
