#include "gesture.h"

// The composer extends stock's cleared BSS and MPU boundary by 256 bytes.
SwitchGesture gesture;

extern "C" unsigned stock_reader(void *reader) {
    auto read = reinterpret_cast<unsigned (*)(void *)>(0x2649);
    unsigned raw = read(reader);
    uint32_t now = *reinterpret_cast<volatile uint32_t *>(0x200130d8);
    constexpr unsigned first = 1u << CNTRL_SWITCH_FIRST_BIT;
    constexpr unsigned second = 1u << CNTRL_SWITCH_SECOND_BIT;
    uint8_t pressed = (!(raw & first) ? 1 : 0) | (!(raw & second) ? 2 : 0);
    GestureResult result = gesture.update(pressed, now);
    if (result.activate) {
        // Disconnect USB before reset so the host discovers cntrl's descriptors.
        *reinterpret_cast<volatile uint32_t *>(0x402e0140) = 2;
        *reinterpret_cast<volatile uint32_t *>(0x400d9034) = 0x80000000;
        auto delay = reinterpret_cast<void (*)(uint32_t)>(0x4d735);
        delay(100);
        __asm__ volatile("cpsid i" ::: "memory");
        // SRC_GPR5 also carries the core's distinct 0x0BAD00F1 crash marker.
        // Only our exact one-shot marker is consumed by boot_gate.
        *reinterpret_cast<volatile uint32_t *>(0x400f8030) = 0x4354524c;
        __asm__ volatile("dsb" ::: "memory");
        *reinterpret_cast<volatile uint32_t *>(0xe000ed0c) = 0x05fa0004;
        __asm__ volatile("dsb" ::: "memory");
        while (true) __asm__ volatile("wfi");
    }
    raw |= first | second;
    if (result.pressed & 1) raw &= ~first;
    if (result.pressed & 2) raw &= ~second;
    return raw;
}
