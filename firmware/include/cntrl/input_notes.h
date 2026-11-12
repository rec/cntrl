#pragma once
#include "checked_array.h"
#include "checked_list.h"
#include "midi_packet.h"
#include <algorithm>

namespace cntrl::midi {
// FIFO pairing includes refused strikes. Physical input/cable quarantine is
// allocated from the configured input domain before accepting any note.
class InputNotes {
public:
    struct Strike {
        uint64_t token = 0;
        unsigned input = 0;
        uint8_t channel = 0, pitch = 0;
        uint16_t source = 65535;
        unsigned refused = 0;
        unsigned destination = 0;
    };
    struct Admission { bool accepted = false, untracked = false; };
    struct Release { bool matched = false; uint64_t token = 0; };
    bool configure(unsigned inputs) {
        CheckedArray<uint16_t> candidate;
        if (!candidate.resize(inputs)) return false;
        for (unsigned i = 0; i < std::min(inputs, blocked_.size()); ++i) candidate[i] = blocked_[i];
        blocked_.swap(candidate); return true;
    }
    Admission admit(unsigned input, uint16_t source, const Packet &packet, uint64_t token, bool ready, unsigned destination = 0) {
        Strike *last = nullptr;
        for (auto &strike : strikes_) if (matches(strike, input, source, packet, destination)) last = &strike;
        if (last && last->refused) return refuse(input, source, last);
        if (quarantined(input, source)) return {};
        if (ready && token && strikes_.append({token, input, key_channel(packet), packet.bytes[1], source, 0, destination})) return {true, false};
        return refuse(input, source, last);
    }
    Release release(unsigned input, uint16_t source, const Packet &packet, unsigned destination = 0) {
        for (auto i = strikes_.begin(); i != strikes_.end(); ++i) {
            if (!matches(*i, input, source, packet, destination)) continue;
            if (i->token) {
                const uint64_t token = i->token; i->token = 0;
                if (!i->refused) strikes_.erase(i);
                return {true, token};
            }
            if (i->refused && !--i->refused) strikes_.erase(i);
            return {true, 0};
        }
        return {quarantined(input, source), 0};
    }
    bool reject(uint64_t token) {
        for (auto &strike : strikes_) if (strike.token == token) {
            strike.token = 0;
            if (strike.refused != UINT32_MAX) ++strike.refused;
            else quarantine(strike.input, strike.source);
            return true;
        }
        return false;
    }
    bool quarantined(unsigned input, uint16_t source) const {
        return input < blocked_.size() && (blocked_[input] & (1u << cable(source)));
    }
    void forget_input(unsigned input) {
        strikes_.erase_if([input](const auto &strike) { return strike.input == input; });
        if (input < blocked_.size()) blocked_[input] = 0;
    }
    void forget_destination(unsigned destination) {
        strikes_.erase_if([destination](const auto &strike) { return strike.destination == destination; });
    }
    void clear() {
        strikes_.clear();
        for (auto &mask : blocked_) mask = 0;
    }
    unsigned outstanding(unsigned destination = UINT32_MAX) const {
        unsigned count = 0;
        for (const auto &strike : strikes_) count += bool(strike.token) && (destination == UINT32_MAX || strike.destination == destination);
        return count;
    }
    unsigned memory_bytes() const { return strikes_.memory_bytes() + blocked_.memory_bytes(); }
    CheckedList<Strike> &strikes() { return strikes_; }
    const CheckedList<Strike> &strikes() const { return strikes_; }
private:
    CheckedList<Strike> strikes_;
    CheckedArray<uint16_t> blocked_;
    static unsigned cable(uint16_t source) { return source == 65535 ? 0 : source & 15; }
    static uint8_t key_channel(const Packet &packet) { return uint8_t((packet.bytes[0] & 15) | (packet.cin == 11 ? 16 : 0)); }
    static bool matches(const Strike &strike, unsigned input, uint16_t source, const Packet &packet, unsigned destination) {
        return strike.input == input && strike.source == source && strike.channel == key_channel(packet) && strike.pitch == packet.bytes[1] && strike.destination == destination;
    }
    bool quarantine(unsigned input, uint16_t source) {
        if (input >= blocked_.size()) return false;
        blocked_[input] |= uint16_t(1u << cable(source)); return true;
    }
    Admission refuse(unsigned input, uint16_t source, Strike *last) {
        if (last && last->refused != UINT32_MAX) { ++last->refused; return {}; }
        return {false, !quarantine(input, source)};
    }
};
}
