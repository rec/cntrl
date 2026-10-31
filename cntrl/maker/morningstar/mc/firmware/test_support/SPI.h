#pragma once
#include <stdint.h>

constexpr uint8_t MSBFIRST = 1;
constexpr uint8_t SPI_MODE0 = 0;

struct SPISettings {
    uint32_t clock;
    uint8_t order, mode;
    SPISettings(uint32_t frequency, uint8_t bit_order, uint8_t spi_mode)
        : clock(frequency), order(bit_order), mode(spi_mode) {}
};

struct SPIClass {
    void begin();
    void beginTransaction(SPISettings settings);
    uint8_t transfer(uint8_t value);
    void endTransaction();
};

extern SPIClass SPI;
