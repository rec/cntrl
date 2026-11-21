#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <charconv>
#include <algorithm>
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
CNTRL_FLASH_ROUTINE(i18n_format_integer)
#endif
// The target's nano printf omits long-long integer conversions.
inline bool format_integer(char *destination, unsigned capacity, const Argument &argument, const char *spec) {
    if (!capacity || (argument.kind != Argument::Kind::signed_number && argument.kind != Argument::Kind::unsigned_number)) return false;
    const char *p = spec;
    bool left = false, plus = false, space = false, alternate = false, zero = false;
    for (; *p && std::strchr("-+ #0", *p); ++p) {
        left |= *p == '-'; plus |= *p == '+'; space |= *p == ' '; alternate |= *p == '#'; zero |= *p == '0';
    }
    unsigned width = 0, precision = 0; bool explicit_precision = false;
    while (*p >= '0' && *p <= '9') width = std::min(capacity + 32, width * 10 + unsigned(*p++ - '0'));
    if (*p == '.') {
        explicit_precision = true; ++p;
        while (*p >= '0' && *p <= '9') precision = std::min(capacity + 32, precision * 10 + unsigned(*p++ - '0'));
    }
    const char type = spec[std::strlen(spec) - 1];
    const bool signed_type = type == 'd' || type == 'i', hex = type == 'x' || type == 'X';
    const uint64_t raw = argument.kind == Argument::Kind::signed_number ? uint64_t(argument.signed_number) : argument.unsigned_number;
    const bool negative = signed_type && int64_t(raw) < 0;
    const uint64_t magnitude = negative ? uint64_t(0) - raw : raw;
    char digits[32]; const auto converted = std::to_chars(digits, digits + sizeof(digits), magnitude, hex ? 16 : 10);
    const unsigned length = explicit_precision && !precision && !magnitude ? 0 : unsigned(converted.ptr - digits);
    if (type == 'X') for (unsigned i = 0; i < length; ++i) if (digits[i] >= 'a' && digits[i] <= 'f') digits[i] -= 'a' - 'A';
    char prefix[3]; unsigned prefix_length = 0;
    if (negative) prefix[prefix_length++] = '-';
    else if (signed_type && (plus || space)) prefix[prefix_length++] = plus ? '+' : ' ';
    if (alternate && hex && magnitude) { prefix[prefix_length++] = '0'; prefix[prefix_length++] = type; }
    unsigned zeros = precision > length ? precision - length : 0;
    const unsigned used = prefix_length + zeros + length, padding = width > used ? width - used : 0;
    const unsigned spaces = zero && !left && !explicit_precision ? 0 : padding;
    if (zero && !left && !explicit_precision) zeros += padding;
    unsigned offset = 0;
    auto append = [&](char value) { if (offset + 1 < capacity) destination[offset++] = value; };
    if (!left) for (unsigned i = 0; i < spaces; ++i) append(' ');
    for (unsigned i = 0; i < prefix_length; ++i) append(prefix[i]);
    for (unsigned i = 0; i < zeros; ++i) append('0');
    for (unsigned i = 0; i < length; ++i) append(digits[i]);
    if (left) for (unsigned i = 0; i < spaces; ++i) append(' ');
    destination[offset] = 0; return true;
}
#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(i18n_render)
#endif
inline bool render(char *destination, unsigned capacity, const Definition &definition,
                   const char *text, const Argument *values, unsigned count
#ifdef CNTRL_EMULATOR
                   , uint8_t (*literals)[2] = nullptr
#endif
                   ) {
    if (!capacity) return false;
    destination[0] = 0;
    if (count != definition.count || !validate(definition, text)) return false;
    unsigned used = 0;
    auto append = [&](const char *value, unsigned length) {
        const unsigned available = capacity - used - 1;
        const unsigned copied = length < available ? length : available;
        std::memcpy(destination + used, value, copied); used += copied; destination[used] = 0;
    };
#ifdef CNTRL_EMULATOR
    unsigned literal = 0;
    if (literals) literals[0][0] = literals[0][1] = 0;
#endif
    for (const char *p = text; *p;) {
        if (*p != '{') {
            append(p++, 1);
#ifdef CNTRL_EMULATOR
            if (literals) literals[literal][1] = uint8_t(used);
#endif
            continue;
        }
        const char *end = std::strchr(p + 1, '}');
        const unsigned index = unsigned(field(definition, p + 1, end));
        const auto &argument = values[index]; const char *spec = definition.fields[index].spec;
        const unsigned n = unsigned(std::strlen(spec)); const char type = spec[n - 1];
        char result[96], pattern[32] = "%"; unsigned offset = 1;
        for (unsigned i = 0; i + 1 < n; ++i) if (spec[i] != 'l') pattern[offset++] = spec[i];
        pattern[offset++] = type; pattern[offset] = 0;
        if (type == 's') {
            if (argument.kind != Argument::Kind::text) return false;
            std::snprintf(result, sizeof(result), pattern, argument.text);
            utf8::copy(result, sizeof(result), result);
        } else if (!format_integer(result, sizeof(result), argument, spec)) return false;
        append(result, unsigned(std::strlen(result))); p = end + 1;
#ifdef CNTRL_EMULATOR
        if (literals) { ++literal; literals[literal][0] = literals[literal][1] = uint8_t(used); }
#endif
    }
    // The formatter's byte bound must not leave a partial UTF-8 character.
    utf8::copy(destination, capacity, destination);
    return true;
}
}
