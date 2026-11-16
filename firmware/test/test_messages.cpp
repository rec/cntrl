#include <cntrl/ui/messages.h>
#include <cassert>
#include <cstring>

namespace i18n = cntrl::ui::i18n;

const char *translated(i18n::Message message, bool short_form) {
    if (short_form) return nullptr;
    if (message == i18n::Message::tempo_slot) return "{tempo:u} BPM, mémoire {slot:u}";
    return nullptr;
}

int main() {
    char text[64];
    assert(i18n::format(text, sizeof(text), i18n::Message::tempo_slot, 2u, 120u));
    assert(!std::strcmp(text, "Slot 2: 120 BPM"));
    i18n::lookup = translated;
    assert(i18n::format(text, sizeof(text), i18n::Message::tempo_slot, 2u, 120u));
    assert(!std::strcmp(text, "120 BPM, mémoire 2"));
    assert(i18n::valid_template(i18n::Message::tempo_slot, "{tempo:u} / {slot:u}"));
    assert(!i18n::valid_template(i18n::Message::tempo_slot, "{tempo:s} / {slot:u}"));
    assert(!i18n::valid_template(i18n::Message::tempo_slot, "{tempo:u}"));
    assert(!i18n::valid_template(i18n::Message::tempo_slot, "{tempo:u} {slot:u} {slot:u}"));
    assert(!i18n::format(text, sizeof(text), i18n::Message::tempo_slot, 2u));
    assert(!text[0]);
    assert(i18n::format(text, 8, i18n::Message::song_added, "été très long"));
    const char *cursor = text; uint32_t code;
    while (*cursor) assert(cntrl::ui::utf8::next(cursor, code));
    assert(i18n::format(text, sizeof(text), i18n::Message::song_added,
                        "éééééééééééééééééééééééééééééééééééé"));
    cursor = text;
    while (*cursor) assert(cntrl::ui::utf8::next(cursor, code));
}
