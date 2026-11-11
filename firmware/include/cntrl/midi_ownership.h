#pragma once
#include "memory.h"
#include "midi_packet.h"
#include <new>

namespace cntrl::midi {
// Physical retriggers and logical claims are separate. Ordinary MIDI retains
// its wire behavior until a loop joins a key. Reserve storage before arming.
class Ownership {
public:
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(ownership_reserve)
#endif
    bool reserve(unsigned capacity) {
        if (capacity == capacity_) return true;
        if (cleaning()) return false;
        MemoryBuffer keys, claims;
        if (!keys.resize(capacity * sizeof(Key)) || !claims.resize(capacity * 2 * sizeof(Claim))) return false;
        keys_.swap(keys); claims_.swap(claims);
        capacity_ = capacity; key_count_ = claim_count_ = 0; return true;
    }
    template<class Send>
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_ownership_submit)
#endif
    bool submit(unsigned output, const Packet &packet, bool loop, Send send) {
        if (!capacity_) return !loop && send(output, packet);
        unsigned kind = packet.bytes[0] >> 4, channel = packet.bytes[0] & 15;
        bool note = kind == 8 || kind == 9;
        bool hold = kind == 11 && packet.bytes[1] >= 64 && packet.bytes[1] <= 69;
        if (!note && !hold) {
            if (!send(output, packet)) return false;
            bool reset = packet.bytes[0] == 0xff;
            if (reset || (kind == 11 && packet.bytes[1] >= 120 && packet.bytes[1] != 122)) {
                for (unsigned i = key_count_; i--;) {
                    unsigned key = key_at(i).id;
                    if ((key >> 13) == output && (reset || (((key >> 9) & 15) == channel
                        && (packet.bytes[1] == 121 ? (key & 511) >= 128 : (key & 511) < 128)))) erase_key(i);
                }
            }
            return true;
        }
        unsigned id = (output << 13) | (channel << 9) | (packet.bytes[1] + (hold ? 128 : 0));
        unsigned source = loop ? loop_source : packet.source;
        bool on = note ? kind == 9 && packet.bytes[2] : packet.bytes[2] >= 64;
        unsigned k = 0; while (k < key_count_ && key_at(k).id != id) ++k;
        if (k == key_count_) {
            if (!on) return loop || send(output, packet);
            if (key_count_ == capacity_ || claim_count_ == capacity_ * 2 || !send(output, packet)) return false;
            new (keys_.data + key_count_++ * sizeof(Key)) Key{id,1,loop};
            new (claims_.data + claim_count_++ * sizeof(Claim)) Claim{id,source,1}; return true;
        }
        auto &key = key_at(k);
        unsigned c = 0; while (c < claim_count_ && (claim_at(c).id != id || claim_at(c).source != source)) ++c;
        unsigned count = c == claim_count_ ? 0 : claim_at(c).count;
        if (on && !count && claim_count_ == capacity_ * 2) return false;
        unsigned remaining = 0;
        for (unsigned i = 0; i < claim_count_; ++i) if (claim_at(i).id == id) remaining += claim_at(i).count;
        if (!on && count) remaining -= hold ? count : 1;
        bool guarded = key.guarded || loop;
        bool suppress = guarded && (on ? (note ? key.physical != 0 : loop && remaining > count) : remaining || !count);
        if (!suppress) {
            if (!send(output, packet)) return false;
            if (on) { if (note) ++key.physical; else key.physical = 1; }
            else if (key.physical) --key.physical;
            if (guarded && !on && !remaining && key.physical) return false;
            if (!guarded && !on && !key.physical) { erase_key(k); return true; }
        }
        key.guarded = guarded;
        if (on) {
            if (!count) new (claims_.data + claim_count_++ * sizeof(Claim)) Claim{id,source,1};
            else if (note) ++claim_at(c).count;
        } else if (count && (hold || !--claim_at(c).count)) claim_at(c) = claim_at(--claim_count_);
        bool retained = false;
        for (unsigned i = 0; i < claim_count_; ++i) if (claim_at(i).id == id) retained = true;
        if (!retained) key_at(k) = key_at(--key_count_);
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(ownership_cleaning)
#endif
    bool cleaning(unsigned outputs = 0xffff) const {
        for (unsigned i = 0; i < claim_count_; ++i)
            if (claim_at(i).source == loop_source && (outputs & (1u << (claim_at(i).id >> 13)))) return true;
        return false;
    }
    template<class Send>
    bool release(unsigned available, Send send) {
        for (unsigned i = 0; i < claim_count_; ++i) {
            const auto &claim = claim_at(i); unsigned output = claim.id >> 13, key = claim.id & 511;
            if (claim.source != loop_source || !(available & (1u << output))) continue;
            Packet packet{uint8_t(key < 128 ? 8 : 11), {uint8_t((key < 128 ? 0x80 : 0xb0) | ((claim.id >> 9) & 15)), uint8_t(key % 128), 0}};
            return submit(output, packet, true, send);
        }
        return !cleaning();
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_ownership_forget)
#endif
    void forget(unsigned output) {
        for (unsigned i = key_count_; i--;) if ((key_at(i).id >> 13) == output) erase_key(i);
    }
private:
    static constexpr unsigned loop_source = 65536;
    struct Key { unsigned id, physical; bool guarded; };
    struct Claim { unsigned id, source, count; };
    static_assert(sizeof(Key) == 12 && sizeof(Claim) == 12);
    MemoryBuffer keys_, claims_;
    unsigned capacity_ = 0, key_count_ = 0, claim_count_ = 0;
    Key &key_at(unsigned i) { return reinterpret_cast<Key *>(keys_.data)[i]; }
    const Key &key_at(unsigned i) const { return reinterpret_cast<const Key *>(keys_.data)[i]; }
    Claim &claim_at(unsigned i) { return reinterpret_cast<Claim *>(claims_.data)[i]; }
    const Claim &claim_at(unsigned i) const { return reinterpret_cast<const Claim *>(claims_.data)[i]; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(ownership_erase_key)
#endif
    void erase_key(unsigned k) {
        unsigned id = key_at(k).id;
        for (unsigned i = claim_count_; i--;) if (claim_at(i).id == id) claim_at(i) = claim_at(--claim_count_);
        key_at(k) = key_at(--key_count_);
    }
};
} // namespace cntrl::midi
