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
}
