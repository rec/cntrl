#pragma once

#include <stdint.h>

constexpr uint8_t LOW = 0;
constexpr uint8_t HIGH = 1;
constexpr uint8_t OUTPUT = 1;
constexpr uint8_t INPUT = 0;

void digitalWrite(uint8_t pin, uint8_t value);
void digitalWriteFast(uint8_t pin, uint8_t value);
void pinMode(uint8_t pin, uint8_t mode);
void delay(unsigned long milliseconds);
void delayMicroseconds(unsigned int microseconds);
void analogWrite(uint8_t pin, int value);
