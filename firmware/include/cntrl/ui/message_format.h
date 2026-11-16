#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include "utf8.h"

#if defined(__IMXRT1062__)
#define CNTRL_I18N_STORAGE __attribute__((section(".progmem")))
#else
#define CNTRL_I18N_STORAGE
#endif

namespace cntrl::ui::i18n {
struct Field { const char *name; const char *spec; };
struct Definition { const char *text; const char *short_text; const Field *fields; unsigned count; };
struct Argument {
    enum class Kind { none, text, signed_number, unsigned_number } kind = Kind::none;
    const char *text = nullptr;
    int64_t signed_number = 0;
    uint64_t unsigned_number = 0;
    Argument() = default;
    Argument(const char *value) : kind(Kind::text), text(value) {}
    template<class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    Argument(T value) {
        if constexpr (std::is_signed_v<T>) { kind = Kind::signed_number; signed_number = value; }
        else { kind = Kind::unsigned_number; unsigned_number = value; }
    }
};

#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(i18n_field)
#endif
inline int field(const Definition &definition, const char *begin, const char *end) {
    const char *colon = begin;
    while (colon < end && *colon != ':') ++colon;
    if (colon == end) return -1;
    for (unsigned i = 0; i < definition.count; ++i) {
        const auto &value = definition.fields[i];
        if (std::strlen(value.name) == unsigned(colon - begin)
            && !std::memcmp(value.name, begin, colon - begin)
            && std::strlen(value.spec) == unsigned(end - colon - 1)
            && !std::memcmp(value.spec, colon + 1, end - colon - 1)) return int(i);
    }
    return -1;
}
#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(i18n_validate)
#endif
inline bool validate(const Definition &definition, const char *text) {
    if (definition.count > 16) return false;
    unsigned seen = 0;
    for (const char *p = text; *p; ++p) {
        if (*p == '}') return false;
        if (*p != '{') continue;
        const char *end = std::strchr(p + 1, '}');
        if (!end) return false;
        const int index = field(definition, p + 1, end);
        if (index < 0 || (seen & (1u << index))) return false;
        seen |= 1u << index; p = end;
    }
    return seen == ((1u << definition.count) - 1);
}
#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(i18n_render)
#endif
inline bool render(char *destination, unsigned capacity, const Definition &definition,
                   const char *text, const Argument *values, unsigned count) {
    if (!capacity) return false;
    destination[0] = 0;
    if (count != definition.count || !validate(definition, text)) return false;
    unsigned used = 0;
    auto append = [&](const char *value, unsigned length) {
        const unsigned available = capacity - used - 1;
        const unsigned copied = length < available ? length : available;
        std::memcpy(destination + used, value, copied); used += copied; destination[used] = 0;
    };
    for (const char *p = text; *p;) {
        if (*p != '{') { append(p++, 1); continue; }
        const char *end = std::strchr(p + 1, '}');
        const unsigned index = unsigned(field(definition, p + 1, end));
        const auto &argument = values[index]; const char *spec = definition.fields[index].spec;
        const unsigned n = unsigned(std::strlen(spec)); const char type = spec[n - 1];
        char result[96], pattern[32] = "%"; unsigned offset = 1;
        for (unsigned i = 0; i + 1 < n; ++i) if (spec[i] != 'l') pattern[offset++] = spec[i];
        if (type != 's') { pattern[offset++] = 'l'; pattern[offset++] = 'l'; }
        pattern[offset++] = type; pattern[offset] = 0;
        if (type == 's') {
            if (argument.kind != Argument::Kind::text) return false;
            std::snprintf(result, sizeof(result), pattern, argument.text);
            utf8::copy(result, sizeof(result), result);
        } else if (type == 'd' || type == 'i') {
            if (argument.kind != Argument::Kind::signed_number && argument.kind != Argument::Kind::unsigned_number) return false;
            std::snprintf(result, sizeof(result), pattern, (long long)(argument.kind == Argument::Kind::signed_number ? argument.signed_number : int64_t(argument.unsigned_number)));
        } else {
            if (argument.kind != Argument::Kind::signed_number && argument.kind != Argument::Kind::unsigned_number) return false;
            std::snprintf(result, sizeof(result), pattern, (unsigned long long)(argument.kind == Argument::Kind::unsigned_number ? argument.unsigned_number : uint64_t(argument.signed_number)));
        }
        append(result, unsigned(std::strlen(result))); p = end + 1;
    }
    // The formatter's byte bound must not leave a partial UTF-8 character.
    utf8::copy(destination, capacity, destination);
    return true;
}
}
