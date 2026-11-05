#pragma once
#include <Arduino.h>
#include <SD.h>
#include <cntrl/document.h>
#include <cstdio>

// The TRS role driver owns the ring supply and pin mux. Only probe a tip while
// its committed role is expression. Bias settles between polls, never in a delay.
class ExpressionPedals {
public:
    void load_calibrations() {
        for (unsigned i = 0; i < 4; ++i) {
            char path[40]; filename(i, path);
            auto file = SD.sdfs.open(path, O_RDONLY);
            if (!file) continue;
            for (unsigned slot = 0; slot < 2; ++slot) {
                Record record{};
                if (!file.seekSet(slot * sizeof(record)) || file.read(&record, sizeof(record)) != sizeof(record)) continue;
                if (record.valid() && record.generation > saved_[i].generation) saved_[i] = record;
            }
        }
    }
    void configure(const cntrl::UtilityPageSettings &settings) {
        for (unsigned i = 0; i < 4; ++i) {
            assignments_[i] = settings.expression[i];
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
    void next_jack() { cancel(); selected_ = (selected_ + 1) % 4; }
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

    // Sample all enabled jacks in parallel, with at most four ADC reads per poll.
    template<class TipPin, class Available, class Send>
    void poll(uint32_t now, TipPin tip_pin, Available available, Send send) {
        if (uint32_t(now - tick_) < (phase_ ? 2u : 8u)) return;
        tick_ = now;
        for (unsigned slot = 0; slot < 4; ++slot) {
            const unsigned i = (first_ + slot) % 4;
            const int pin = tip_pin(i);
            if (pin < 0) {
                inputs_[i].reset();
                if (selected_ == i && stage_ != Capture::idle) cancel();
                continue;
            }
            if (phase_ == 0) { pinMode(pin, INPUT_PULLDOWN); continue; }
            if (phase_ == 1) { low_[i] = analogRead(pin); pinMode(pin, INPUT_PULLUP); continue; }
            inputs_[i].sample(low_[i], analogRead(pin), saved_[i].cal);
            pinMode(pin, INPUT);
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
        if (!phase_) first_ = (first_ + 1) % 4;
    }
    unsigned selected() const { return selected_; }
    void status(bool enabled, char (&title)[40], char (&position)[40], char (&prompt)[40]) const {
        const auto &a = assignments_[selected_];
        if (a.channel) std::snprintf(title, sizeof(title), "Jack %u  CC %u Ch %u", selected_ + 1, a.controller, a.channel);
        else std::snprintf(title, sizeof(title), "Jack %u  Unassigned", selected_ + 1);
        const auto &input = inputs_[selected_];
        if (!enabled) std::strcpy(position, "Select expression role");
        else if (input.reading() < 0) std::strcpy(position, "No pedal");
        else if (!saved_[selected_].cal.valid()) std::snprintf(position, sizeof(position), "Raw %d  Uncalibrated", input.reading());
        else std::snprintf(position, sizeof(position), "%d%%  Calibrated", (input.position() * 100 + 511) / 1023);
        std::snprintf(prompt, sizeof(prompt), "%s", message_ ? message_ : !enabled ? "Use TRS Ports" :
            stage_ == Capture::heel ? "Heel down: C" : stage_ == Capture::toe ? "Toe down: C" : "C: Calibrate");
    }
private:
    struct Record {
        uint32_t generation = 0;
        cntrl::ExpressionCalibration cal{};
        uint32_t crc = 0;
        uint32_t checksum() const { return cntrl::checksum(reinterpret_cast<const uint8_t *>(this), 8); }
        bool valid() const { return generation && cal.valid() && crc == checksum(); }
    };
    static_assert(sizeof(Record) == 12);
    static void filename(unsigned jack, char (&path)[40]) {
        std::snprintf(path, sizeof(path), "/cntrl/expression-%u.cal", jack + 1);
    }
    bool save(unsigned jack, cntrl::ExpressionCalibration cal) {
        char path[40]; filename(jack, path);
        auto file = SD.sdfs.open(path, O_CREAT | O_RDWR);
        Record record{saved_[jack].generation + 1, cal};
        record.crc = record.checksum();
        unsigned offset = ((record.generation - 1) & 1) * sizeof(record);
        Record verify{};
        if (!file || !file.seekSet(offset) || file.write(&record, sizeof(record)) != sizeof(record)
            || !file.sync() || !file.seekSet(offset) || file.read(&verify, sizeof(verify)) != sizeof(verify)
            || std::memcmp(&record, &verify, sizeof(record))) return false;
        saved_[jack] = record;
        return true;
    }
    cntrl::ExpressionInput inputs_[4];
    cntrl::ExpressionAssignment assignments_[4];
    Record saved_[4];
    enum class Capture { idle, heel, toe };
    Capture stage_ = Capture::idle;
    unsigned selected_ = 0, phase_ = 0, first_ = 0;
    uint32_t tick_ = 0;
    int heel_ = 0, low_[4] = {};
    const char *message_ = nullptr;
};
