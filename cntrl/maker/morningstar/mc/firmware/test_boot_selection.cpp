#include "boot_switch/selection.h"
#include <cassert>

int main() {
    uint32_t words[selection_records * 2];
    for (auto &word : words) word = UINT32_MAX;
    assert(inspect_boot_selection(words).choice == BootChoice::cntrl);
    assert(!can_enter_stock(inspect_boot_selection(words)));
    words[0] = selection_magic; words[1] = ~selection_magic;
    assert(inspect_boot_selection(words).choice == BootChoice::cntrl);
    assert(can_enter_stock(inspect_boot_selection(words)));
    uint32_t value = uint32_t(BootChoice::stock);
    for (unsigned bits = 0; bits <= 64; ++bits) {
        words[2] = words[3] = UINT32_MAX;
        for (unsigned bit = 0; bit < bits; ++bit) {
            uint32_t expected = bit < 32 ? value : ~value;
            words[2 + bit / 32] &= expected | ~(uint32_t(1) << (bit % 32));
        }
        auto state = inspect_boot_selection(words);
        bool complete = words[2] == value && words[3] == ~value;
        assert(state.choice == (complete ? BootChoice::stock : BootChoice::cntrl));
    }
    assert(inspect_boot_selection(words).next == 2);
    words[4] = uint32_t(BootChoice::cntrl); // A torn default change leaves stock selected.
    assert(inspect_boot_selection(words).choice == BootChoice::stock);
    words[6] = uint32_t(BootChoice::cntrl); words[7] = ~words[6];
    assert(inspect_boot_selection(words).choice == BootChoice::cntrl);
    words[8] = 0x12345678; words[9] = ~words[8];
    assert(inspect_boot_selection(words).choice == BootChoice::cntrl);
    // A patch visit requires room for its continuation records.
    words[1020] = value; words[1021] = ~value;
    assert(inspect_boot_selection(words).next == 511);
    assert(!can_enter_stock(inspect_boot_selection(words)));
    words[1022] = uint32_t(BootChoice::cntrl); words[1023] = ~words[1022];
    assert(!inspect_boot_selection(words).next);
    assert(!can_enter_stock(inspect_boot_selection(words)));
    words[1] ^= 1;
    assert(inspect_boot_selection(words).choice == BootChoice::cntrl);
    assert(!inspect_boot_selection(words).next);
    for (auto &word : words) word = UINT32_MAX;
    words[0] = selection_magic; words[1] = ~selection_magic;
    auto append = [&](uint32_t v) {
        auto state = inspect_boot_selection(words); assert(state.next);
        words[state.next * 2] = v; words[state.next * 2 + 1] = ~v;
    };
    append(visit_config | 18 | 0x8000); // B+E, visit 1.
    auto state = inspect_boot_selection(words);
    assert(state.pair == 18 && state.visit == 1 && !state.returned);
    // An interrupted return marker does not trigger automatic recall.
    words[4] = visit_return | 1;
    assert(!inspect_boot_selection(words).returned);
    words[5] = ~words[4];
    assert(inspect_boot_selection(words).returned == 1);
    append(visit_consumed | 1);
    assert(!inspect_boot_selection(words).returned);
    append(visit_config | default_exit_pair); // A visit outside a song has no continuation.
    assert(!inspect_boot_selection(words).visit && inspect_boot_selection(words).pair == 33);
    for (unsigned i = 1; i < selection_records - 3; ++i) { words[i * 2] = 0; words[i * 2 + 1] = ~0u; }
    state = inspect_boot_selection(words);
    assert(state.next == selection_records - 3 && can_enter_stock(state));
    append(0);
    assert(!can_enter_stock(inspect_boot_selection(words)));

    uint32_t request = request_stock;
    assert(select_boot(BootChoice::cntrl, request) == BootChoice::stock);
    assert(request == visiting_stock);
    assert(select_boot(BootChoice::cntrl, request) == BootChoice::cntrl && !request);
    request = request_cntrl;
    assert(select_boot(BootChoice::stock, request) == BootChoice::cntrl && !request);
    assert(select_boot(BootChoice::stock, request) == BootChoice::stock);
    request = 0x0bad00f1; // Preserve Teensy's crash reboot marker.
    assert(select_boot(BootChoice::stock, request) == BootChoice::stock && request == 0x0bad00f1);
}
