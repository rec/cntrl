#include <cntrl/numeric.h>
#include <cassert>
#include <cmath>
#include <limits>
#include <initializer_list>

int main() {
    using cntrl::binary64_bits;
    using cntrl::multiply_divide;
    using cntrl::positive_finite_binary64;
    using cntrl::scale_binary64;
    using cntrl::scale_integer;
    assert(multiply_divide(UINT64_MAX,UINT64_MAX,UINT64_MAX) == UINT64_MAX);
    assert(multiply_divide(UINT64_MAX - 1,UINT64_MAX - 1,UINT64_MAX) == UINT64_MAX - 2);
    assert(multiply_divide(uint64_t(1) << 63,3,2) == (uint64_t(3) << 62));
    assert(multiply_divide(UINT64_MAX,UINT64_MAX,1) == UINT64_MAX);
    // Long recorded segments retain exact endpoint and halfway expression times.
    assert(multiply_divide(UINT64_MAX / 2,uint64_t(64) << 32,UINT64_MAX) == (uint64_t(32) << 32) - 1);
    for (unsigned original = 0; original <= 16383; ++original)
        assert(scale_integer(int(original),0,16383,-8192,8191) == int(original) - 8192);
    assert(scale_integer(1,0,2,0,127) == 64);
    assert(scale_integer(1,0,2,0,-127) == -63);
    assert(scale_integer(1,0,2,-1,0) == 0);
    assert(scale_integer(0,-8192,8191,16383,0) == 8191);
    for (double speed : {1.0,0.5,1.5,2.0}) {
        assert(positive_finite_binary64(binary64_bits(speed)));
        assert(scale_binary64(3600000001ull,binary64_bits(speed)) == uint64_t(3600000001ull * speed));
    }
    assert(scale_binary64(UINT64_MAX,binary64_bits(1.0)) == UINT64_MAX);
    assert(scale_binary64(UINT64_MAX,binary64_bits(0.5)) == UINT64_MAX / 2);
    assert(scale_binary64(UINT64_MAX,binary64_bits(0.5),true) == (uint64_t(1) << 63));
    assert(scale_binary64(UINT64_MAX,binary64_bits(2.0)) == UINT64_MAX);
    assert(scale_binary64(1,binary64_bits(std::numeric_limits<double>::max())) == UINT64_MAX);
    assert(scale_binary64(UINT64_MAX,1) == 0); // Smallest positive subnormal.
    // Use the encoded rate exactly, without rounding its product to binary64 first.
    assert(scale_binary64(10,binary64_bits(0.3)) == 2);
    assert(positive_finite_binary64(1));
    for (double invalid : {0.0,-0.0,-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        assert(!positive_finite_binary64(binary64_bits(invalid)));
    assert(scale_binary64(1000,binary64_bits(120.0),true) == 120000);
    assert(scale_binary64(1000,binary64_bits(120.0625),true) == 120063);
    assert(scale_binary64(1000,binary64_bits(0.00001),true) == 0);
}
