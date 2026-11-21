#pragma once
#include <algorithm>
#include <cstdlib>
#include <cstdint>

namespace cntrl {
constexpr unsigned expression_record_size = 38; // 32-byte output followed by six assignment bytes.
constexpr unsigned expression_section_size = 4 * expression_record_size;

struct ExpressionCalibration {
    static constexpr unsigned maximum = 4095;
    static constexpr unsigned minimum_travel = 256;
    uint16_t heel = 0, toe = 0;
    bool valid() const { return heel <= maximum && toe <= maximum && std::abs(int(toe) - int(heel)) >= int(minimum_travel); }
    static uint16_t from_10_bit(unsigned raw) { return uint16_t((raw * maximum + 511) / 1023); }
};

struct CalibrationCapture {
    int heel = -1, toe = -1;
    bool heel_ready = false, toe_ready = false;

    void sample(int raw, uint32_t now, uint32_t settle_ms) {
        if (raw < 0) return;
        if (start_ < 0) { start_ = heel = raw; return; }
        if (!direction_) {
            heel = raw;
            if (std::abs(raw - start_) < movement) return;
            direction_ = raw > start_ ? 1 : -1;
        }
        const auto farther_heel = direction_ * (raw - heel);
        if (farther_heel > 0) heel = raw;
        if (!heel_ready) {
            if (direction_ * (heel - raw) < movement) return;
            heel_ready = true;
            toe = quiet_position_ = raw;
            changed_ = now;
        }
        if (direction_ * (toe - raw) > 0) toe = raw;
        if (std::abs(raw - quiet_position_) >= movement) {
            quiet_position_ = raw;
            changed_ = now;
        }
        if (value().valid() && (direction_ * (raw - toe) >= movement || uint32_t(now - changed_) >= settle_ms))
            toe_ready = true;
    }

    ExpressionCalibration value() const {
        return heel >= 0 && toe >= 0 ? ExpressionCalibration{uint16_t(heel), uint16_t(toe)} : ExpressionCalibration{};
    }

private:
    static constexpr int movement = 16;
    int start_ = -1, direction_ = 0, quiet_position_ = -1;
    uint32_t changed_ = 0;
};

enum class ExpressionCurve : uint8_t { linear, slow, fast };

struct ExpressionAssignment {
    char output[32] = {};
    uint8_t channel = 0, controller = 0, minimum = 0, maximum = 127;
    bool reverse = false;
    ExpressionCurve curve = ExpressionCurve::linear;
    uint8_t mirror_route = 255;

    int value(unsigned position) const {
        unsigned x = reverse ? 1023 - position : position;
        if (curve == ExpressionCurve::slow) x = (x * x + 511) / 1023;
        if (curve == ExpressionCurve::fast) x = 1023 - ((1023 - x) * (1023 - x) + 511) / 1023;
        return minimum + (x * (maximum - minimum) + 511) / 1023;
    }
};

class ExpressionInput {
public:
    void reset() { samples_ = 0; position_ = -1; disarm(); }
    void disarm() { baseline_ = last_value_ = -1; has_sent_ = false; }
    int position() const { return position_; } // 0..1023 between calibrated endpoints.
    int reading() const { return samples_ >= 3 ? filtered_ : -1; }

    // An open wiper follows the two weak biases. Only accept successive connected samples.
    void sample(int low, int high, const ExpressionCalibration &cal) {
        if (low < 0 || high < 0 || low > int(ExpressionCalibration::maximum)
            || high > int(ExpressionCalibration::maximum) || std::abs(high - low) > 1024) { reset(); return; }
        const int raw = (low + high + 1) / 2;
        filtered_ = samples_ ? (3 * filtered_ + raw + 2) / 4 : raw;
        if ((raw == cal.heel || raw == cal.toe) && std::abs(filtered_ - raw) <= 8) filtered_ = raw;
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
        const bool moved = std::abs(position_ - baseline_) >= 8 && std::abs(filtered_ - baseline_raw_) >= 12;
        const bool finish_at_endpoint = has_sent_ && (position_ == 0 || position_ == 1023);
        if (!moved && !finish_at_endpoint) return -1;
        return value;
    }
    void sent(int value) { has_sent_ = true; last_value_ = value; baseline_ = position_; baseline_raw_ = filtered_; }
private:
    unsigned samples_ = 0;
    bool has_sent_ = false;
    int filtered_ = 0, position_ = -1, baseline_ = -1, baseline_raw_ = 0, last_value_ = -1;
};
}
