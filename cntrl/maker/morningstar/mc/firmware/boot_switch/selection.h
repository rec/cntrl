#pragma once
#include <cstdint>

enum class BootChoice : uint32_t { stock = 0x53544b31, cntrl = 0x43545231 };

struct BootSelection {
    BootChoice choice = BootChoice::cntrl;
    unsigned next = 0;
};

constexpr uint32_t selection_magic = 0x434e4254;
constexpr unsigned selection_records = 512;

inline BootSelection inspect_boot_selection(const volatile uint32_t *words) {
    BootSelection result;
    if (words[0] != selection_magic || words[1] != ~selection_magic) return result;
    result.next = 1;
    for (unsigned i = 1; i < selection_records; ++i) {
        uint32_t value = words[i * 2], check = words[i * 2 + 1];
        if (value != UINT32_MAX || check != UINT32_MAX) result.next = i + 1;
        if (check == ~value && (value == uint32_t(BootChoice::stock)
            || value == uint32_t(BootChoice::cntrl))) result.choice = BootChoice(value);
    }
    if (result.next == selection_records) result.next = 0;
    return result;
}

// Reserve the following record for A+F to return from stock without erasing flash.
inline bool can_enter_stock(const BootSelection &selection) {
    return selection.next && selection.next < selection_records - 1;
}
