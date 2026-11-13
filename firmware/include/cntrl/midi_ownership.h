#pragma once
#include "checked_list.h"
#include "midi_packet.h"
#include <algorithm>

namespace cntrl::midi {
// Live claims are tracked before a loop is armed. Reserved loop credits cannot
// be consumed by subsequent live input, and releases never acquire nodes.
class Ownership {
public:
    bool retain_source(const void *identity, unsigned owner) {
        if (!reserve_source(identity, owner, 0, true)) return false;
        auto *source = find_source(identity, owner);
        if (source->references == UINT32_MAX) return false;
        ++source->references; return true;
    }
    void release_identity(const void *identity) {
        for (auto &source : sources_) if (source.identity == identity && source.references)
            if (!--source.references) { source.active = false; source.limit = 0; }
    }
    bool refill_identity(const void *identity) {
        for (auto &source : sources_) if (source.identity == identity && !refill_source(identity, source.owner)) return false;
        return true;
    }
    void begin_source(const void *identity, unsigned owner) {
        if (auto *source = find_source(identity, owner)) if (!source->references) source->limit = 0;
    }
    void finish_source(const void *identity, unsigned owner) {
        if (auto *source = find_source(identity, owner)) if (!source->references) release_source(identity, owner);
    }
    bool reserve_source(const void *identity, unsigned owner, unsigned count, bool extend) {
        auto *source = find_source(identity, owner);
        if (!source) { Source value; value.identity = identity; value.owner = owner; source = sources_.append(std::move(value)); }
        if (!source) return false;
        if (!extend) return count <= source->limit;
        if (count > UINT32_MAX - source->limit) return false;
        const unsigned target = source->limit + count, previous = source->keys.capacity();
        if (!source->keys.reserve(target)) return false;
        if (!source->claims.reserve(target)) { source->keys.trim(source->keys.capacity() - previous); return false; }
        source->limit = target; source->active = true; return true;
    }
    bool refill_source(const void *identity, unsigned owner) {
        auto *source = find_source(identity, owner);
        if (!source) return true;
        const unsigned previous = source->keys.capacity();
        if (!source->keys.reserve(source->limit)) return false;
        if (!source->claims.reserve(source->limit)) { source->keys.trim(source->keys.capacity() - previous); return false; }
        source->active = true; return true;
    }
    void release_source(const void *identity, unsigned owner) {
        if (auto *source = find_source(identity, owner)) { source->active = false; source->limit = 0; }
    }
    bool reserve(unsigned capacity) {
        if (!capacity && loop_claims_) return false;
        if (capacity < loop_keys_ || capacity < (loop_claims_ + 1) / 2) return false;
        const unsigned ordinary_keys = keys_.size() - loop_keys_;
        const unsigned ordinary_claims = claims_.size() - loop_claims_;
        if (capacity > (UINT32_MAX - ordinary_claims) / 2 || capacity > UINT32_MAX - ordinary_keys) return false;
        if (!storage(ordinary_keys + capacity, ordinary_claims + 2 * capacity)) return false;
        reserved_ = capacity;
        return true;
    }
    unsigned reclaim(unsigned budget) {
        const unsigned needed_keys = keys_.size() + reserved_ - loop_keys_;
        const unsigned needed_claims = claims_.size() + 2 * reserved_ - loop_claims_;
        unsigned retired = 0;
        if (keys_.capacity() > needed_keys) retired = keys_.trim(std::min(budget, keys_.capacity() - needed_keys));
        if (claims_.capacity() > needed_claims && retired < budget)
            retired += claims_.trim(std::min(budget - retired, claims_.capacity() - needed_claims));
        for (auto i = sources_.begin(); i != sources_.end() && retired < budget;) {
            auto &source = *i;
            if (source.active) { ++i; continue; }
            retired += source.keys.trim(budget - retired);
            if (retired < budget) retired += source.claims.trim(budget - retired);
            if (source.keys.capacity() || source.claims.capacity() || source.keys.loans() || source.claims.loans()) ++i;
            else i = sources_.erase(i);
        }
        if (retired < budget) retired += sources_.trim(budget - retired);
        return retired;
    }
    template<class Send>
    bool submit(unsigned output, const Packet &packet, bool loop, Send send, unsigned owner = loop_source, bool *memory_refused = nullptr,
                const void *identity = nullptr, unsigned prepared_owner = 0, bool protected_source = false) {
        struct ReturnUnused {
            Ownership &ownership;
            ~ReturnUnused() { ownership.keys_.return_reserved(); ownership.claims_.return_reserved(); }
        } return_unused{*this};
        const unsigned kind = packet.bytes[0] >> 4, channel = packet.bytes[0] & 15;
        const bool note = kind == 8 || kind == 9;
        const bool hold = kind == 11 && packet.bytes[1] >= 64 && packet.bytes[1] <= 69;
        if (!note && !hold) {
            if (!send(output, packet)) return false;
            const bool reset = packet.bytes[0] == 0xff;
            if (reset || (kind == 11 && packet.bytes[1] >= 120 && packet.bytes[1] != 122)) {
                for (auto i = keys_.begin(); i != keys_.end();) {
                    const unsigned key = i->id;
                    if ((key >> 13) == output && (reset || (((key >> 9) & 15) == channel
                        && (packet.bytes[1] == 121 ? (key & 511) >= 128 : (key & 511) < 128)))) i = erase_key(i);
                    else ++i;
                }
            }
            return true;
        }
        const unsigned id = (output << 13) | (channel << 9) | (packet.bytes[1] + (hold ? 128 : 0));
        const unsigned source = loop ? owner : packet.source;
        const bool on = note ? kind == 9 && packet.bytes[2] : packet.bytes[2] >= 64;
        auto key = std::find_if(keys_.begin(), keys_.end(), [id](const Key &entry) { return entry.id == id; });
        auto claim = std::find_if(claims_.begin(), claims_.end(), [&](const Claim &entry) { return entry.id == id && entry.source == source; });
        const unsigned count = claim == claims_.end() ? 0 : claim->count;
        if (on) {
            const bool fresh_key = key == keys_.end(), fresh_claim = claim == claims_.end();
            const bool fresh_loop_key = loop && (fresh_key || !key->loop_claims);
            if (fresh_loop_key && loop_keys_ == reserved_) { if (memory_refused) *memory_refused = true; return false; }
            const unsigned spare_keys = loop ? 0 : reserved_ - loop_keys_;
            const unsigned spare_claims = loop ? 0 : 2 * reserved_ - loop_claims_;
            const unsigned need_keys = keys_.size() + unsigned(fresh_key) + spare_keys;
            const unsigned need_claims = claims_.size() + unsigned(fresh_claim) + spare_claims;
            auto *prepared = protected_source ? find_source(identity, prepared_owner) : nullptr;
            const bool ready = protected_source ? prepared
                && (need_keys <= keys_.capacity() || keys_.take_reserved(prepared->keys, need_keys - keys_.capacity()))
                && (need_claims <= claims_.capacity() || claims_.take_reserved(prepared->claims, need_claims - claims_.capacity()))
                : storage(need_keys, need_claims);
            if (!ready) { keys_.return_reserved(); claims_.return_reserved(); if (memory_refused) *memory_refused = true; return false; }
            if (count == UINT32_MAX) return false;
        }
        if (key == keys_.end()) {
            if (!on) return loop || send(output, packet);
            if (!send(output, packet)) return false;
            auto *created = keys_.append({id, 1, unsigned(loop), loop});
            if (!claims_.append({id, source, 1})) return false;
            if (loop) { ++loop_keys_; ++loop_claims_; }
            return created != nullptr;
        }
        unsigned remaining = 0;
        for (const auto &entry : claims_) if (entry.id == id) {
            if (entry.count > UINT32_MAX - remaining) return false;
            remaining += entry.count;
        }
        if (!on && count) remaining -= hold ? count : 1;
        const bool guarded = key->guarded || loop;
        const bool suppress = guarded && (on ? (note ? key->physical != 0 : loop && remaining > count) : remaining || !count);
        if (!suppress) {
            if (!send(output, packet)) return false;
            if (on) { if (note) ++key->physical; else key->physical = 1; }
            else if (key->physical) --key->physical;
            if (guarded && !on && !remaining && key->physical) return false;
            if (!guarded && !on && !key->physical) { erase_key(key); return true; }
        }
        key->guarded = guarded;
        if (on) {
            if (!count) {
                if (!claims_.append({id, source, 1})) return false;
                if (loop) { if (!key->loop_claims++) ++loop_keys_; ++loop_claims_; }
            } else if (note) ++claim->count;
        } else if (count && (hold || !--claim->count)) {
            if (claim->source >= loop_source) { --loop_claims_; if (!--key->loop_claims) --loop_keys_; }
            claims_.erase(claim);
        }
        bool retained = false;
        for (const auto &entry : claims_) if (entry.id == id) retained = true;
        if (!retained) keys_.erase(key);
        return true;
    }
    bool cleaning(unsigned outputs = 0xffff) const {
        for (const auto &claim : claims_)
            if (claim.source == loop_source && (outputs & (1u << (claim.id >> 13)))) return true;
        return false;
    }
    template<class Send> bool release(unsigned available, Send send) {
        for (const auto &claim : claims_) {
            const unsigned output = claim.id >> 13, key = claim.id & 511;
            if (claim.source != loop_source || !(available & (1u << output))) continue;
            Packet packet{uint8_t(key < 128 ? 8 : 11), {uint8_t((key < 128 ? 0x80 : 0xb0) | ((claim.id >> 9) & 15)), uint8_t(key % 128), 0}};
            return submit(output, packet, true, send);
        }
        return !cleaning();
    }
    void forget(unsigned output) {
        for (auto i = keys_.begin(); i != keys_.end();) {
            if ((i->id >> 13) == output) i = erase_key(i); else ++i;
        }
    }
private:
    static constexpr unsigned loop_source = 65536;
    struct Key { unsigned id, physical, loop_claims; bool guarded; };
    struct Claim { unsigned id, source, count; };
    struct Source {
        const void *identity = nullptr; unsigned owner = 0, limit = 0, references = 0; bool active = false;
        CheckedList<Key> keys; CheckedList<Claim> claims;
    };
    CheckedList<Source> sources_;
    Source *find_source(const void *identity, unsigned owner) {
        for (auto &source : sources_) if (source.identity == identity && source.owner == owner) return &source;
        return nullptr;
    }
#if UINTPTR_MAX == UINT32_MAX
    static_assert(CheckedList<Key>::node_size() == 40 && CheckedList<Claim>::node_size() == 36);
#endif
    CheckedList<Key> keys_;
    CheckedList<Claim> claims_;
    unsigned reserved_ = 0, loop_keys_ = 0, loop_claims_ = 0;
    bool storage(unsigned keys, unsigned claims) {
        const unsigned before = keys_.capacity();
        if (!keys_.reserve(keys)) return false;
        if (claims_.reserve(claims)) return true;
        keys_.trim(keys_.capacity() - before); return false;
    }
    typename CheckedList<Key>::iterator erase_key(typename CheckedList<Key>::iterator key) {
        const unsigned id = key->id;
        if (key->loop_claims) --loop_keys_;
        claims_.erase_if([&](const Claim &entry) {
            if (entry.id != id) return false;
            if (entry.source >= loop_source) --loop_claims_;
            return true;
        });
        return keys_.erase(key);
    }
};
} // namespace cntrl::midi
