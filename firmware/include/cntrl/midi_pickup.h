#pragma once
#include "midi_packet.h"
#include "midi_state.h"
#include <algorithm>
#include <vector>

namespace cntrl::midi {
// Only live submissions enter this stage.
class Pickup {
public:
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(pickup_configure)
#endif
    void configure(bool enabled, unsigned pairing_ms) {
        enabled_ = enabled; pairing_us_ = pairing_ms * 1000u;
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [](const Entry &e) { return !e.progress && !e.selection; }), entries_.end());
        for (auto &e : entries_) { e.acquired = false; e.previous = -1; e.rearm = true; }
        selectors_.clear();
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(pickup_forget)
#endif
    void forget(unsigned output) {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [output](const Entry &e) { return e.output == output; }), entries_.end());
        selectors_.erase(std::remove_if(selectors_.begin(), selectors_.end(), [output](const Selector &s) { return s.output == output; }), selectors_.end());
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(pickup_forget_source)
#endif
    void forget_source(unsigned input) {
        auto matches = [input](unsigned source) { return source < 256 && source / 16 == input; };
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry &e) {
            return matches(e.source) && !e.progress && !e.selection;
        }), entries_.end());
        for (auto &e : entries_) if (matches(e.source)) e.rearm = true;
        selectors_.erase(std::remove_if(selectors_.begin(), selectors_.end(), [&](const Selector &s) { return matches(s.source); }), selectors_.end());
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(pickup_observe)
#endif
    void observe(const MidiState &state, const void *owner = nullptr) {
        for (auto &e : entries_) {
            int target = destination_value(e, state);
            if (&e != owner && target != e.target) e.acquired = false;
            e.target = target;
        }
    }
    template<class Send>
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(pickup_flush)
#endif
    bool flush(unsigned output, uint32_t now, const MidiState &state, Send send, bool force = false) {
        for (auto &e : entries_) if (e.output == output && e.count && (e.progress || e.selection)) {
            if (!deliver(e, state, send)) return false;
        }
        for (auto &e : entries_) if (e.output == output && e.count && (force || e.progress || e.selection || uint32_t(now - e.since) >= pairing_us_)) {
            if (!deliver(e, state, send)) return false;
        }
        return true;
    }
    template<class Send>
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(pickup_submit)
#endif
    bool submit(unsigned output, const Packet &packet, uint32_t now, const MidiState &state, Send send, uint32_t *transmitted = nullptr) {
        if (!flush(output, now, state, send)) return false;
        if (!enabled_) {
            if (!send(output, packet)) return false;
            if (transmitted) ++*transmitted;
            return true;
        }
        unsigned kind = packet.bytes[0] >> 4, channel = packet.bytes[0] & 15;
        unsigned control = 0, value = 0;
        bool pair = false, low = false;
        if (kind == 11) {
            unsigned cc = packet.bytes[1];
            if (cc == 98 || cc == 99 || cc == 100 || cc == 101) {
                if (!flush(output, now, state, send, true)) return false;
                auto *s = selector(output, packet.source, channel);
                bool nrpn = cc >= 98 && cc <= 99;
                if (s->nrpn != nrpn) { s->high = s->low = -1; s->nrpn = nrpn; }
                if (cc == 99 || cc == 101) s->high = packet.bytes[2]; else s->low = packet.bytes[2];
                if (!send(output, packet)) return false;
                if (transmitted) ++*transmitted;
                observe(state); return true;
            }
            // Bank, switches, data increments and channel-mode messages are deliberate.
            if (!cc || cc == 32 || (cc >= 64 && cc <= 69) || cc == 84 || cc == 88 || (cc >= 96 && cc <= 101) || cc >= 120) {
                if ((cc == 96 || cc == 97 || cc >= 120) && !flush(output, now, state, send, true)) return false;
                if (!send(output, packet)) return false;
                if (transmitted) ++*transmitted;
                observe(state); return true;
            }
            pair = cc < 64; low = cc >= 32 && cc < 64;
            control = pair ? 144 + (cc & 31) : cc;
            if (cc == 6 || cc == 38) {
                auto *s = selector(output, packet.source, channel);
                if (s->high >= 0 && s->low >= 0 && (s->high != 127 || s->low != 127))
                    control = (s->nrpn ? 256 : 33024) + unsigned(s->high * 128 + s->low);
            }
            value = packet.bytes[2];
        } else if (kind == 14) { control = 129; value = packet.bytes[1] | unsigned(packet.bytes[2]) << 7; }
        else if (kind == 13) { control = 132; value = packet.bytes[1]; }
        else if (kind == 10) { control = 65536 + packet.bytes[1]; value = packet.bytes[2]; }
        else {
            if ((kind == 12 || packet.bytes[0] == 0xf0 || packet.bytes[0] == 0xff)
                && !flush(output, now, state, send, true)) return false;
            if (!send(output, packet)) return false;
            if (transmitted) ++*transmitted;
            observe(state); return true;
        }
        auto *e = entry(output, packet.source, channel, control);
        if (e->count && (e->progress || e->selection || !pair || e->low == low || uint32_t(now - e->since) > pairing_us_))
            if (!deliver(*e, state, send)) return false;
        if (!e->count) { e->since = now; e->low = low; }
        e->transmitted = transmitted;
        e->packets[e->count++] = packet;
        e->pair = pair;
        e->sample = int(value);
        if (pair && e->count == 2) {
            const auto &high = e->packets[e->low ? 1 : 0];
            const auto &lsb = e->packets[e->low ? 0 : 1];
            e->sample = int(high.bytes[2] * 128 + lsb.bytes[2]);
        }
        if (!pair || e->count == 2 || !pairing_us_) deliver(*e, state, send);
        return true;
    }
private:
    struct Entry {
        unsigned output, source, channel, control;
        int previous = -1, target = -1, sample = 0;
        Packet packets[2];
        // The queue owner keeps its diagnostic counter alive until these samples
        // are delivered or forgotten, just as it owns its queued MIDI storage.
        uint32_t *transmitted = nullptr;
        uint32_t since = 0;
        unsigned count = 0, progress = 0, selection = 0;
        bool acquired = false, pair = false, low = false, full = false, previous_low = false, rearm = false;
    };
    struct Selector { unsigned output, source, channel; int high = -1, low = -1; bool nrpn = true; };
    std::vector<Entry> entries_;
    std::vector<Selector> selectors_;
    bool enabled_ = true;
    unsigned pairing_us_ = 2000;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(pickup_destination_value)
#endif
    static int destination_value(const Entry &e, const MidiState &state) {
        Parameter parameter{}; parameter.channel = uint8_t(e.channel);
        if (e.control == 129) parameter.kind = Parameter::bend;
        else if (e.control == 132) parameter.kind = Parameter::pressure;
        else if (e.control >= 65536) { parameter.kind = Parameter::key_pressure; parameter.address = uint16_t(e.control - 65536); }
        else if (e.control >= 256) {
            parameter.kind = e.control >= 33024 ? Parameter::rpn14 : Parameter::nrpn14;
            parameter.address = uint16_t(e.control - (e.control >= 33024 ? 33024 : 256));
            if (!e.full && !e.low && parameter.kind == Parameter::nrpn14) parameter.kind = Parameter::nrpn7;
        } else {
            parameter.kind = e.pair && e.full ? Parameter::cc14 : Parameter::cc7;
            parameter.address = uint16_t(parameter.kind == Parameter::cc14 ? e.control - 144 : e.packets[0].bytes[1]);
        }
        int target = state.parameter_value(e.output, parameter);
        if (target == Parameter::unknown) return -1;
        if (parameter.kind == Parameter::bend) return target + 8192;
        if (e.pair && !e.full && (parameter.kind == Parameter::nrpn14 || parameter.kind == Parameter::rpn14))
            return e.low ? target & 127 : target >> 7;
        return target;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(pickup_entry)
#endif
    Entry *entry(unsigned output, unsigned source, unsigned channel, unsigned control) {
        for (auto &e : entries_) if (e.output == output && e.source == source && e.channel == channel && e.control == control) return &e;
        Entry created{}; created.output = output; created.source = source; created.channel = channel; created.control = control;
        entries_.push_back(created); return &entries_.back();
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(pickup_selector)
#endif
    Selector *selector(unsigned output, unsigned source, unsigned channel) {
        for (auto &s : selectors_) if (s.output == output && s.source == source && s.channel == channel) return &s;
        selectors_.push_back({output, source, channel}); return &selectors_.back();
    }
    template<class Send>
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(pickup_deliver)
#endif
    bool deliver(Entry &e, const MidiState &state, Send send) {
        bool full = e.pair && e.count == 2;
        if (!e.progress && !e.selection) {
            if (full != e.full || (e.pair && !full && e.low != e.previous_low)) {
                e.previous = -1; e.acquired = false; e.full = full;
            }
            e.previous_low = e.low;
            int target = destination_value(e, state);
            bool crossed = e.previous >= 0 && ((e.previous <= target && e.sample >= target) || (e.previous >= target && e.sample <= target));
            if (!e.acquired && target >= 0 && e.sample != target && !crossed) {
                e.previous = e.sample; e.count = 0; return true;
            }
        }
        if (e.control >= 256 && e.control < 65536) {
            bool nrpn = e.control < 33024;
            unsigned parameter = e.control - (nrpn ? 256 : 33024);
            unsigned high = nrpn ? 99 : 101, low = nrpn ? 98 : 100;
            while (e.selection < 2) {
                unsigned cc = e.selection ? low : high;
                unsigned data = e.selection ? parameter & 127 : parameter >> 7;
                if (!send(e.output, {11, {uint8_t(0xb0 | e.channel), uint8_t(cc), uint8_t(data)}})) return false;
                if (e.transmitted) ++*e.transmitted;
                ++e.selection;
                observe(state, &e);
            }
        }
        while (e.progress < e.count) {
            if (!send(e.output, e.packets[e.progress])) return false;
            if (e.transmitted) ++*e.transmitted;
            ++e.progress;
            observe(state, &e);
        }
        e.previous = e.sample; e.acquired = true; e.count = e.progress = e.selection = 0;
        if (e.rearm) { e.previous = -1; e.acquired = false; e.rearm = false; }
        return true;
    }
};
} // namespace cntrl::midi
