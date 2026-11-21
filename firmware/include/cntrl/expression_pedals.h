#pragma once
#include <cntrl/document.h>
#include <cntrl/calibration_file.h>
#include <cntrl/ui/messages.h>
#include <vector>
#include <array>
#include <cstdio>

namespace cntrl {
// The adapter supplies biased measurements; settling never blocks the runtime.
class ExpressionPedals {
public:
    ExpressionPedals(unsigned count, files::FileSystem &filesystem, const char *confirm,
                     uint32_t rest_ms, uint32_t bias_ms)
        : inputs_(count), assignments_(count), mirrors_(count), delivered_(count), low_(count),
          filesystem_(filesystem), calibrations_(filesystem, count), confirm_(confirm),
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
        if (stage_ != Capture::idle) { inputs_[selected_].disarm(); delivered_[selected_] = {-1, -1}; }
        stage_ = Capture::idle;
        message_ = no_message;
    }
    void select(unsigned index) { cancel(); selected_ = index; }
    void calibrate(bool enabled) {
        if (!enabled) { cancel(); return; }
        if (stage_ == Capture::idle) {
            stage_ = Capture::heel;
            message_ = no_message;
            inputs_[selected_].reset();
            return;
        }
        int reading = inputs_[selected_].reading();
        if (reading < 0) { message_ = ui::i18n::Message::no_pedal; return; }
        if (stage_ == Capture::heel) {
            heel_ = reading;
            stage_ = Capture::toe;
            message_ = no_message;
            return;
        }
        cntrl::ExpressionCalibration cal{uint16_t(heel_), uint16_t(reading)};
        if (!cal.valid()) { message_ = ui::i18n::Message::travel_too_short; return; }
        if (!calibration_ready_ || !calibrations_.save(selected_, cal)) { message_ = ui::i18n::Message::save_failed; return; }
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
                delivered_[i] = {-1, -1};
                if (selected_ == i && stage_ != Capture::idle) cancel();
                continue;
            }
            if (phase_ == 0) { sample(i, phase_); continue; }
            if (phase_ == 1) { low_[i] = sample(i, phase_); continue; }
            inputs_[i].sample(low_[i], sample(i, phase_), calibrations_.calibration(i));
            if (inputs_[i].position() < 0) delivered_[i] = {-1, -1};
            if (stage_ != Capture::idle && i == selected_) {
                if (inputs_[i].reading() < 0 && stage_ == Capture::toe) {
                    stage_ = Capture::heel;
                    message_ = ui::i18n::Message::reconnect_heel;
                }
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
    unsigned selected() const { return selected_; }
    unsigned count() const { return inputs_.size(); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(expression_status)
#endif
    void status(const char *label, bool enabled, char (&title)[40], char (&position)[40], char (&prompt)[40]) const {
        using ui::i18n::Message;
        const auto &a = assignments_[selected_];
        if (a.channel) ui::i18n::format(title, sizeof(title), Message::expression_assigned, label, a.controller, a.channel);
        else ui::i18n::format(title, sizeof(title), Message::unassigned_port, label);
        const auto &input = inputs_[selected_];
        if (!enabled) ui::utf8::copy(position, sizeof(position), ui::i18n::text(Message::select_expression_role));
        else if (input.reading() < 0) ui::utf8::copy(position, sizeof(position), ui::i18n::text(Message::no_pedal));
        else if (!calibrations_.calibration(selected_).valid()) ui::i18n::format(position, sizeof(position), Message::raw_uncalibrated, input.reading());
        else ui::i18n::format(position, sizeof(position), Message::calibrated_position, (input.position() * 100 + 511) / 1023);
        if (message_ != no_message) ui::utf8::copy(prompt, sizeof(prompt), ui::i18n::text(message_));
        else if (!enabled) ui::utf8::copy(prompt, sizeof(prompt), ui::i18n::text(Message::select_expression_role));
        else if (!confirm_[0]) prompt[0] = 0;
        else ui::i18n::format(prompt, sizeof(prompt), Message::expression_step, confirm_,
            ui::i18n::text(stage_ == Capture::heel ? Message::heel_down : stage_ == Capture::toe ? Message::toe_down : Message::calibrate));
    }

private:
    struct LegacyRecord {
        uint32_t generation = 0;
        cntrl::ExpressionCalibration cal{};
        static LegacyRecord decode(const uint8_t *bytes) {
            const ExpressionCalibration calibration{u16(bytes + 4), u16(bytes + 6)};
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
    bool calibration_ready_ = false;
    const char *confirm_;
    uint32_t rest_ms_, bias_ms_;
    enum class Capture { idle, heel, toe };
    Capture stage_ = Capture::idle;
    unsigned selected_ = 0, phase_ = 0, first_ = 0;
    uint32_t tick_ = 0;
    int heel_ = 0;
    static constexpr auto no_message = ui::i18n::Message(ui::i18n::message_count);
    ui::i18n::Message message_ = no_message;
};

} // namespace cntrl
