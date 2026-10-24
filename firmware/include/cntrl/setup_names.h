#pragma once
#include <cstring>
#include <cstdint>

namespace cntrl {
inline int name_compare(const char *a, const char *b) {
    for (;; ++a, ++b) {
        unsigned x = uint8_t(*a), y = uint8_t(*b);
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y || !x) return int(x) - int(y);
    }
}
inline bool setup_name(const char *name) {
    unsigned n = std::strlen(name);
    if (!n || n > 63 || name[n - 1] == ' ' || name[n - 1] == '.') return false;
    for (unsigned i = 0; i < n; ++i)
        if (uint8_t(name[i]) < 32 || uint8_t(name[i]) > 126 || std::strchr("<>:\"/\\|?*", name[i])) return false;
    return true;
}
inline void hex_text(const char *text, char *hex) {
    const char *digits = "0123456789abcdef";
    while (*text) { uint8_t c = uint8_t(*text++); *hex++ = digits[c >> 4]; *hex++ = digits[c & 15]; }
    *hex = 0;
}
inline bool unhex_name(const char *hex, char *name) {
    unsigned n = std::strlen(hex);
    if (!n || n > 126 || n % 2) return false;
    for (unsigned i = 0; i < n; i += 2) {
        unsigned value = 0;
        for (unsigned j = 0; j < 2; ++j) {
            char c = hex[i + j];
            if (c >= '0' && c <= '9') value = value * 16 + unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') value = value * 16 + unsigned(c - 'a' + 10);
            else return false;
        }
        if (!value) return false;
        name[i / 2] = char(value);
    }
    name[n / 2] = 0;
    return setup_name(name);
}
}
