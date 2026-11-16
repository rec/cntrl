#pragma once
#include <cstdint>
#include <cstring>

namespace cntrl::ui::utf8 {
#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(utf8_next)
#endif
inline bool next(const char *&text, uint32_t &code) {
    const auto *p = reinterpret_cast<const uint8_t *>(text);
    if (!*p) return false;
    code = *p++;
    if (code < 128) { text = reinterpret_cast<const char *>(p); return true; }
    unsigned remaining; uint32_t minimum;
    if (code >= 0xc2 && code <= 0xdf) { remaining = 1; code &= 31; minimum = 128; }
    else if (code >= 0xe0 && code <= 0xef) { remaining = 2; code &= 15; minimum = 2048; }
    else if (code >= 0xf0 && code <= 0xf4) { remaining = 3; code &= 7; minimum = 65536; }
    else return false;
    while (remaining--) {
        if ((*p & 0xc0) != 0x80) return false;
        code = (code << 6) | (*p++ & 63);
    }
    if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    text = reinterpret_cast<const char *>(p); return true;
}
// The supported UI languages use Latin base characters and combining marks.
// Keep these marks, variation selectors and joined sequences with their base.
#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(utf8_extension)
#endif
inline bool extension(uint32_t code) {
    return (code >= 0x300 && code <= 0x36f) || (code >= 0x1ab0 && code <= 0x1aff)
        || (code >= 0x1dc0 && code <= 0x1dff) || (code >= 0x20d0 && code <= 0x20ff)
        || (code >= 0xfe20 && code <= 0xfe2f) || (code >= 0xfe00 && code <= 0xfe0f)
        || code == 0x200d;
}
#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(utf8_copy)
#endif
inline bool copy(char *destination, unsigned capacity, const char *source) {
    if (!capacity) return false;
    const char *cursor = source; unsigned used = 0, cluster = 0; bool joined = false;
    const bool in_place = destination == source;
    if (!in_place) destination[0] = 0;
    while (*cursor) {
        const char *start = cursor; uint32_t code;
        if (!next(cursor, code)) { destination[used] = 0; return false; }
        const bool continuation = extension(code) || joined;
        if (!continuation) cluster = used;
        const unsigned length = unsigned(cursor - start);
        if (length >= capacity - used) {
            destination[continuation ? cluster : used] = 0;
            return false;
        }
        if (!in_place) std::memcpy(destination + used, start, length);
        used += length;
        if (!in_place) destination[used] = 0;
        joined = code == 0x200d;
    }
    destination[used] = 0;
    return true;
}
#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(utf8_erase_last)
#endif
inline bool erase_last(char *text) {
    if (!*text) return false;
    const char *cursor = text; unsigned cluster = 0; bool joined = false;
    while (*cursor) {
        const char *start = cursor; uint32_t code;
        if (!next(cursor, code)) { text[unsigned(start - text)] = 0; return true; }
        if (!extension(code) && !joined) cluster = unsigned(start - text);
        joined = code == 0x200d;
    }
    text[cluster] = 0; return true;
}
} // namespace cntrl::ui::utf8
