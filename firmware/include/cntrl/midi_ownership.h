#pragma once
#include "checked_array.h"
#include "checked_list.h"
#include "midi_packet.h"
#include <algorithm>

namespace cntrl::midi {
// One ledger of accepted output messages. Notes are counted, held pedals are
// switches, and every message is forwarded independently of other sources.
class Ownership {
public:
    bool retain_source(const void *identity, unsigned owner) {
        auto *source = prepare_source(identity,owner);
        if (!source || source->references == UINT32_MAX) return false;
        ++source->references; return true;
    }
    void release_identity(const void *identity) {
        for (auto &source : sources_) if (source.identity == identity && source.references)
            if (!--source.references) { source.active = false; source.limit = source.used; }
    }
    bool refill_identity(const void *identity) {
        for (auto &source : sources_) if (source.identity == identity) source.active = true;
        return storage(claims_.size() + spare());
    }
    void begin_source(const void *identity, unsigned owner) {
        if (auto *source = find_source(identity,owner)) if (!source->references) source->limit = source->used;
    }
    void finish_source(const void *identity, unsigned owner) {
        if (auto *source = find_source(identity,owner)) if (!source->references) release_source(identity,owner);
    }
    bool reserve_source(const void *identity, unsigned owner, unsigned count, bool extend) {
        auto *source = prepare_source(identity,owner);
        if (!source) return false;
        if (!extend) return count <= source->limit;
        const uint64_t needed = uint64_t(claims_.size()) + spare() + count;
        if (count > UINT32_MAX - source->limit || needed > UINT32_MAX || !storage(unsigned(needed))) return false;
        source->limit += count; source->active = true; return true;
    }
    void release_source(const void *identity, unsigned owner) {
        if (auto *source = find_source(identity,owner)) { source->active = false; source->limit = source->used; }
    }
    bool reserve(unsigned capacity) {
        const unsigned used = loop_claims();
        if (capacity < used) return false;
        const unsigned old = reserved_;
        reserved_ = capacity;
        const uint64_t needed = uint64_t(claims_.size()) + spare();
        if (needed <= UINT32_MAX && storage(unsigned(needed))) return true;
        reserved_ = old; return false;
    }
    unsigned reclaim(unsigned budget) {
        unsigned retired = 0;
        for (auto i = sources_.begin(); i != sources_.end() && retired < budget;) {
            if (!i->active && !i->references && !i->used) { i = sources_.erase(i); ++retired; }
            else ++i;
        }
        if (retired < budget) retired += sources_.trim(budget - retired);
        if (retired < budget && claims_.empty() && !spare() && claims_.capacity()) { claims_.release(); ++retired; }
        return retired;
    }
    template<class Send>
    bool submit(unsigned output, const Packet &packet, bool loop, Send send, unsigned owner = loop_source, bool *memory_refused = nullptr,
                const void *identity = nullptr, unsigned prepared_owner = 0, bool protected_source = false) {
        if (output >= 16) return false;
        const unsigned kind = packet.bytes[0] >> 4, channel = packet.bytes[0] & 15;
        const bool note = kind == 8 || kind == 9;
        const bool hold = kind == 11 && packet.bytes[1] >= 64 && packet.bytes[1] <= 69;
        if (!note && !hold) {
            if (!send(output,packet)) return false;
            reset(output,packet); return true;
        }
        const unsigned key = (output << 13) | (channel << 9) | (packet.bytes[1] + (hold ? 128 : 0));
        const unsigned source = loop ? owner : packet.source;
        const unsigned position = find(key,source);
        const bool on = note ? kind == 9 && packet.bytes[2] : packet.bytes[2] >= 64;
        Source *credit = protected_source ? find_source(identity,prepared_owner) : nullptr;
        if (on && position == claims_.size()) {
            unsigned unused = spare();
            const bool loop_credit = loop && loop_claims() < reserved_;
            const bool owner_credit = credit && credit->used < credit->limit;
            if (protected_source && !owner_credit) {
                if (memory_refused) *memory_refused = true;
                return false;
            }
            if ((loop_credit || owner_credit) && unused) --unused;
            const uint64_t needed = uint64_t(claims_.size()) + 1 + unused;
            if (needed > UINT32_MAX || !storage(unsigned(needed))) {
                if (memory_refused) *memory_refused = true;
                return false;
            }
        }
        if (on && note && position < claims_.size() && claims_[position].count == UINT32_MAX) return false;
        if (!send(output,packet)) return false;
        if (on) {
            if (position == claims_.size()) {
                const unsigned reservation = credit && credit->used < credit->limit ? credit->serial : 0;
                if (reservation) ++credit->used;
                (void)claims_.append({key,source,1,reservation});
            } else if (note) ++claims_[position].count;
        } else if (position < claims_.size() && (hold || !--claims_[position].count)) erase(position);
        return true;
    }
    bool cleaning(unsigned outputs = 0xffff, unsigned source = loop_source) const {
        for (const auto &claim : claims_) if (claim.source == source && (outputs & (1u << (claim.key >> 13)))) return true;
        return false;
    }
    bool notes_active(unsigned outputs = 0xffff) const {
        for (const auto &claim : claims_) if ((claim.key & 511) < 128 && (outputs & (1u << (claim.key >> 13)))) return true;
        return false;
    }
    template<class Visit> void each_note(Visit visit) const {
        for (const auto &claim : claims_) if ((claim.key & 511) < 128)
            visit(claim.key >> 13,(claim.key >> 9) & 15,claim.key & 127,claim.count,claim.source);
    }
    template<class Visit> void each_source(Visit visit) const {
        for (unsigned i = 0; i < claims_.size(); ++i) {
            unsigned j = 0;
            for (; j < i && claims_[j].source != claims_[i].source; ++j) {}
            if (j == i) visit(claims_[i].source);
        }
    }
    template<class Send> bool release(unsigned available, Send send, unsigned source = loop_source) {
        for (const auto &claim : claims_) {
            const unsigned output = claim.key >> 13, key = claim.key & 511;
            if (claim.source != source || !(available & (1u << output))) continue;
            Packet packet{uint8_t(key < 128 ? 8 : 11),{uint8_t((key < 128 ? 0x80 : 0xb0) | ((claim.key >> 9) & 15)),uint8_t(key % 128),0}};
            return submit(output,packet,true,send,source);
        }
        return !cleaning(0xffff,source);
    }
    void forget(unsigned output) {
        for (unsigned i = 0; i < claims_.size();) {
            if ((claims_[i].key >> 13) == output) erase(i); else ++i;
        }
    }
    void forget_source(unsigned source, unsigned outputs = 0xffff) {
        for (unsigned i = 0; i < claims_.size();) {
            if (claims_[i].source == source && (outputs & (1u << (claims_[i].key >> 13)))) erase(i); else ++i;
        }
    }
    void reset(unsigned output, const Packet &packet) {
        const unsigned kind = packet.bytes[0] >> 4, channel = packet.bytes[0] & 15;
        const bool all = packet.bytes[0] == 0xff;
        if (!all && (kind != 11 || packet.bytes[1] < 120 || packet.bytes[1] == 122)) return;
        for (unsigned i = 0; i < claims_.size();) {
            const unsigned key = claims_[i].key;
            if ((key >> 13) == output && (all || (((key >> 9) & 15) == channel
                && (packet.bytes[1] == 121 ? (key & 511) >= 128 : (key & 511) < 128)))) erase(i);
            else ++i;
        }
    }
private:
    static constexpr unsigned loop_source = 65536;
    struct Claim { unsigned key, source, count, credit; };
    struct Source {
        const void *identity; unsigned owner, serial, limit = 0, used = 0, references = 0; bool active = false;
    };
    CheckedArray<Claim> claims_;
    CheckedList<Source> sources_;
    unsigned reserved_ = 0, serial_ = 0;
    Source *find_source(const void *identity, unsigned owner) {
        for (auto &source : sources_) if (source.identity == identity && source.owner == owner) return &source;
        return nullptr;
    }
    Source *prepare_source(const void *identity, unsigned owner) {
        if (auto *source = find_source(identity,owner)) return source;
        if (serial_ == UINT32_MAX) return nullptr;
        return sources_.append({identity,owner,++serial_});
    }
    unsigned loop_claims() const {
        unsigned count = 0;
        for (const auto &claim : claims_) if (claim.source == loop_source || claim.source == loop_source + 1) ++count;
        return count;
    }
    unsigned spare() const {
        unsigned count = reserved_ > loop_claims() ? reserved_ - loop_claims() : 0;
        for (const auto &source : sources_) if (source.limit > source.used) {
            if (source.limit - source.used > UINT32_MAX - count) return UINT32_MAX;
            count += source.limit - source.used;
        }
        return count;
    }
    bool storage(unsigned count) {
        if (count <= claims_.capacity()) return true;
        if (count > UINT32_MAX - 7) return false;
        return claims_.reserve((count + 7) & ~7u);
    }
    unsigned find(unsigned key, unsigned source) const {
        unsigned i = 0;
        for (; i < claims_.size(); ++i) if (claims_[i].key == key && claims_[i].source == source) break;
        return i;
    }
    void erase(unsigned position) {
        const unsigned serial = claims_[position].credit;
        if (serial) for (auto &source : sources_) if (source.serial == serial) {
            --source.used;
            if (!source.active && source.limit > source.used) source.limit = source.used;
            break;
        }
        claims_[position] = claims_.back();
        (void)claims_.resize(claims_.size() - 1);
    }
};
} // namespace cntrl::midi
