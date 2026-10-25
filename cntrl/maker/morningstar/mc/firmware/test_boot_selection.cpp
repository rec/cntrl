#include "boot_switch/selection.h"
#include <cassert>

int main() {
    uint32_t words[selection_records * 2];
    for (auto &word : words) word = UINT32_MAX;
    assert(inspect_boot_selection(words).choice == BootChoice::stock);
    assert(!inspect_boot_selection(words).next);
    words[0] = selection_magic;
    words[1] = ~selection_magic;
    assert(inspect_boot_selection(words).choice == BootChoice::stock);
    assert(inspect_boot_selection(words).next == 1);

    // Model a program interrupted after each prefix of the intended bits.
    uint32_t value = uint32_t(BootChoice::cntrl);
    for (unsigned bits = 0; bits <= 64; ++bits) {
        words[2] = words[3] = UINT32_MAX;
        for (unsigned bit = 0; bit < bits; ++bit) {
            uint32_t expected = bit < 32 ? value : ~value;
            words[2 + bit / 32] &= expected | ~(uint32_t(1) << (bit % 32));
        }
        auto state = inspect_boot_selection(words);
        bool complete = words[2] == value && words[3] == ~value;
        assert(state.choice == (complete ? BootChoice::cntrl : BootChoice::stock));
    }
    assert(inspect_boot_selection(words).next == 2);

    // A torn following record preserves the earlier committed selection.
    words[4] = uint32_t(BootChoice::stock);
    assert(inspect_boot_selection(words).choice == BootChoice::cntrl);
    assert(inspect_boot_selection(words).next == 3);
    words[6] = uint32_t(BootChoice::stock);
    words[7] = ~words[6];
    assert(inspect_boot_selection(words).choice == BootChoice::stock);

    // Unknown and corrupt records do not become boot choices.
    words[8] = 0x12345678;
    words[9] = ~words[8];
    assert(inspect_boot_selection(words).choice == BootChoice::stock);
    words[10] = value;
    words[11] = ~value ^ 1;
    assert(inspect_boot_selection(words).choice == BootChoice::stock);

    words[1022] = value;
    words[1023] = ~value;
    assert(inspect_boot_selection(words).choice == BootChoice::cntrl);
    assert(!inspect_boot_selection(words).next);
    words[1] ^= 1;
    assert(inspect_boot_selection(words).choice == BootChoice::stock);
    assert(!inspect_boot_selection(words).next);
}
