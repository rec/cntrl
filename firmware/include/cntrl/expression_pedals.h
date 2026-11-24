#pragma once
#include <cntrl/document.h>
#include <cntrl/calibration_file.h>
#include <vector>
#include <array>
#include <cstdio>

namespace cntrl {
// The adapter supplies biased measurements; settling never blocks the runtime.
class ExpressionPedals {
public:
    ExpressionPedals(unsigned count, files::FileSystem &filesystem,
                     uint32_t rest_ms, uint32_t bias_ms)
        : inputs_(count), assignments_(count), mirrors_(count), delivered_(count), low_(count),
          filesystem_(filesystem), calibrations_(filesystem, count), captures_(count),
          rest_ms_(rest_ms), bias_ms_(bias_ms) {}
    bool load_calibrations() {
        calibration_ready_ = calibrations_.load();
        if (!calibration_ready_ || filesystem_.exists(CalibrationFile::path)) return calibration_ready_;
        auto file = filesystem_.file();
        if (!file) return false;
        for (unsigned i = 0; i < inputs_.size(); ++i) {
            char path[40]; std::snprintf(path, sizeof(path), "/cntrl/expression-%u.cal", i + 1);
            if (!file->open(path, files::Mode::read)) continue;
            LegacyRecord latest;
            for (unsigned slot = 0; slot < 2; ++slot) {
                uint8_t bytes[12];
                if (!file->read(slot * sizeof(bytes), bytes, sizeof(bytes))) continue;
                auto record = LegacyRecord::decode(bytes);
                if (record.generation > latest.generation) latest = record;
            }
            file->close();
            if (latest.generation) calibrations_.import(i, latest.cal);
        }
        return true;
    }
    void configure(const cntrl::ExpressionAssignment *assignments, midi::Outputs outputs = {}) {
        for (unsigned i = 0; i < inputs_.size(); ++i) {
            assignments_[i] = assignments ? assignments[i] : ExpressionAssignment{};
            const auto *mirror = outputs.route(assignments_[i].mirror_route);
            std::snprintf(mirrors_[i].data(), mirrors_[i].size(), "%s", mirror ? mirror->name : "");
            delivered_[i] = {-1, -1};
            inputs_[i].reset();
        }
        phase_ = 0;
        cancel();
    }
    void cancel() {
        if (!calibrating_) return;
        for (auto &input : inputs_) input.disarm();
        for (auto &values : delivered_) values = {-1, -1};
        calibrating_ = capture_paused_ = false;
    }
    bool calibrate() {
        if (!calibrating_) {
            std::fill(captures_.begin(), captures_.end(), CalibrationCapture{});
            calibrating_ = true;
            return true;
        }
        if (captured() && (!calibration_ready_ || !calibrations_.save(&captures_, calibrations_.settle_ms()))) return false;
        cancel();
        return true;
    }

    // Each poll samples every enabled input at most once.
    template<class Enabled, class Sample, class Available, class Send>
    void poll(uint32_t now, Enabled enabled, Sample sample, Available available, Send send) {
        if (inputs_.empty() || uint32_t(now - tick_) < (phase_ ? bias_ms_ : rest_ms_)) return;
        tick_ = now;
        for (unsigned slot = 0; slot < inputs_.size(); ++slot) {
            const unsigned i = (first_ + slot) % inputs_.size();
            if (!enabled(i)) {
                inputs_[i].reset();
                delivered_[i] = {-1, -1};
                continue;
            }
            if (phase_ == 0) { sample(i, phase_); continue; }
            if (phase_ == 1) { low_[i] = sample(i, phase_); continue; }
            const int high = sample(i, phase_);
            inputs_[i].sample(low_[i], high, calibrations_.calibration(i));
            if (inputs_[i].position() < 0) delivered_[i] = {-1, -1};
            if (calibrating_) {
                if (!capture_paused_ && inputs_[i].reading() >= 0)
                    captures_[i].sample((low_[i] + high + 1) / 2, now, calibrations_.settle_ms());
                continue;
            }
            const auto &a = assignments_[i];
            if (!a.channel) continue;
            if (!available(a.output) || (mirrors_[i][0] && !available(mirrors_[i].data()))) { inputs_[i].disarm(); delivered_[i] = {-1, -1}; continue; }
            int value = inputs_[i].pending(a);
            if (value >= 0) {
                uint8_t data[] = {uint8_t(0xb0 | (a.channel - 1)), a.controller, uint8_t(value)};
                auto &delivered = delivered_[i];
                if (delivered[0] != value && send(i, a.output, data)) delivered[0] = value;
                if (mirrors_[i][0] && delivered[1] != value && send(i, mirrors_[i].data(), data)) delivered[1] = value;
                if (delivered[0] == value && (!mirrors_[i][0] || delivered[1] == value)) inputs_[i].sent(value);
            }
        }
        phase_ = (phase_ + 1) % 3;
        if (!phase_) first_ = (first_ + 1) % inputs_.size();
    }
    unsigned count() const { return inputs_.size(); }
    bool calibrating() const { return calibrating_; }
    void pause_capture(bool paused) { capture_paused_ = paused; }
    bool captured() const {
        return calibrating_ && std::any_of(captures_.begin(), captures_.end(), [](const auto &capture) { return capture.toe_ready; });
    }
    const CalibrationCapture &capture(unsigned jack) const { return captures_[jack]; }
    ExpressionCalibration calibration(unsigned jack) const { return calibrations_.calibration(jack); }
    const char *name(unsigned jack) const { return calibrations_.name(jack); }
    unsigned settle_ms() const { return calibrations_.settle_ms(); }
    bool save_calibration_settings(unsigned settle_ms, unsigned jack, const char *label) {
        return calibration_ready_ && !calibrating_ && calibrations_.save(nullptr, settle_ms, jack, label);
    }

private:
    struct LegacyRecord {
        uint32_t generation = 0;
        cntrl::ExpressionCalibration cal{};
        static LegacyRecord decode(const uint8_t *bytes) {
            const auto heel = u16(bytes + 4), toe = u16(bytes + 6);
            if (heel > 1023 || toe > 1023) return {};
            const ExpressionCalibration calibration{ExpressionCalibration::from_10_bit(heel), ExpressionCalibration::from_10_bit(toe)};
            if (!calibration.valid() || u32(bytes + 8) != cntrl::checksum(bytes, 8)) return {};
            return {u32(bytes), calibration};
        }
    };
    std::vector<cntrl::ExpressionInput> inputs_;
    std::vector<cntrl::ExpressionAssignment> assignments_;
    std::vector<std::array<char, 32>> mirrors_;
    std::vector<std::array<int, 2>> delivered_;
    std::vector<int> low_;
    files::FileSystem &filesystem_;
    CalibrationFile calibrations_;
    std::vector<CalibrationCapture> captures_;
    bool calibration_ready_ = false;
    bool calibrating_ = false, capture_paused_ = false;
    uint32_t rest_ms_, bias_ms_;
    unsigned phase_ = 0, first_ = 0;
    uint32_t tick_ = 0;
};

} // namespace cntrl
