#pragma once
#include <algorithm>
#include <cstdlib>
#include <cstdint>

namespace cntrl {
constexpr unsigned expression_record_size = 38; // 32-byte output followed by six assignment bytes.
constexpr unsigned expression_section_size = 4 * expression_record_size;

struct ExpressionCalibration {
    uint16_t heel = 0, toe = 0;
    bool valid() const { return heel <= 1023 && toe <= 1023 && std::abs(int(toe) - int(heel)) >= 64; }
};

enum class ExpressionCurve : uint8_t { linear, slow, fast };

struct ExpressionAssignment {
    char output[32] = {};
    uint8_t channel = 0, controller = 0, minimum = 0, maximum = 127;
    bool reverse = false;
    ExpressionCurve curve = ExpressionCurve::linear;

    int value(unsigned position) const {
        unsigned x = reverse ? 1023 - position : position;
        if (curve == ExpressionCurve::slow) x = (x * x + 511) / 1023;
        if (curve == ExpressionCurve::fast) x = 1023 - ((1023 - x) * (1023 - x) + 511) / 1023;
        return minimum + (x * (maximum - minimum) + 511) / 1023;
    }
};

class ExpressionInput {
public:
    void reset() { samples_ = 0; baseline_ = last_value_ = -1; position_ = -1; }
    void disarm() { baseline_ = last_value_ = -1; }
    int position() const { return position_; } // 0..1023 between calibrated endpoints.
    int reading() const { return samples_ >= 3 ? filtered_ : -1; }

    // An open wiper follows the two weak biases. Only accept successive connected samples.
    void sample(int low, int high, const ExpressionCalibration &cal) {
        if (low < 0 || high < 0 || low > 1023 || high > 1023 || std::abs(high - low) > 256) { reset(); return; }
        const int raw = (low + high + 1) / 2;
        filtered_ = samples_ ? (3 * filtered_ + raw + 2) / 4 : raw;
        if (samples_ < 3) ++samples_;
        if (samples_ < 3 || !cal.valid()) { position_ = -1; return; }
        position_ = std::clamp((filtered_ - int(cal.heel)) * 1023 / (int(cal.toe) - int(cal.heel)), 0, 1023);
    }
    // Call after every sample. Keep just the current value if an output is busy.
    int pending(const ExpressionAssignment &assignment) {
        if (position_ < 0 || !assignment.channel) return -1;
        if (baseline_ < 0) { baseline_ = position_; baseline_raw_ = filtered_; last_value_ = assignment.value(position_); return -1; }
        int value = assignment.value(position_);
        if (value == last_value_) return -1;
        const bool moved = std::abs(position_ - baseline_) >= 8 && std::abs(filtered_ - baseline_raw_) >= 3;
        if (!moved && position_ != 0 && position_ != 1023) return -1;
        return value;
    }
    void sent(int value) { last_value_ = value; baseline_ = position_; baseline_raw_ = filtered_; }
private:
    unsigned samples_ = 0;
    int filtered_ = 0, position_ = -1, baseline_ = -1, baseline_raw_ = 0, last_value_ = -1;
};
}
