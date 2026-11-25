#pragma once
#include <cstdint>

enum class BootChoice : uint32_t { stock = 0x53544b31, cntrl = 0x43545231, doom = 0x444f4f4d };

// Only our markers are consumed. Teensy's crash reporting also uses this
// reset-retained register, with a different value.
constexpr uintptr_t boot_request_register = 0x400f8030;
constexpr uint32_t request_stock = 0x434e5701, request_cntrl = 0x434e5702, visiting_stock = 0x434e5703;
constexpr uint32_t request_doom = 0x434e5704;
constexpr uintptr_t doom_origin = 0x60780000, application_end = 0x607c0000;
inline BootChoice select_boot(BootChoice default_firmware, volatile uint32_t &request) {
    const uint32_t marker = request;
    if (marker == request_doom) {
        request = 0;
        return BootChoice::doom;
    }
    if (marker == request_stock) {
        request = visiting_stock;
        return BootChoice::stock;
    }
    if (marker == request_cntrl || marker == visiting_stock) request = 0;
    return marker == request_cntrl ? BootChoice::cntrl : default_firmware;
}

inline uint32_t doom_startup(const volatile uint32_t *ivt) {
    const uint32_t reset = ivt[1];
    return ivt[0] == 0x432000d1 && ivt[5] == doom_origin + 0x1000 &&
        (reset & 1) && reset >= doom_origin + 0x1401 && reset < application_end ? reset : 0;
}

constexpr unsigned default_exit_pair = 33; // A+F

struct BootSelection {
    BootChoice choice = BootChoice::cntrl;
    unsigned next = 0, pair = default_exit_pair, visit = 0, returned = 0;
};

constexpr uint32_t selection_magic = 0x434e4254;
constexpr unsigned selection_records = 512;
constexpr uint32_t visit_config = 0x46430000, visit_return = 0x46520000, visit_consumed = 0x46580000;
inline bool valid_exit_pair(unsigned mask) {
    unsigned rest = mask & (mask - 1);
    return mask < 64 && rest && !(rest & (rest - 1));
}

inline BootSelection inspect_boot_selection(const volatile uint32_t *words) {
    BootSelection result;
    if (words[0] != selection_magic || words[1] != ~selection_magic) return result;
    result.next = 1;
    for (unsigned i = 1; i < selection_records; ++i) {
        uint32_t value = words[i * 2], check = words[i * 2 + 1];
        if (value != UINT32_MAX || check != UINT32_MAX) result.next = i + 1;
        if (check == ~value) {
            if ((value & 0xffff0000) == visit_config && valid_exit_pair(value & 63)) {
                result.pair = value & 63; result.visit = value & 0x8000 ? i : 0; result.returned = 0;
            }
            if ((value & 0xffff0000) == visit_return && (value & 0xffff) == result.visit) result.returned = result.visit;
            if ((value & 0xffff0000) == visit_consumed && (value & 0xffff) == result.visit) result.returned = result.visit = 0;
        }
        if (check == ~value && (value == uint32_t(BootChoice::stock)
            || value == uint32_t(BootChoice::cntrl))) result.choice = BootChoice(value);
    }
    if (result.next == selection_records) result.next = 0;
    return result;
}

// Reserve config, return and consumption records for a temporary stock visit.
inline bool can_enter_stock(const BootSelection &selection) {
    return selection.next && selection.next <= selection_records - 3;
}
