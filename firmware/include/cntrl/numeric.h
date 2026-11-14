#pragma once
#include <cstdint>
#include <cstring>

namespace cntrl {
inline uint16_t u16(const uint8_t *p) { return p[0] | uint16_t(p[1]) << 8; }
inline uint32_t u32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
// A portable full-width product; Cortex-M7 does not provide __int128.
struct IntegerProduct {
    uint64_t high, low;
    static IntegerProduct multiply(uint64_t a, uint64_t b) {
        uint64_t first = uint64_t(uint32_t(a)) * uint32_t(b);
        uint64_t middle = (a >> 32) * uint32_t(b) + (first >> 32);
        uint64_t carry = middle >> 32;
        middle = uint64_t(uint32_t(a)) * (b >> 32) + uint32_t(middle);
        return {(a >> 32) * (b >> 32) + carry + (middle >> 32), (middle << 32) | uint32_t(first)};
    }
};

// Floor(a * b / divisor), saturating only if the result exceeds uint64_t.
inline uint64_t multiply_divide(uint64_t a, uint64_t b, uint64_t divisor) {
    auto product = IntegerProduct::multiply(a,b);
    if (product.high >= divisor) return UINT64_MAX;
    if (!product.high) return product.low / divisor;
    uint64_t quotient = 0, remainder = product.high;
    for (unsigned bit = 64; bit--;) {
        bool carry = remainder >> 63;
        remainder = (remainder << 1) | ((product.low >> bit) & 1);
        if (carry || remainder >= divisor) { remainder -= divisor; quotient |= uint64_t(1) << bit; }
    }
    return quotient;
}

inline uint64_t binary64_bits(const double &value) {
    static_assert(sizeof(value) == sizeof(uint64_t), "MIDI speed uses IEEE binary64");
    uint64_t bits; std::memcpy(&bits,&value,sizeof(bits)); return bits;
}

inline bool positive_finite_binary64(uint64_t bits) {
    return bits && !(bits >> 63) && ((bits >> 52) & 2047) != 2047;
}

// Apply the exact positive binary64 rate without floating-point arithmetic.
// All finite positive encoded rates, including subnormals, remain supported.
inline uint64_t scale_binary64(uint64_t value, uint64_t bits, bool round = false) {
    unsigned exponent = unsigned((bits >> 52) & 2047);
    uint64_t mantissa = bits & ((uint64_t(1) << 52) - 1);
    if (exponent) mantissa |= uint64_t(1) << 52;
    auto product = IntegerProduct::multiply(value,mantissa);
    int shift = exponent ? int(exponent) - 1075 : -1074;
    if (shift >= 0) {
        if (!product.high && !product.low) return 0;
        if (product.high || shift >= 64 || product.low > (UINT64_MAX >> shift)) return UINT64_MAX;
        return product.low << shift;
    }
    unsigned right = unsigned(-shift);
    if (right >= 128) return 0;
    uint64_t result;
    bool halfway;
    if (right >= 64) {
        result = product.high >> (right - 64);
        halfway = right == 64 ? product.low >> 63 : (product.high >> (right - 65)) & 1;
    } else {
        if (product.high >> right) return UINT64_MAX;
        result = (product.high << (64 - right)) | (product.low >> right);
        halfway = (product.low >> (right - 1)) & 1;
    }
    return round && halfway && result != UINT64_MAX ? result + 1 : result;
}

// Nearest integer, with halfway values rounded toward positive infinity.
inline int scale_integer(int value, int low, int high, int output_low, int output_high) {
    int64_t denominator = int64_t(high) - low;
    int64_t numerator = 2 * (int64_t(output_low) * denominator + (int64_t(value) - low) * (int64_t(output_high) - output_low)) + denominator;
    denominator *= 2;
    return int(numerator / denominator - (numerator < 0 && numerator % denominator != 0));
}
}
