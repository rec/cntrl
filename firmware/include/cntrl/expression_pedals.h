#pragma once
#include <cntrl/document.h>
#include <cntrl/filesystem.h>
#include <vector>
#include <cstdio>

namespace cntrl {
// The adapter supplies biased measurements; settling never blocks the runtime.
class ExpressionPedals {
public:
    static void calibration_filename(unsigned input, char *path, unsigned capacity) {
        std::snprintf(path, capacity, "/cntrl/expression-%u.cal", input + 1);
    }
    ExpressionPedals(unsigned count, files::File &calibrations,
                     void (*filename)(unsigned, char *, unsigned), const char *confirm,
                     uint32_t rest_ms, uint32_t bias_ms)
        : inputs_(count), assignments_(count), saved_(count), low_(count),
          calibrations_(calibrations), filename_(filename), confirm_(confirm),
          rest_ms_(rest_ms), bias_ms_(bias_ms) {}
    void load_calibrations() {
        for (unsigned i = 0; i < inputs_.size(); ++i) {
            char path[40]; filename_(i, path, sizeof(path));
            if (!calibrations_.open(path, files::Mode::read)) continue;
            for (unsigned slot = 0; slot < 2; ++slot) {
                uint8_t bytes[12];
                if (!calibrations_.read(slot * sizeof(bytes), bytes, sizeof(bytes))) continue;
                Record record = Record::decode(bytes);
                if (record.valid() && record.generation > saved_[i].generation) saved_[i] = record;
            }
            calibrations_.close();
        }
    }
    void configure(const cntrl::ExpressionAssignment *assignments) {
        for (unsigned i = 0; i < inputs_.size(); ++i) {
            assignments_[i] = assignments[i];
            inputs_[i].reset();
        }
        phase_ = 0;
        cancel();
    }
    void cancel() {
        if (stage_ != Capture::idle) inputs_[selected_].disarm();
        stage_ = Capture::idle;
        message_ = nullptr;
    }
    void select(unsigned index) { cancel(); selected_ = index; }
    void calibrate(bool enabled) {
        if (!enabled) { cancel(); return; }
        if (stage_ == Capture::idle) {
            stage_ = Capture::heel;
            message_ = nullptr;
            inputs_[selected_].reset();
            return;
        }
        int reading = inputs_[selected_].reading();
        if (reading < 0) { message_ = "No pedal"; return; }
        if (stage_ == Capture::heel) {
            heel_ = reading;
            stage_ = Capture::toe;
            message_ = nullptr;
            return;
        }
        cntrl::ExpressionCalibration cal{uint16_t(heel_), uint16_t(reading)};
        if (!cal.valid()) { message_ = "Travel too short"; return; }
        if (!save(selected_, cal)) { message_ = "Save failed"; return; }
        cancel(); inputs_[selected_].reset();
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
                if (selected_ == i && stage_ != Capture::idle) cancel();
                continue;
            }
            if (phase_ == 0) { sample(i, phase_); continue; }
            if (phase_ == 1) { low_[i] = sample(i, phase_); continue; }
            inputs_[i].sample(low_[i], sample(i, phase_), saved_[i].cal);
            if (stage_ != Capture::idle && i == selected_) {
                if (inputs_[i].reading() < 0 && stage_ == Capture::toe) {
                    stage_ = Capture::heel;
                    message_ = "Reconnect: heel";
                }
                continue;
            }
            const auto &a = assignments_[i];
            if (!a.channel) continue;
            if (!available(a.output)) { inputs_[i].disarm(); continue; }
            int value = inputs_[i].pending(a);
            if (value >= 0) {
                uint8_t data[] = {uint8_t(0xb0 | (a.channel - 1)), a.controller, uint8_t(value)};
                if (send(a.output, data)) inputs_[i].sent(value);
            }
        }
        phase_ = (phase_ + 1) % 3;
        if (!phase_) first_ = (first_ + 1) % inputs_.size();
    }
    unsigned selected() const { return selected_; }
    void status(const char *label, bool enabled, char (&title)[40], char (&position)[40], char (&prompt)[40]) const {
        const auto &a = assignments_[selected_];
        if (a.channel) std::snprintf(title, sizeof(title), "%.20s  CC %u Ch %u", label, a.controller, a.channel);
        else std::snprintf(title, sizeof(title), "%.24s  Unassigned", label);
        const auto &input = inputs_[selected_];
        if (!enabled) std::strcpy(position, "Select expression role");
        else if (input.reading() < 0) std::strcpy(position, "No pedal");
        else if (!saved_[selected_].cal.valid()) std::snprintf(position, sizeof(position), "Raw %d  Uncalibrated", input.reading());
        else std::snprintf(position, sizeof(position), "%d%%  Calibrated", (input.position() * 100 + 511) / 1023);
        if (message_) std::snprintf(prompt, sizeof(prompt), "%s", message_);
        else if (!enabled) std::strcpy(prompt, "Select expression role");
        else if (!confirm_[0]) prompt[0] = 0;
        else std::snprintf(prompt, sizeof(prompt), "%s: %s", confirm_,
            stage_ == Capture::heel ? "Heel down" : stage_ == Capture::toe ? "Toe down" : "Calibrate");
    }

private:
    struct Record {
        uint32_t generation = 0;
        cntrl::ExpressionCalibration cal{};
        uint32_t crc = 0;
        void encode(uint8_t *bytes) const {
            for (unsigned i = 0; i < 4; ++i) bytes[i] = uint8_t(generation >> (8 * i));
            bytes[4] = uint8_t(cal.heel); bytes[5] = uint8_t(cal.heel >> 8);
            bytes[6] = uint8_t(cal.toe); bytes[7] = uint8_t(cal.toe >> 8);
            for (unsigned i = 0; i < 4; ++i) bytes[8 + i] = uint8_t(crc >> (8 * i));
        }
        static Record decode(const uint8_t *bytes) {
            Record r;
            for (unsigned i = 0; i < 4; ++i) { r.generation |= uint32_t(bytes[i]) << (8 * i); r.crc |= uint32_t(bytes[8 + i]) << (8 * i); }
            r.cal = {uint16_t(bytes[4] | uint16_t(bytes[5]) << 8), uint16_t(bytes[6] | uint16_t(bytes[7]) << 8)};
            return r;
        }
        uint32_t checksum() const { uint8_t bytes[12]; encode(bytes); return cntrl::checksum(bytes, 8); }
        bool valid() const { return generation && cal.valid() && crc == checksum(); }
    };
    bool save(unsigned jack, cntrl::ExpressionCalibration cal) {
        char path[40]; filename_(jack, path, sizeof(path));
        if (!calibrations_.open(path, files::Mode::create)) return false;
        Record record{saved_[jack].generation + 1, cal};
        record.crc = record.checksum();
        uint8_t bytes[12], verify[12]; record.encode(bytes);
        unsigned offset = ((record.generation - 1) & 1) * sizeof(bytes);
        bool valid = calibrations_.write(offset, bytes, sizeof(bytes)) && calibrations_.sync()
            && calibrations_.read(offset, verify, sizeof(verify)) && !std::memcmp(bytes, verify, sizeof(bytes));
        calibrations_.close();
        if (!valid) return false;
        saved_[jack] = record;
        return true;
    }
    std::vector<cntrl::ExpressionInput> inputs_;
    std::vector<cntrl::ExpressionAssignment> assignments_;
    std::vector<Record> saved_;
    std::vector<int> low_;
    files::File &calibrations_;
    void (*filename_)(unsigned, char *, unsigned);
    const char *confirm_;
    uint32_t rest_ms_, bias_ms_;
    enum class Capture { idle, heel, toe };
    Capture stage_ = Capture::idle;
    unsigned selected_ = 0, phase_ = 0, first_ = 0;
    uint32_t tick_ = 0;
    int heel_ = 0;
    const char *message_ = nullptr;
};

} // namespace cntrl
