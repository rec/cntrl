#include "gesture.h"
#include "selection.h"

// The composer extends stock's cleared BSS and MPU boundary by 256 bytes.
SwitchGesture gesture;
bool blocked = false;

extern "C" unsigned boot_selected() {
    auto words = reinterpret_cast<const volatile uint32_t *>(0x60084000);
    return inspect_boot_selection(words).choice == BootChoice::cntrl;
}

extern "C" unsigned stock_reader(void *reader) {
    auto read = reinterpret_cast<unsigned (*)(void *)>(0x2649);
    unsigned raw = read(reader);
    uint32_t now = *reinterpret_cast<volatile uint32_t *>(0x200130d8);
    auto words = reinterpret_cast<const volatile uint32_t *>(0x60084000);
    BootSelection selection = inspect_boot_selection(words);
    unsigned pair = selection.pair;
    constexpr unsigned physical[] = {2, 1, 0, 3, 4, 5};
    unsigned first = 0, second = 0;
    for (unsigned i = 0; i < 6; ++i) if (pair & (1u << i)) {
        if (!first) first = 1u << physical[i]; else second = 1u << physical[i];
    }
    if (blocked) {
        if ((raw & (first | second)) == (first | second)) {
            blocked = false;
            gesture.cancel();
        }
        return raw | first | second;
    }
    uint8_t pressed = (!(raw & first) ? 1 : 0) | (!(raw & second) ? 2 : 0);
    GestureResult result = gesture.update(pressed, now);
    if (result.activate) {
        if (selection.choice != BootChoice::cntrl) {
            if (!selection.next || selection.next > selection_records - (selection.visit ? 2u : 1u)) { blocked = true; return raw | first | second; }
            uint32_t returned = visit_return | selection.visit;
            uint32_t record[] = {returned, ~returned, uint32_t(BootChoice::cntrl), ~uint32_t(BootChoice::cntrl)};
            auto destination = reinterpret_cast<void *>(0x60084000 + selection.next * 8);
            // This stock routine executes from ITCM and waits for flash ready
            // before returning. The source record is on the RAM stack.
            auto program = reinterpret_cast<void (*)(void *, const void *, uint32_t)>(0x4da91);
            program(destination, selection.visit ? record : record + 2, selection.visit ? sizeof(record) : 8);
            if (inspect_boot_selection(words).choice != BootChoice::cntrl) {
                blocked = true;
                return raw | first | second;
            }
        }
        // Disconnect USB before reset so the host discovers cntrl's descriptors.
        *reinterpret_cast<volatile uint32_t *>(0x402e0140) = 2;
        *reinterpret_cast<volatile uint32_t *>(0x400d9034) = 0x80000000;
        auto delay = reinterpret_cast<void (*)(uint32_t)>(0x4d735);
        delay(100);
        __asm__ volatile("cpsid i" ::: "memory");
        *reinterpret_cast<volatile uint32_t *>(0xe000ed0c) = 0x05fa0004;
        __asm__ volatile("dsb" ::: "memory");
        while (true) __asm__ volatile("wfi");
    }
    raw |= first | second;
    if (result.pressed & 1) raw &= ~first;
    if (result.pressed & 2) raw &= ~second;
    return raw;
}
