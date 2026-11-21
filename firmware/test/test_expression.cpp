#include <cntrl/expression.h>
#include <cassert>

using namespace cntrl;

int main() {
    ExpressionCalibration cal{400, 3600};
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
    const auto sample = [&](int low, int high, ExpressionCalibration endpoints) {
        input.sample(4 * low, 4 * high, endpoints);
    };
    for (int n = 0; n < 12; ++n) {
        sample(500, 500, {}); assert(input.pending(a) < 0);
    }
    input.reset();
    for (int n = 0; n < 100; ++n) {
        sample(500 + n % 3, 504 - n % 2, cal);
        assert(input.pending(a) < 0); // stationary jitter across a CC boundary
    }
    for (int n = 0; n < 50; ++n) sample(950, 950, cal);
    assert(input.position() == 1023 && input.pending(a) == 110);
    // A busy destination receives only the latest position on recovery.
    for (int n = 0; n < 50; ++n) sample(80, 80, cal);
    assert(input.pending(a) == 10);
    input.sent(10); assert(input.pending(a) < 0);
    sample(0, 1023, cal);
    assert(input.reading() == -1 && input.pending(a) < 0);
    for (int n = 0; n < 12; ++n) {
        sample(950, 950, {3600, 400});
        assert(input.pending(a) < 0); // reconnection and reversed physical travel
    }
    assert(input.position() == 0);
    input.reset();
    a.minimum = 0; a.maximum = 127;
    for (unsigned i = 0; i < 10; ++i) { sample(0, 0, {0, 4092}); (void)input.pending(a); }
    for (unsigned i = 0; i < 100; ++i) {
        sample(1023, 1023, {0, 4092});
        int value = input.pending(a);
        if (value >= 0) input.sent(value);
    }
    assert(input.position() == 1023);
    input.reset();
    // One ADC count near a calibrated endpoint can span several CC steps.
    for (int n = 0; n < 8; ++n) {
        sample(101, 101, {400, 656}); assert(input.pending(a) < 0);
    }
    for (int n = 0; n < 16; ++n) {
        sample(99, 99, {400, 656}); assert(input.pending(a) < 0);
    }

    for (int polarity : {-1, 1}) {
        CalibrationCapture capture;
        const auto raw = [&](int value) { return polarity > 0 ? value : 4095 - value; };
        capture.sample(raw(1000), 0, 2000);
        capture.sample(raw(500), 100, 2000);
        assert(capture.heel == raw(500) && !capture.heel_ready);
        capture.sample(raw(800), 200, 2000);
        assert(capture.heel_ready && !capture.toe_ready);
        capture.sample(raw(3500), 500, 2000);
        capture.sample(raw(3502), 2499, 2000);
        assert(!capture.toe_ready);
        capture.sample(raw(3501), 2500, 2000);
        assert(capture.toe_ready);
        capture.sample(raw(400), 2600, 2000);
        capture.sample(raw(3900), 2800, 2000);
        assert(capture.value().heel == raw(400) && capture.value().toe == raw(3900));
    }
    CalibrationCapture reverse;
    reverse.sample(1500, 0, 2000);
    reverse.sample(500, 100, 2000);
    reverse.sample(3600, 200, 2000);
    reverse.sample(3500, 300, 2000);
    assert(reverse.toe_ready && reverse.toe == 3600);
}
