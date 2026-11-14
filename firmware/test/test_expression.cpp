#include <cntrl/expression.h>
#include <cassert>

using namespace cntrl;

int main() {
    ExpressionCalibration cal{100, 900};
    ExpressionAssignment a;
    a.channel = 1; a.minimum = 10; a.maximum = 110;
    for (auto curve : {ExpressionCurve::linear, ExpressionCurve::slow, ExpressionCurve::fast}) {
        a.curve = curve;
        for (bool reverse : {false, true}) {
            a.reverse = reverse;
            assert(a.value(0) == (reverse ? 110 : 10));
            assert(a.value(1023) == (reverse ? 10 : 110));
            for (unsigned x = 1; x <= 1023; ++x)
                assert(reverse ? a.value(x) <= a.value(x - 1) : a.value(x) >= a.value(x - 1));
        }
    }
    a.reverse = false;
    a.curve = ExpressionCurve::slow; assert(a.value(512) == 35);
    a.curve = ExpressionCurve::fast; assert(a.value(512) == 85);
    a.curve = ExpressionCurve::linear;
    ExpressionInput input;
    for (int n = 0; n < 12; ++n) {
        input.sample(500, 500, {}); assert(input.pending(a) < 0);
    }
    input.reset();
    for (int n = 0; n < 100; ++n) {
        input.sample(500 + n % 3, 504 - n % 2, cal);
        assert(input.pending(a) < 0); // stationary jitter across a CC boundary
    }
    for (int n = 0; n < 50; ++n) input.sample(950, 950, cal);
    assert(input.position() == 1023 && input.pending(a) == 110);
    // A busy destination receives only the latest position on recovery.
    for (int n = 0; n < 50; ++n) input.sample(80, 80, cal);
    assert(input.pending(a) == 10);
    input.sent(10); assert(input.pending(a) < 0);
    input.sample(0, 1023, cal);
    assert(input.reading() == -1 && input.pending(a) < 0);
    for (int n = 0; n < 12; ++n) {
        input.sample(950, 950, {900, 100});
        assert(input.pending(a) < 0); // reconnection and reversed physical travel
    }
    assert(input.position() == 0);
    input.reset();
    a.minimum = 0; a.maximum = 127;
    for (unsigned i = 0; i < 10; ++i) { input.sample(0, 0, {0, 1023}); (void)input.pending(a); }
    for (unsigned i = 0; i < 100; ++i) {
        input.sample(1023, 1023, {0, 1023});
        int value = input.pending(a);
        if (value >= 0) input.sent(value);
    }
    assert(input.position() == 1023);
    input.reset();
    // One ADC count near a calibrated endpoint can span several CC steps.
    for (int n = 0; n < 8; ++n) {
        input.sample(101, 101, {100, 164}); assert(input.pending(a) < 0);
    }
    for (int n = 0; n < 16; ++n) {
        input.sample(99, 99, {100, 164}); assert(input.pending(a) < 0);
    }
}
