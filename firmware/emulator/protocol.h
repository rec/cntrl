#pragma once
#include <cntrl/document.h>
#include <algorithm>
#include <iostream>
#include <vector>
#include <stdexcept>

namespace cntrl::emulator {
inline void integer(std::vector<uint8_t> &bytes, uint64_t value, unsigned size) {
    for (unsigned i = 0; i < size; ++i) bytes.push_back(uint8_t(value >> (8 * i)));
}
inline void frame(const std::vector<uint8_t> &bytes) {
    uint8_t length[4]; put32(length, unsigned(bytes.size()));
    std::cout.write(reinterpret_cast<const char *>(length), 4);
    std::cout.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
}
inline void error(const char *message) {
    std::vector<uint8_t> bytes{3}; bytes.insert(bytes.end(), message, message + std::min(std::strlen(message),size_t(65535))); frame(bytes);
}
inline void ack() { frame({0}); std::cout.flush(); }
inline bool request(std::vector<uint8_t> &bytes) {
    uint8_t length[4];
    std::cin.read(reinterpret_cast<char *>(length), 4);
    if (!std::cin.gcount()) return false;
    if (std::cin.gcount() != 4 || !u32(length) || u32(length) > 65536) throw std::runtime_error("invalid emulator frame length");
    bytes.resize(u32(length));
    if (!std::cin.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size()))) throw std::runtime_error("truncated emulator frame");
    return true;
}
inline uint64_t u64(const uint8_t *p) { return uint64_t(u32(p)) | uint64_t(u32(p + 4)) << 32; }
} // namespace cntrl::emulator
