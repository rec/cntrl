#include <cntrl/ui/messages.h>
#include <cassert>
#include <cstring>

namespace i18n = cntrl::ui::i18n;

const char *translated(i18n::Message message, bool short_form) {
    if (short_form) return nullptr;
    if (message == i18n::Message::cycles_patches) return "{patches:u} patches, {cycles:u} cycles";
    return nullptr;
}

int main() {
    char text[64];
    const i18n::Field number_field[] = {{"value", "llu"}};
    const i18n::Definition number = {"{value:llu}", "", number_field, 1};
    const i18n::Argument maximum[] = {UINT64_MAX};
    assert(i18n::render(text, sizeof(text), number, number.text, maximum, 1));
    assert(!std::strcmp(text, "18446744073709551615"));
    const i18n::Field signed_field[] = {{"value", "d"}};
    const i18n::Definition signed_number = {"{value:d}", "", signed_field, 1};
    const i18n::Argument minimum[] = {INT64_MIN};
    assert(i18n::render(text, sizeof(text), signed_number, signed_number.text, minimum, 1));
    assert(!std::strcmp(text, "-9223372036854775808"));
    assert(i18n::format(text, sizeof(text), i18n::Message::uptime_value, 12ul, 3u, 4u));
    assert(std::strstr(text, "03") && std::strstr(text, "04") && !std::strstr(text, "lu"));
    const struct { const char *spec; const char *pattern; int value; const char *expected; } numbers[] = {
        {"#08X", "{value:#08X}", 42, "0X00002A"},
        {"+6.3d", "{value:+6.3d}", 42, "  +042"},
        {"-5d", "{value:-5d}", -42, "-42  "},
        {".0u", "{value:.0u}", 0, ""},
    };
    for (const auto &value : numbers) {
        const i18n::Field fields[] = {{"value", value.spec}};
        const i18n::Definition definition = {value.pattern, "", fields, 1};
        const i18n::Argument arguments[] = {value.value};
        assert(i18n::render(text, sizeof(text), definition, definition.text, arguments, 1));
        assert(!std::strcmp(text, value.expected));
    }
    assert(i18n::format(text, sizeof(text), i18n::Message::cycles_patches, 2u, 120u));
    assert(!std::strcmp(text, "Cycles:2  Patches:120"));
    i18n::lookup = translated;
    assert(i18n::format(text, sizeof(text), i18n::Message::cycles_patches, 2u, 120u));
    assert(!std::strcmp(text, "120 patches, 2 cycles"));
    assert(i18n::valid_template(i18n::Message::cycles_patches, "{patches:u} / {cycles:u}"));
    assert(!i18n::valid_template(i18n::Message::cycles_patches, "{patches:s} / {cycles:u}"));
    assert(!i18n::valid_template(i18n::Message::cycles_patches, "{patches:u}"));
    assert(!i18n::valid_template(i18n::Message::cycles_patches, "{patches:u} {cycles:u} {cycles:u}"));
    assert(!i18n::format(text, sizeof(text), i18n::Message::cycles_patches, 2u));
    assert(!text[0]);
    assert(i18n::format(text, 8, i18n::Message::song_added, "été très long"));
    const char *cursor = text; uint32_t code;
    while (*cursor) assert(cntrl::ui::utf8::next(cursor, code));
    assert(i18n::format(text, sizeof(text), i18n::Message::song_added,
                        "éééééééééééééééééééééééééééééééééééé"));
    cursor = text;
    while (*cursor) assert(cntrl::ui::utf8::next(cursor, code));
}
