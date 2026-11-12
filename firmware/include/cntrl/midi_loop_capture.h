#pragma once
#include "memory.h"
#include "midi_packet.h"
#include <algorithm>
#include <new>

namespace cntrl::midi {
// Capture selectors per physical input/cable/channel, then emit complete,
// self-contained writes. Notes and ordinary CC7 messages need no decoder slot.
class LoopCapture {
public:
    struct Emit {
        void *context;
        bool (*send)(void *, const Packet *, unsigned, uint64_t);
        bool operator()(const Packet *packets,unsigned count,uint64_t tick) const { return send(context,packets,count,tick); }
    };
    static constexpr unsigned state_bytes = 56;
    unsigned discarded = 0;
    bool reserve(unsigned contexts) {
        return contexts >= count_ && contexts <= UINT32_MAX / state_bytes && storage_.resize(contexts * state_bytes);
    }
    void release() { storage_.clear(); count_ = 0; }
    void swap(LoopCapture &other) {
        storage_.swap(other.storage_); std::swap(count_,other.count_);
        std::swap(pairing_,other.pairing_); std::swap(discarded,other.discarded);
    }
    void reset(unsigned pairing_ticks) { count_ = 0; pairing_ = std::max(1u,pairing_ticks); }
    void pairing(unsigned ticks) { pairing_ = std::max(1u,ticks); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(loop_capture_forget_input)
#endif
    void forget_input(unsigned input) {
        for (unsigned i = count_; i--;) if (at(i).source / 16 == input) {
            if (at(i).fine >= 0 || at(i).data >= 0 || at(i).selection_pending) ++discarded;
            at(i) = at(--count_);
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(loop_capture_receive)
#endif
    bool receive(unsigned source, const Packet &packet, uint64_t tick, Emit emit) {
        if (packet.bytes[0] >> 4 != 11) return emit(&packet,1,tick);
        unsigned channel = packet.bytes[0] & 15, cc = packet.bytes[1], value = packet.bytes[2];
        bool selector = cc >= 98 && cc <= 101;
        if (!selector && cc != 6 && cc != 38 && cc != 96 && cc != 97 && (cc == 0 || cc == 32 || cc >= 64)) return emit(&packet,1,tick);
        State *state = nullptr;
        for (unsigned i = 0; i < count_; ++i) if (at(i).source == source && at(i).channel == channel) state = &at(i);
        if (!state) {
            if (count_ == storage_.size / sizeof(State) && !reserve(count_ + 1)) return false;
            state = new (storage_.data + count_++ * sizeof(State)) State;
            state->source = source; state->channel = channel;
        }
        auto &s = *state;
        if (selector) {
            if (!data(s,emit)) return false;
            unsigned family = cc >= 98 && cc <= 99 ? 1 : 2;
            if (family != s.family) { s.high = s.low = -1; s.family = family; }
            if (cc == 99 || cc == 101) s.high = int(value); else s.low = int(value);
            s.selection_pending = true;
            if (s.high == 127 && s.low == 127) { s.family = 0; s.selection_pending = false; }
            return true;
        }
        if (cc == 6 || cc == 38 || cc == 96 || cc == 97) {
            if (!s.family || s.high < 0 || s.low < 0) { ++discarded; return true; }
            if (cc == 6) {
                if (!data(s,emit)) return false;
                s.data = int(value); s.data_time = tick; return true;
            }
            if (cc == 38) {
                if (s.data < 0) { ++discarded; return true; }
                Packet packets[6]; selection(s,packets);
                packets[2] = {11,{uint8_t(0xb0 | channel),6,uint8_t(s.data)}};
                packets[3] = packet; nulls(channel,packets + 4);
                if (!emit(packets,6,tick)) return false;
                s.data = -1; s.selection_pending = false; return true;
            }
            if (!data(s,emit)) return false;
            Packet packets[5]; selection(s,packets); packets[2] = packet; nulls(channel,packets + 3);
            s.selection_pending = false; return emit(packets,5,tick);
        }
        if (cc > 32 && s.fine == int(cc - 32) && tick - s.fine_time <= pairing_) {
            Packet packets[2] = {{11,{uint8_t(0xb0 | channel),uint8_t(s.fine),uint8_t(s.fine_value)}},packet};
            packets[0].source = uint16_t(source);
            s.fine = -1; return emit(packets,2,tick);
        }
        if (cc < 32) {
            if (!fine(s,emit)) return false;
            s.fine = int(cc); s.fine_value = int(value); s.fine_time = tick; return true;
        }
        ++discarded; return true; // An LSB without its captured MSB is incomplete.
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(loop_capture_flush)
#endif
    bool flush(uint64_t tick, bool finish, Emit emit) {
        for (unsigned i = 0; i < count_; ++i) {
            auto &s = at(i);
            if (s.fine >= 0 && (finish || tick - s.fine_time >= pairing_) && !fine(s,emit)) return false;
            if (s.data >= 0 && (finish || tick - s.data_time >= pairing_) && !data(s,emit)) return false;
            if (finish && s.selection_pending) {
                ++discarded; s.selection_pending = false;
                s.family = 0; s.high = s.low = -1;
            }
        }
        return true;
    }
private:
    struct State {
        unsigned source = 0, channel = 0, family = 0;
        int high = -1, low = -1, data = -1, fine = -1, fine_value = 0;
        uint64_t data_time = 0, fine_time = 0;
        bool selection_pending = false;
    };
    static_assert(sizeof(State) == 56);
    MemoryBuffer storage_;
    unsigned count_ = 0, pairing_ = 16;
    State &at(unsigned i) { return reinterpret_cast<State *>(storage_.data)[i]; }
    static void selection(const State &s, Packet *packets) {
        unsigned cc = s.family == 1 ? 99 : 101;
        packets[0] = {11,{uint8_t(0xb0 | s.channel),uint8_t(cc),uint8_t(s.high)}};
        packets[0].source = uint16_t(s.source);
        packets[1] = {11,{uint8_t(0xb0 | s.channel),uint8_t(cc - 1),uint8_t(s.low)}};
    }
    static void nulls(unsigned channel, Packet *packets) {
        packets[0] = {11,{uint8_t(0xb0 | channel),101,127}};
        packets[1] = {11,{uint8_t(0xb0 | channel),100,127}};
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(loop_capture_data)
#endif
    static bool data(State &s, Emit emit) {
        if (s.data < 0) return true;
        Packet packets[5]; selection(s,packets);
        packets[2] = {11,{uint8_t(0xb0 | s.channel),6,uint8_t(s.data)}}; nulls(s.channel,packets + 3);
        if (!emit(packets,5,s.data_time)) return false;
        s.data = -1; s.selection_pending = false; return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(loop_capture_fine)
#endif
    static bool fine(State &s, Emit emit) {
        if (s.fine < 0) return true;
        Packet packet{11,{uint8_t(0xb0 | s.channel),uint8_t(s.fine),uint8_t(s.fine_value)}};
        packet.source = uint16_t(s.source);
        if (!emit(&packet,1,s.fine_time)) return false;
        s.fine = -1; return true;
    }
};
} // namespace cntrl::midi
