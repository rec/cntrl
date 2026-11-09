#pragma once
#include <Arduino.h>
#include <FlexSerial.h>
#include <cntrl/midi_outputs.h>
#include <optional>

class TrsPorts {
public:
    using Mode = cntrl::midi::TrsMode;
    void configure(const uint8_t *roles) {
        for (unsigned i = 0; i < 4; ++i) {
            const Mode next = Mode(roles[i]);
            if (initialized_ && mode_[i] == next) continue;
            auto &tx = serial_[i];
            pinMode(tip[i], INPUT);
            pinMode(ring[i], INPUT);
            tx.reset();
            mode_[i] = Mode::off;
            if (next == Mode::aux) {
                pinMode(tip[i], INPUT_PULLUP);
                pinMode(ring[i], INPUT_PULLUP);
            } else if (next != Mode::off) {
                const bool reversed = next == Mode::midi_b;
                const uint8_t reference = reversed ? tip[i] : ring[i];
                digitalWriteFast(reference, HIGH);
                pinMode(reference, OUTPUT);
                if (cntrl::midi::midi_output(next)) {
                    tx.emplace(-1, reversed ? ring[i] : tip[i]);
                    tx->begin(31250);
                    if (!*tx) {
                        tx.reset();
                        pinMode(reference, INPUT);
                        pinMode(reversed ? ring[i] : tip[i], INPUT);
                        continue;
                    }
                }
            }
            mode_[i] = next;
        }
        initialized_ = true;
    }
    int expression_pin(unsigned i) const { return mode_[i] == Mode::expression ? tip[i] : -1; }
    Mode mode(unsigned i) const { return mode_[i]; }
    unsigned contacts(unsigned i) const {
        if (mode_[i] != Mode::aux) return 0;
        return (digitalReadFast(tip[i]) == LOW ? 1u : 0u) | (digitalReadFast(ring[i]) == LOW ? 2u : 0u);
    }
    bool idle() const {
        for (const auto &tx : serial_) if (tx && tx->transmitting()) return false;
        return true;
    }
    bool send(unsigned i, const uint8_t *data, unsigned length) {
        auto &tx = serial_[i];
        return tx && unsigned(tx->availableForWrite()) >= length && tx->write(data, length) == length;
    }
private:
    static constexpr uint8_t tip[] = {26, 38, 39, 14}, ring[] = {32, 9, 37, 36};
    std::optional<FlexSerial> serial_[4];
    Mode mode_[4] = {};
    bool initialized_ = false;
};
