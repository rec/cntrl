#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace cntrl {
inline constexpr uint8_t usb_magic[] = {0x89, 'C', 'N', 'T', 'R', 'L', '\r', '\n'};
inline constexpr unsigned usb_header_size = 40;
enum class UsbKind : uint8_t {
    open = 1, close = 2, command = 3, poll = 4,
    setup = 16, bundle = 17, songs = 18, font = 19, history = 20, movie = 21, disk = 22,
    text = 128, data = 129, error = 255
};
enum class UsbStatus : uint16_t { ok = 0, lost = 1, invalid = 2, version = 3 };
inline constexpr auto usb_crc_table = [] {
    std::array<uint32_t, 256> table{};
    for (unsigned i = 0; i < table.size(); ++i) {
        uint32_t n = i;
        for (unsigned b = 0; b < 8; ++b) n = (n >> 1) ^ (0xedb88320u & (0u - (n & 1)));
        table[i] = n;
    }
    return table;
}();
inline uint32_t usb_crc(const uint8_t *bytes, unsigned length) {
    uint32_t crc = 0xffffffff;
    for (unsigned i = 0; i < length; ++i) crc = (crc >> 8) ^ usb_crc_table[(crc ^ bytes[i]) & 255];
    return ~crc;
}
inline uint32_t usb_word(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline void usb_word(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(n >> (i * 8));
}
struct UsbFrame {
    UsbKind kind = UsbKind::poll;
    UsbStatus status = UsbStatus::ok;
    uint32_t session = 0, sequence = 0, acknowledged = 0, block_size = 0, length = 0, checksum = 0;
    void encode(uint8_t *out, const uint8_t *payload) const {
        std::memcpy(out, usb_magic, sizeof(usb_magic));
        out[8] = 1; out[9] = uint8_t(kind); out[10] = uint8_t(status); out[11] = uint8_t(unsigned(status) >> 8);
        usb_word(out + 12, session); usb_word(out + 16, sequence); usb_word(out + 20, acknowledged);
        usb_word(out + 24, block_size); usb_word(out + 28, length);
        usb_word(out + 32, usb_crc(payload, length)); usb_word(out + 36, usb_crc(out, 36));
        if (length) std::memcpy(out + usb_header_size, payload, length);
    }
    static bool valid_header(const uint8_t *p) {
        return !std::memcmp(p, usb_magic, sizeof(usb_magic)) && usb_word(p + 36) == usb_crc(p, 36);
    }
    static UsbFrame decode(const uint8_t *p) {
        return {UsbKind(p[9]), UsbStatus(unsigned(p[10]) | unsigned(p[11]) << 8),
            usb_word(p + 12), usb_word(p + 16), usb_word(p + 20), usb_word(p + 24), usb_word(p + 28), usb_word(p + 32)};
    }
};
} // namespace cntrl
