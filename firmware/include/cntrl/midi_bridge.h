#pragma once
#include "midi_state.h"
#include "midi_packet.h"
#include "midi_pickup.h"
#include "midi_ownership.h"
#include "checked_array.h"
#include "input_notes.h"
#include <cstdint>
#include "midi_outputs.h"
#include <cstring>

#ifndef CNTRL_MIDI_REJECT
#define CNTRL_MIDI_REJECT(route, packet, state, now) ((void)0)
#endif
#ifndef CNTRL_MIDI_ACCEPT
#define CNTRL_MIDI_ACCEPT(route, packet) ((void)0)
#endif

namespace cntrl::midi {
// A raw Patch contains exactly one self-contained MIDI message.
inline bool message_valid(const uint8_t *bytes, unsigned size) {
    if (!size || bytes[0] < 0x80) return false;
    unsigned status = bytes[0], expected = 0;
    if (status < 0xf0) expected = (status >> 4 == 12 || status >> 4 == 13) ? 2 : 3;
    else if (status == 0xf0) {
        if (size < 2 || bytes[size - 1] != 0xf7) return false;
        for (unsigned i = 1; i + 1 < size; ++i) if (bytes[i] & 0x80) return false;
        return true;
    } else if (status == 0xf1 || status == 0xf3) expected = 2;
    else if (status == 0xf2) expected = 3;
    else if (status == 0xf6 || status == 0xf8 || status == 0xfa || status == 0xfb
             || status == 0xfc || status == 0xfe || status == 0xff) expected = 1;
    if (size != expected) return false;
    for (unsigned i = 1; i < size; ++i) if (bytes[i] & 0x80) return false;
    return true;
}
inline bool stream_valid(const uint8_t *bytes, unsigned size) {
    if (!size) return false;
    for (unsigned offset = 0; offset < size;) {
        unsigned status = bytes[offset], n = 1;
        if (status < 0x80) return false;
        if (status < 0xf0) n = (status >> 4 == 12 || status >> 4 == 13) ? 2 : 3;
        else if (status == 0xf0) {
            while (offset + n < size && bytes[offset + n] != 0xf7) ++n;
            ++n;
        } else if (status == 0xf1 || status == 0xf3) n = 2;
        else if (status == 0xf2) n = 3;
        if (n > size - offset || !message_valid(bytes + offset, n)) return false;
        offset += n;
    }
    return true;
}
inline bool batch_valid(const uint8_t *bytes, unsigned size) {
    if (!size || size > 14) return false;
    for (unsigned offset = 0; offset < size;) {
        unsigned kind = bytes[offset] >> 4;
        if (kind < 8 || kind > 14) return false;
        unsigned n = kind == 12 || kind == 13 ? 2 : 3;
        if (n > size - offset) return false;
        for (unsigned i = 1; i < n; ++i) if (bytes[offset + i] & 0x80) return false;
        offset += n;
    }
    return true;
}
class Parser {
public:
    bool sysex() const { return sysex_; }
    bool discarding() const { return dropping_; }
    void reset() { *this = Parser(); }
    void discard_sysex() { reset(); dropping_ = true; }
    bool receive(uint8_t b, Packet &p) {
        if (b >= 0xf8) { p = {15, {b, 0, 0}}; return true; }
        if (dropping_) {
            if (b == 0xf7) { dropping_ = false; return false; }
            if (b < 0x80 || b == 0xf0) return false;
            dropping_ = false;
        }
        if (sysex_) {
            if (b == 0xf7 || b < 0x80) {
                data_[used_++] = b;
                bool end = b == 0xf7;
                if (used_ == 3 || end) {
                    p = {uint8_t(end ? 4 + used_ : 4), {data_[0], data_[1], data_[2]}};
                    used_ = 0; std::memset(data_, 0, 3);
                    if (end) sysex_ = false;
                    return true;
                }
                return false;
            }
            // A new non-real-time status aborts an unterminated stream.
            reset();
        }
        if (b & 0x80) {
            used_ = 0; std::memset(data_, 0, 3);
            if (b == 0xf0) { status_ = 0; sysex_ = true; data_[used_++] = b; return false; }
            if (b == 0xf6) { status_ = 0; p = {5, {b, 0, 0}}; return true; }
            status_ = (b <= 0xef || b == 0xf1 || b == 0xf2 || b == 0xf3) ? b : 0;
            return false;
        }
        if (!status_) return false;
        if (!used_) data_[used_++] = status_;
        data_[used_++] = b;
        unsigned n = (status_ < 0xf0) ? ((status_ >> 4 == 12 || status_ >> 4 == 13) ? 2 : 3)
            : (status_ == 0xf2 ? 3 : 2);
        if (used_ != n) return false;
        uint8_t cin = status_ < 0xf0 ? status_ >> 4 : (n == 3 ? 3 : 2);
        p = {cin, {data_[0], data_[1], data_[2]}};
        used_ = 0; std::memset(data_, 0, 3);
        if (status_ >= 0xf0) status_ = 0;
        return true;
    }
private:
    uint8_t status_ = 0, data_[3] = {}, used_ = 0;
    bool sysex_ = false, dropping_ = false;
};

struct Counters {
    uint32_t received = 0, transmitted = 0, dropped = 0, aborted = 0;
    unsigned high_water = 0;
};

struct NoteRouting {
    virtual ~NoteRouting() = default;
    virtual bool suppress(unsigned output, const Packet &packet) = 0;
    virtual bool prepare(unsigned, const Packet &) { return true; }
    virtual bool prepare_batch(unsigned, const uint8_t *, unsigned, unsigned = 65535) { return true; }
    virtual bool begin_batch(unsigned, unsigned output, const uint8_t *bytes, unsigned size, unsigned source = 65535) { return prepare_batch(output, bytes, size, source); }
    virtual void enter_batch(unsigned) {}
    virtual void leave_batch() {}
    virtual void finish_batch(unsigned) {}
    virtual void accepted(unsigned output, const Packet &packet) = 0;
};

struct Sink {
    virtual ~Sink() = default;
    NoteRouting *note_routing = nullptr;
    bool memory_refused() const { return memory_refused_; }
    bool retain_owner_state(const void *identity, unsigned owner) {
        if (!state_.retain_owner(identity, owner)) return false;
        if (ownership_.retain_source(identity, owner)) return true;
        state_.release_owner(identity); return false;
    }
    void release_owner_state(const void *identity) { state_.release_owner(identity); ownership_.release_identity(identity); }
    void activate_owner_state(const void *identity, unsigned owner) { state_.activate_owner(identity, owner); }
    bool prepare_owner_cycle(const void *identity) { return ownership_.refill_identity(identity) && state_.prepare_owner_cycle(identity); }
    bool reserve_owner_stream(const void *identity, unsigned owner, unsigned output, const uint8_t *bytes, unsigned size, unsigned source = 65535) {
        if (!stream_valid(bytes, size)) return false;
        if (note_routing && !note_routing->prepare_batch(output, bytes, size, source)) return false;
        Parameter parameter;
        const bool logical = parameter.decode_messages(bytes, size);
        Parser parser; Packet packet; unsigned claims = 0;
        for (unsigned i = 0; i < size; ++i) if (parser.receive(bytes[i], packet)) {
            if (!state_.require_packet(identity, owner, output, packet.bytes, packet.size(), !logical)) return false;
            if ((packet.cin == 9 && packet.bytes[2]) || (packet.cin == 11 && packet.bytes[1] >= 64 && packet.bytes[1] <= 69 && packet.bytes[2] >= 64)) ++claims;
        }
        return ownership_.reserve_source(identity, owner, claims, !state_.owner_sealed(identity, owner))
            && (!logical || state_.require_parameter(identity, owner, output, parameter)) && state_.finish_requirements(identity, owner);
    }
    bool prepare_stream(unsigned owner, unsigned output, const uint8_t *bytes, unsigned size, unsigned source = 65535) {
        memory_refused_ = false;
        if (!state_.begin_stream(owner)) { memory_refused_ = true; return false; }
        ownership_.begin_source(state_.active_identity(owner), owner);
        if (!reserve_owner_stream(state_.active_identity(owner), owner, output, bytes, size, source)) { finish_stream(owner); memory_refused_ = true; return false; }
        if (note_routing && !note_routing->begin_batch(owner, output, bytes, size, source)) { finish_stream(owner); memory_refused_ = true; return false; }
        state_.seal_stream(owner); return true;
    }
    void finish_stream(unsigned owner) {
        if (note_routing) note_routing->finish_batch(owner);
        ownership_.finish_source(state_.active_identity(owner), owner); state_.end_stream(owner);
    }
    bool submit_owned(unsigned owner, unsigned output, const Packet &packet, uint32_t *transmitted = nullptr) {
        memory_refused_ = false;
        writing_owner_ = owner; writing_owned_ = true;
        if (note_routing) note_routing->enter_batch(owner);
        const bool accepted = packet.source == 65535 ? submit(output, packet) : submit_live(output, packet, transmitted);
        if (note_routing) note_routing->leave_batch();
        writing_owned_ = false; return accepted;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(sink_submit_arpeggiator)
#endif
    bool submit_arpeggiator(unsigned output, const Packet &packet) {
        memory_refused_ = false;
        if (!pickup_.flush(output,now_,state_,Writer{*this},true)) return false;
        if (!ownership_.submit(output, packet, true, RawWriter{*this, 1}, 65537, &memory_refused_)) return false;
        pickup_.observe(state_); return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(sink_submit)
#endif
    bool submit(unsigned destination, const Packet &packet) {
        memory_refused_ = false;
        const bool release = packet.cin == 8 || (packet.cin == 9 && !packet.bytes[2])
            || (packet.cin == 11 && packet.bytes[1] >= 64 && packet.bytes[1] <= 69 && packet.bytes[2] < 64);
        if (!release && !pickup_.flush(destination, now_, state_, Writer{*this}, true)) return false;
        if (!write(destination, packet)) return false;
        pickup_.observe(state_);
        return true;
    }
    bool reserve_loop_ownership(unsigned keys) {
        if (keys) keys = std::max(keys, loop_keys_);
        if (keys > UINT32_MAX - arpeggiator_keys_ || !ownership_.reserve(keys + arpeggiator_keys_)) return false;
        loop_keys_ = keys; return true;
    }
    bool reserve_playback_state(unsigned keys, unsigned outputs, unsigned channels, unsigned kind = 0) {
        return state_.reserve_playback(keys, outputs, channels, kind);
    }
    unsigned shrink_playback_state(unsigned keys, unsigned kind = 0, unsigned budget = 8) {
        return state_.shrink_playback(keys, kind, budget);
    }
    unsigned reclaim_memory(unsigned budget = 4) {
        unsigned retired = ownership_.reclaim(budget / 2);
        retired += state_.reclaim(budget - retired);
        return retired + pickup_.reclaim(budget - retired);
    }
    virtual unsigned capture_contexts(unsigned selected_inputs, unsigned selected_channels) const {
        unsigned inputs = 0, channels = 0;
        for (unsigned mask = selected_inputs; mask; mask &= mask - 1) ++inputs;
        for (unsigned mask = selected_channels; mask; mask &= mask - 1) ++channels;
        return inputs * 16 * channels;
    }
    bool reserve_arpeggiator_ownership(unsigned keys) {
        if (keys) keys = std::max(keys, arpeggiator_keys_);
        if (keys > UINT32_MAX - loop_keys_ || !ownership_.reserve(keys + loop_keys_)) return false;
        arpeggiator_keys_ = keys; return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(sink_submit_loop)
#endif
    bool submit_loop(unsigned destination, const Packet &packet) {
        memory_refused_ = false;
        if (destination >= 16) return false;
        if (!pickup_.flush(destination, now_, state_, Writer{*this}, true)) return false;
        if (!ownership_.submit(destination, packet, true, RawWriter{*this, 0}, 65536, &memory_refused_)) return false;
        pickup_.observe(state_); return true;
    }
    bool loop_cleaning(unsigned outputs = 0xffff) const { return ownership_.cleaning(outputs); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(sink_release_loop)
#endif
    void release_loop(unsigned available) {
        ownership_.release(available, RawWriter{*this, 0});
        pickup_.observe(state_);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(sink_submit_live)
#endif
    bool submit_live(unsigned destination, const Packet &packet, uint32_t *transmitted = nullptr) {
        memory_refused_ = false;
        const bool release = packet.cin == 8 || (packet.cin == 9 && !packet.bytes[2])
            || (packet.cin == 11 && packet.bytes[1] >= 64 && packet.bytes[1] <= 69 && packet.bytes[2] < 64);
        if (release) {
            if (!write(destination, packet)) return false;
            if (transmitted) ++*transmitted;
            pickup_.observe(state_); return true;
        }
        return pickup_.submit(destination, packet, now_, state_, Writer{*this}, transmitted);
    }
    void pickup(bool enabled, unsigned pairing_ms = 2) { pickup_.configure(enabled, pairing_ms); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(sink_pickup_poll)
#endif
    void pickup_poll(uint32_t now, unsigned outputs, unsigned available = ~0u) {
        now_ = now;
        for (unsigned i = 0; i < outputs; ++i) if (available & (1u << i))
            pickup_.flush(i, now, state_, Writer{*this});
        reclaim_memory();
    }
    void pickup_time(uint32_t now) { now_ = now; }
    void forget_pickup_source(unsigned input) { pickup_.forget_source(input); }
    const MidiState &state() const { return state_; }
    void parameter_observer(ParameterObserver *observer) { observer_ = observer; }
    int parameter_value(unsigned output, const Parameter &parameter) const {
        if (output >= 16 || (parameter_locks_[output].token && parameter_locks_[output].parameter.overlaps(parameter))) return Parameter::unknown;
        return state_.parameter_value(output, parameter);
    }
    bool parameter_locked(unsigned output) const { return output < 16 && parameter_locks_[output].token; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(sink_submit_parameter)
#endif
    bool submit_parameter(unsigned output, ParameterTransaction &transaction) {
        if (output >= 16 || !transaction.token || !transaction.parameter.valid()
            || transaction.position >= transaction.parameter.messages()) return false;
        auto &lock = parameter_locks_[output];
        if (lock.token && lock.token != transaction.token) return false;
        if (!transaction.position && !pickup_.flush(output, now_, state_, Writer{*this}, true)) return false;
        uint8_t bytes[3] = {};
        transaction.parameter.message(transaction.position, bytes);
        Packet packet{uint8_t(bytes[0] >> 4), {bytes[0], bytes[1], bytes[2]}};
        if (!state_.prepare(output, packet.bytes, packet.size(), !transaction.position ? &transaction.parameter : nullptr)) return false;
        if (!transaction.position && observer_ && !observer_->parameter_write(output, bytes, 3, &transaction.parameter, transaction.origin)) { state_.finish_preparation(false); return false; }
        if (!write_packet(output, packet)) { state_.finish_preparation(false); return false; }
        state_.record(output, packet.bytes, packet.size());
        packet_sent(output, packet);
        lock = {transaction.parameter, transaction.token, 0};
        if (++transaction.position == transaction.parameter.messages()) lock.token = 0;
        if (!lock.token) pickup_.observe(state_);
        return true;
    }
    void abandon_parameter(unsigned output, ParameterTransaction &transaction) {
        if (output < 16 && parameter_locks_[output].token == transaction.token) {
            parameter_locks_[output].token = 0;
            state_.forget_parameter(output, transaction.parameter);
            pickup_.observe(state_);
        }
        transaction = {};
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(sink_loop_group)
#endif
    bool submit_loop_group(unsigned output, const Packet &packet, const Parameter &parameter, uint64_t token, bool first, bool last) {
        if (output >= 16 || !token || packet.bytes[0] >> 4 != 11) return false;
        auto &lock = parameter_locks_[output];
        if (lock.token && lock.token != token) return false;
        if (first && !pickup_.flush(output,now_,state_,Writer{*this},true)) return false;
        if (!state_.prepare(output,packet.bytes,packet.size(),first ? &parameter : nullptr, 0)) return false;
        if (first && observer_ && !observer_->parameter_write(output,packet.bytes,packet.size(),&parameter,0)) { state_.finish_preparation(false); return false; }
        if (!write_packet(output,packet)) { state_.finish_preparation(false); return false; }
        state_.record(output,packet.bytes,packet.size()); packet_sent(output,packet);
        lock = {parameter,last ? 0 : token,0};
        if (last) pickup_.observe(state_);
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(sink_abandon_loop_group)
#endif
    void abandon_loop_group(unsigned output, uint64_t token) {
        if (output >= 16 || parameter_locks_[output].token != token) return;
        ParameterTransaction transaction{parameter_locks_[output].parameter,token};
        abandon_parameter(output,transaction);
    }
    void forget_output(unsigned output) {
        if (output < 16) parameter_locks_[output] = {};
        state_.forget(output);
        pickup_.release_output(output, [&](const void *identity) {
            state_.end_identity_stream(identity, pickup_owner); ownership_.finish_source(identity, pickup_owner);
        });
        pickup_.forget(output);
        ownership_.forget(output);
    }
protected:
    virtual void packet_sent(unsigned, const Packet &) {}
    virtual bool write_packet(unsigned destination, const Packet &packet) = 0;
private:
    static constexpr unsigned pickup_owner = UINT32_MAX - 32;
    bool memory_refused_ = false;
    bool writing_owned_ = false;
    unsigned writing_owner_ = 0;
    struct Writer {
        Sink &sink;
        const void *identity = nullptr;
        void context(const void *value) { identity = value; }
        bool prepare(unsigned output, const uint8_t *bytes, unsigned size) {
            sink.ownership_.begin_source(identity, pickup_owner);
            if (!sink.state_.begin_identity_stream(identity, pickup_owner)
                || !sink.reserve_owner_stream(identity, pickup_owner, output, bytes, size)) {
                sink.state_.end_identity_stream(identity, pickup_owner); sink.ownership_.finish_source(identity, pickup_owner);
                sink.memory_refused_ = true; return false;
            }
            return true;
        }
        void finish() { sink.state_.end_identity_stream(identity, pickup_owner); sink.ownership_.finish_source(identity, pickup_owner); }
        bool operator()(unsigned destination, const Packet &packet) const { return sink.write(destination, packet, identity); }
    };
    struct RawWriter {
        Sink &sink;
        unsigned kind = 2;
        const void *identity = nullptr;
        bool operator()(unsigned destination, const Packet &packet) const { return sink.write_raw(destination, packet, identity ? 3 : kind, identity); }
    };
    Pickup pickup_;
    Ownership ownership_;
    unsigned loop_keys_ = 0, arpeggiator_keys_ = 0;
    uint32_t now_ = 0;
    MidiState state_;
    ParameterTransaction parameter_locks_[16] = {};
    ParameterObserver *observer_ = nullptr;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(sink_write)
#endif
    bool write(unsigned destination, const Packet &packet, const void *identity = nullptr) {
        if (note_routing && note_routing->suppress(destination,packet)) return true;
        if (note_routing && !note_routing->prepare(destination,packet)) { memory_refused_ = true; return false; }
        bool accepted = ownership_.submit(destination, packet, false, RawWriter{*this, 2, identity}, 65536, &memory_refused_,
            identity ? identity : writing_owned_ ? state_.active_identity(writing_owner_) : nullptr,
            identity ? pickup_owner : writing_owner_, identity || writing_owned_);
        if (accepted && note_routing) note_routing->accepted(destination,packet);
        return accepted;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(sink_write_raw)
#endif
    bool write_raw(unsigned destination, const Packet &packet, unsigned kind = 2, const void *identity = nullptr) {
        if (destination >= 16) return false;
        bool live_selection = packet.source != 65535 && packet.bytes[0] >> 4 == 11 && packet.bytes[1] >= 98 && packet.bytes[1] <= 101;
        if (parameter_locks_[destination].token && packet.bytes[0] < 0xf8) return false;
        if (!state_.prepare(destination, packet.bytes, packet.size(), nullptr, identity || writing_owned_ ? 3 : kind,
                            identity ? identity : writing_owned_ ? state_.active_identity(writing_owner_) : nullptr,
                            identity ? pickup_owner : writing_owner_)) { memory_refused_ = true; return false; }
        if (observer_ && !live_selection && !observer_->parameter_write(destination, packet.bytes, packet.size(), nullptr, 0)) { state_.finish_preparation(false); return false; }
        if (!write_packet(destination, packet)) { state_.finish_preparation(false); return false; }
        state_.record(destination, packet.bytes, packet.size());
        packet_sent(destination, packet);
        return true;
    }
};

// Route storage follows the supplied endpoint table. One service attempts
// at most one packet per destination. USB framing belongs to the adapter.
class Bridge {
public:
    explicit Bridge(Outputs outputs) : outputs_(outputs),
        device_routes_(outputs.transport_mask(OutputTransport::usb_device)),
        host_routes_(outputs.transport_mask(OutputTransport::usb_host)),
        physical_routes_(outputs.transport_mask(OutputTransport::physical)) { if (memory_budget) prepare(); }
    bool prepare() {
        if (prepared_) return true;
        CheckedArray<Route> candidate; InputNotes notes;
        if (!candidate.resize(outputs_.route_count()) || !notes.configure(outputs_.route_count())) return false;
        routes_.swap(candidate); notes_ = std::move(notes); prepared_ = true; return true;
    }
    unsigned destinations() const { return unsigned(routes_.size()); }
    unsigned busy_outputs() const {
        unsigned mask = 0;
        for (unsigned i = 0; i < destinations(); ++i) {
            const auto &r = routes_[i];
            if (!r.queue.empty() || !r.pedal.empty() || r.wire_sysex || r.terminate) mask |= 1u << i;
        }
        for (const auto &b : owned_)
            if (b.delivery == Delivery::pending) mask |= 1u << b.route;
        return mask;
    }
    enum class Delivery { idle, pending, complete, cancelled };
    bool prepare_owner(unsigned owner) {
        if (find_owner(owner)) return true;
        Owned entry; entry.owner = owner; return owned_.append(std::move(entry)) != nullptr;
    }
    bool retain_owner(unsigned owner) {
        if (!prepare_owner(owner)) return false;
        auto &entry = *find_owner(owner);
        if (entry.reservations == UINT32_MAX) return false;
        entry.retire = false; ++entry.reservations; return true;
    }
    void release_owner(unsigned owner) {
        if (auto *entry = find_owner(owner)) {
            if (entry->reservations) --entry->reservations;
            if (!entry->reservations && entry->leased) entry->retire = true;
        }
        owned_.erase_if([&](const auto &entry) { return entry.owner == owner && !entry.reservations && !entry.leased && entry.delivery != Delivery::pending; });
    }
    Delivery delivery(unsigned owner) const {
        for (const auto &entry : owned_) if (entry.owner == owner) return entry.delivery;
        return Delivery::idle;
    }
    bool owned_send(unsigned owner, unsigned route, const uint8_t *bytes, unsigned size) {
        if (route >= destinations() || !usb_enabled(route) || !batch_valid(bytes, size) || !prepare_owner(owner)) return false;
        auto &b = *find_owner(owner); if (b.delivery == Delivery::pending || b.leased) return false;
        std::memcpy(b.bytes, bytes, size); b.size = size; b.offset = 0;
        b.source = b.bytes; b.sysex = b.packet_ready = false; b.parsed_offset = 0; b.parser.reset();
        b.route = route; b.delivery = Delivery::pending; return true;
    }
    // The caller pins the source until delivery or cancellation. This keeps long
    // SysEx bounded to one USB packet per service tick without copying its data.
    bool owned_stream(unsigned owner, unsigned route, const uint8_t *bytes, unsigned size) {
        if (route >= destinations() || !usb_enabled(route) || !stream_valid(bytes, size) || !prepare_owner(owner)) return false;
        auto &b = *find_owner(owner); if (b.delivery == Delivery::pending || b.leased) return false;
        b.source = bytes; b.size = size; b.offset = 0; b.route = route;
        b.sysex = b.packet_ready = false; b.parsed_offset = 0; b.parser.reset();
        b.delivery = Delivery::pending; return true;
    }
    void cancel_owner(unsigned owner) {
        auto *entry = find_owner(owner); if (!entry) return;
        auto &b = *entry;
        if (b.delivery == Delivery::pending) invalidated_ |= 1u << b.route;
        if (b.delivery == Delivery::pending && b.sysex) routes_[b.route].terminate = true;
        b.delivery = Delivery::cancelled;
    }
    // Only forwarded clock/transport is filtered. Notes, SysEx and other real-time
    // bytes keep their order, including those queued before a source change.
    void clock_owner(unsigned mask) {
        unsigned acquired = mask & ~clock_owner_;
        clock_owner_ = mask;
        for (unsigned i = 0; i < destinations(); ++i) if (acquired & (1u << i)) {
            auto &r = routes_[i];
            r.queue.erase_if([](const auto &entry) { return clock_transport(entry.packet); });
        }
    }
    bool enabled() const { return enabled_; }
    bool input_quarantined(unsigned input, unsigned cable) const {
        return cable < 16 && notes_.quarantined(input, uint16_t(input * 16 + cable));
    }
    bool pedal_quarantined(unsigned route) const { return route < destinations() && routes_[route].pedal_closed; }
    void reset_input(unsigned input) {
        for (const auto &strike : notes_.strikes()) if (strike.input == input && strike.token) {
            if (strike.destination >= 65536) {
                auto &route = routes_[strike.destination - 65536]; bool queued = false;
                for (auto &batch : route.pedal) for (unsigned j = 0; j < batch.notes; ++j) if (batch.tokens[j] == strike.token) {
                    const unsigned offset = batch.offsets[j], kind = batch.bytes[offset] >> 4;
                    const bool on = (kind == 9 && batch.bytes[offset + 2]) || (kind == 11 && batch.bytes[offset + 2] >= 64);
                    queued |= on; batch.skip |= uint16_t(1u << offset);
                }
                if (!queued) {
                    Batch cleanup; cleanup.size = 3; cleanup.source = strike.source;
                    cleanup.bytes[0] = uint8_t((strike.channel & 16 ? 0xb0 : 0x80) | (strike.channel & 15)); cleanup.bytes[1] = strike.pitch;
                    (void)route.pedal.append(std::move(cleanup));
                }
                continue;
            }
            auto &route = routes_[strike.destination];
            bool queued = false;
            for (const auto &entry : route.queue)
                if (entry.token == strike.token && entry.packet.cin == 9 && entry.packet.bytes[2]) queued = true;
            if (queued) route.queue.erase_if([&](const auto &entry) { return entry.token == strike.token; });
            else {
                Packet off{uint8_t(strike.channel & 16 ? 11 : 8), {uint8_t((strike.channel & 16 ? 0xb0 : 0x80) | (strike.channel & 15)), strike.pitch, 0}, strike.source};
                (void)route.queue.append({off, strike.token});
            }
        }
        notes_.forget_input(input);
        for (unsigned index = 0; index < destinations(); ++index) {
            auto &route = routes_[index];
            for (;;) {
                auto onset = std::find_if(route.queue.begin(), route.queue.end(), [&](const auto &entry) {
                    return entry.packet.cin == 9 && entry.packet.bytes[2]
                        && (entry.packet.source == 65535 ? index : entry.packet.source / 16) == input;
                });
                if (onset == route.queue.end()) break;
                const uint64_t token = onset->token;
                route.queue.erase_if([&](const auto &entry) { return entry.token == token; });
            }
            for (auto &batch : route.pedal) if ((batch.source == 65535 ? 65535 : batch.source / 16) == input)
                for (unsigned j = 0; j < batch.notes; ++j) {
                    const unsigned offset = batch.offsets[j], kind = batch.bytes[offset] >> 4;
                    if (!((kind == 9 && batch.bytes[offset + 2]) || (kind == 11 && batch.bytes[offset + 2] >= 64))) continue;
                    for (auto &other : route.pedal) for (unsigned k = 0; k < other.notes; ++k)
                        if (other.tokens[k] == batch.tokens[j]) other.skip |= uint16_t(1u << other.offsets[k]);
                }
            if (input == 65535) route.pedal_closed = false;
        }
    }
    void discard_forwarded(unsigned index) {
        auto &r = routes_[index];
        if (r.parser.sysex() || r.wire_sysex) abort(index);
        const unsigned previous = r.queue.size();
        for (const auto &entry : r.queue) if (entry.packet.cin == 9 && entry.packet.bytes[2]) {
            const auto token = entry.token;
            r.queue.erase_if([token](const auto &queued) {
                return queued.token == token && (queued.packet.cin == 8 || (queued.packet.cin == 9 && !queued.packet.bytes[2]));
            });
            notes_.reject(token);
        }
        r.queue.erase_if([](const auto &entry) {
            return entry.packet.cin != 8 && !(entry.packet.cin == 9 && !entry.packet.bytes[2]);
        });
        r.counts.dropped += previous - r.queue.size();
        for (auto &strike : notes_.strikes()) if (strike.destination == index && strike.token) {
            const uint64_t token = strike.token;
            Packet off{uint8_t(strike.channel & 16 ? 11 : 8), {uint8_t((strike.channel & 16 ? 0xb0 : 0x80) | (strike.channel & 15)), strike.pitch, 0}, strike.source};
            (void)r.queue.append({off, token}); notes_.reject(token);
        }
        r.parser.reset();
    }
    const Counters &counters(unsigned route) const { return routes_[route].counts; }
    bool idle() const {
        for (const auto &b : owned_) if (b.delivery == Delivery::pending) return false;
        for (const auto &r : routes_) if (!r.pedal.empty()) return false;
        return true;
    }
    bool usb_enabled(unsigned route) const {
        return route < destinations() && ((host_routes_ & (1u << route)) ? usb_host_ : (device_routes_ & (1u << route)) ? usb_device_ : true);
    }
    void disconnect_output(unsigned route) {
        reset_input(route);
        invalidated_ |= 1u << route;
        discard_forwarded(route);
        cancel_route(route);
        auto &r = routes_[route];
        r.queue.clear(); notes_.forget_destination(route); notes_.forget_destination(route + 65536);
        r.counts.dropped += r.pedal.size();
        r.pedal.clear(); r.pedal_offset = 0; r.pedal_closed = false;
        r.wire_sysex = r.terminate = false;
    }
    void configure_usb(bool device, bool host, uint32_t now) {
        if (!prepare()) return;
        usb_device_ = device; usb_host_ = host;
        connected(device_present_, now);
        host_connected(host_present_);
        for (unsigned route = 0; route < destinations(); ++route)
            if (!usb_enabled(route)) disconnect_output(route);
    }
    void configure(bool enabled) {
        if (!prepare()) return;
        // Also reset when switching between two enabled setups.
        enabled_ = enabled;
        forwarding_ = enabled && connected_; waiting_ = false;
        for (unsigned i = 0; i < destinations(); ++i)
            if (!(host_routes_ & (1u << i)) && !routes_[i].relay) discard_forwarded(i);
    }
    void connected(bool connected, uint32_t now) {
        if (!prepare()) return;
        device_present_ = connected; connected = connected && usb_device_;
        if (!connected && forwarding_) { forwarding_ = false; waiting_ = false; }
        // Retry interface forwarding without delaying explicit pedal USB output.
        bool started = false;
        if (enabled_ && !forwarding_ && (!waiting_ || uint32_t(now - checked_) >= 2000)) {
            checked_ = now; waiting_ = true;
            started = forwarding_ = connected;
        }
        if (connected_ == connected && !started) return;
        connected_ = connected;
        for (unsigned i = 0; i < destinations(); ++i) if (device_routes_ & (1u << i)) reset_input(i);
        for (unsigned i = 0; i < destinations(); ++i) if (!(host_routes_ & (1u << i))) discard_forwarded(i);
        if (!connected) {
            for (unsigned i = 0; i < destinations(); ++i) if (device_routes_ & (1u << i)) disconnect_output(i);
        }
    }
    void host_connected(bool connected) {
        if (!prepare()) return;
        host_present_ = connected; connected = connected && usb_host_;
        if (host_connected_ == connected) return;
        host_connected_ = connected;
        for (unsigned i = 0; i < destinations(); ++i) if (host_routes_ & (1u << i)) reset_input(i);
        if (!connected) for (unsigned i = 0; i < destinations(); ++i) if (host_routes_ & (1u << i)) disconnect_output(i);
    }
    bool pedal(unsigned route, const uint8_t *bytes, unsigned size, unsigned source = 65535) {
        if (route >= destinations() || !usb_enabled(route) || !batch_valid(bytes, size) || ((device_routes_ & (1u << route)) && !connected_)) return false;
        auto &r = routes_[route];
        const unsigned path = route + 65536, input = source == 65535 ? 65535 : source / 16;
        unsigned onsets = 0;
        for (unsigned offset = 0; offset < size;) {
            const unsigned kind = bytes[offset] >> 4;
            onsets += (kind == 9 && bytes[offset + 2]) || (kind == 11 && bytes[offset + 1] >= 64 && bytes[offset + 1] <= 69 && bytes[offset + 2] >= 64);
            offset += kind == 12 || kind == 13 ? 2 : 3;
        }
        const bool ready = r.pedal.reserve(r.pedal.size() + notes_.outstanding(path) + onsets + 1)
            && notes_.strikes().reserve(notes_.strikes().size() + onsets);
        Batch batch; batch.source = source;
        for (unsigned offset = 0; offset < size;) {
            const unsigned kind = bytes[offset] >> 4, n = kind == 12 || kind == 13 ? 2 : 3;
            Packet packet{uint8_t(kind), {bytes[offset], bytes[offset + 1], uint8_t(n == 3 ? bytes[offset + 2] : 0)}, uint16_t(source)};
            const bool hold = kind == 11 && packet.bytes[1] >= 64 && packet.bytes[1] <= 69;
            const bool on = (kind == 9 && packet.bytes[2]) || (hold && packet.bytes[2] >= 64);
            const bool off = kind == 8 || (kind == 9 && !packet.bytes[2]) || (hold && packet.bytes[2] < 64);
            uint64_t token = 0; bool keep = ready;
            if (on) {
                token = ++note_serial_;
                const auto admission = notes_.admit(input, uint16_t(source), packet, token, ready && !r.pedal_closed, path);
                keep = admission.accepted; if (admission.untracked) r.pedal_closed = true;
            } else if (off) {
                const auto match = notes_.release(input, uint16_t(source), packet, path);
                token = match.token; keep = match.matched ? bool(token) : ready;
            }
            if (keep) {
                if (token) { batch.tokens[batch.notes] = token; batch.offsets[batch.notes++] = uint8_t(batch.size); }
                std::memcpy(batch.bytes + batch.size, bytes + offset, n); batch.size += n;
            }
            offset += n;
        }
        return batch.size && r.pedal.append(std::move(batch));
    }
    // Report physical packet completion even without an available USB receiver.
    bool receive(unsigned route, uint8_t b, uint32_t now, unsigned source = 65535) {
        if (route >= destinations() || (host_routes_ & (1u << route)) || !usb_device_ || !enabled_ || (!forwarding_ && (physical_routes_ & (1u << route)))) return false;
        return receive_byte(route, b, now, forwarding_, source);
    }
    // Hosted-device forwarding is independent of the physical MIDI-interface switch.
    void relay_packet(unsigned route, const Packet &packet, uint32_t now) {
        if (route >= destinations() || !usb_enabled(route) || !connected_ || !host_connected_) return;
        routes_[route].relay = true;
        receive_packet(route, packet, now);
    }
    void forward_packet(unsigned route, const Packet &packet, uint32_t now) {
        if (route >= destinations() || !forwarding_ || (host_routes_ & (1u << route)) || !usb_device_ || !enabled_) return;
        receive_packet(route, packet, now);
    }
private:
    bool receive_byte(unsigned route, uint8_t b, uint32_t now, bool forwarding, unsigned source = 65535) {
        auto &r = routes_[route];
        bool was_sysex = r.parser.sysex();
        // Close an interrupted physical SysEx before forwarding a new status.
        if (was_sysex && b >= 0x80 && b < 0xf7 && b != 0xf0) abort(route);
        if (b == 0xf0 && was_sysex) abort(route);
        Packet p;
        bool complete = r.parser.receive(b, p);
        p.source = uint16_t(source);
        if (b < 0xf8 && (was_sysex || r.parser.sysex())) r.last_sysex = now;
        if (complete && forwarding) enqueue(route, p);
        return complete;
    }
    void receive_packet(unsigned route, const Packet &packet, uint32_t now) {
        if (!valid(packet, routes_[route].parser.sysex() || routes_[route].parser.discarding())) {
            CNTRL_MIDI_REJECT(route, packet, unsigned(routes_[route].parser.sysex())
                | (unsigned(routes_[route].parser.discarding()) << 1), now);
            ++routes_[route].counts.dropped; return;
        }
        CNTRL_MIDI_ACCEPT(route, packet);
        for (unsigned i = 0; i < packet.size(); ++i) receive_byte(route, packet.bytes[i], now, true, packet.source);
    }
public:
    bool handoff(Sink &sink) {
        bool ready = true;
        for (unsigned i = 0; i < destinations(); ++i) {
            if (routes_[i].terminate) terminate(i, sink);
            if (routes_[i].terminate) ready = false;
        }
        return ready;
    }
    bool sysex_ready(unsigned outputs) const {
        for (unsigned i = 0; i < destinations(); ++i)
            if ((outputs & (1u << i)) && (routes_[i].wire_sysex || routes_[i].terminate || routes_[i].pedal_offset)) return false;
        for (const auto &b : owned_)
            if ((outputs & (1u << b.route)) && b.delivery == Delivery::pending && b.offset) return false;
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(bridge_service)
#endif
    void service(Sink &sink, uint32_t now, unsigned reserved_outputs = 0) {
        for (auto &entry : owned_) if (entry.leased && entry.delivery != Delivery::pending) { sink.finish_stream(entry.owner); entry.leased = false; }
        owned_.erase_if([](const auto &entry) { return entry.retire && !entry.leased && !entry.reservations && entry.delivery != Delivery::pending; });
        for (unsigned i = 0; i < destinations(); ++i) if (invalidated_ & (1u << i)) sink.forget_output(i);
        invalidated_ = 0;
        for (unsigned i = 0; i < destinations(); ++i) {
            unsigned index = (cursor_ + i) % destinations();
            auto &r = routes_[index];
            if (r.pedal_lease && (r.pedal.empty() || !r.pedal.front().leased)) { sink.finish_stream(pedal_owner(index)); r.pedal_lease = false; }
            if (r.parser.sysex() && uint32_t(now - r.last_sysex) >= 1000) abort(index);
            if (r.terminate) {
                terminate(index, sink);
                continue;
            }
            bool owned_in_progress = false;
            for (const auto &b : owned_)
                if (b.route == index && b.delivery == Delivery::pending && b.offset) owned_in_progress = true;
            if ((reserved_outputs & (1u << index)) && !r.wire_sysex && !owned_in_progress && !r.pedal_offset) {
                service_realtime(index, sink); continue;
            }
            if ((owned_in_progress || r.pedal_offset) && service_realtime(index, sink)) continue;
            if (!r.wire_sysex && !r.pedal_offset && service_owned(index, sink)) continue;
            // A pedal batch is indivisible relative to ordinary bridge traffic.
            if (!r.pedal.empty() && !r.wire_sysex) {
                auto &b = r.pedal.front();
                const unsigned batch_owner = pedal_owner(index);
                bool release_only = true;
                for (unsigned offset = 0; offset < b.size;) {
                    const unsigned kind = b.bytes[offset] >> 4;
                    if (!(b.skip & (1u << offset))) release_only &= kind == 8 || (kind == 9 && !b.bytes[offset + 2])
                        || (kind == 11 && b.bytes[offset + 1] >= 64 && b.bytes[offset + 1] <= 69 && b.bytes[offset + 2] < 64);
                    offset += kind == 12 || kind == 13 ? 2 : 3;
                }
                if (!release_only && !b.leased && !sink.prepare_stream(batch_owner, index, b.bytes, b.size, b.source)) {
                    for (unsigned offset = 0; offset < b.size;) {
                        const unsigned kind = b.bytes[offset] >> 4;
                        const bool hold = kind == 11 && b.bytes[offset + 1] >= 64 && b.bytes[offset + 1] <= 69;
                        if ((kind == 9 && b.bytes[offset + 2]) || (hold && b.bytes[offset + 2] >= 64)) {
                            for (unsigned j = 0; j < b.notes; ++j) if (b.offsets[j] == offset) {
                                const uint64_t token = b.tokens[j]; notes_.reject(token);
                                for (auto &queued : r.pedal) for (unsigned k = 0; k < queued.notes; ++k)
                                    if (queued.tokens[k] == token) queued.skip |= uint16_t(1u << queued.offsets[k]);
                            }
                        }
                        if (kind != 8 && !(kind == 9 && !b.bytes[offset + 2]) && !(hold && b.bytes[offset + 2] < 64)) b.skip |= uint16_t(1u << offset);
                        offset += kind == 12 || kind == 13 ? 2 : 3;
                    }
                    ++r.counts.dropped; continue;
                }
                if (!release_only) b.leased = r.pedal_lease = true;
                uint8_t status = b.bytes[r.pedal_offset];
                unsigned n = status >> 4 == 12 || status >> 4 == 13 ? 2 : 3;
                Packet p{uint8_t(status >> 4), {status, b.bytes[r.pedal_offset + 1], 0}};
                if (n == 3) p.bytes[2] = b.bytes[r.pedal_offset + 2];
                p.source = uint16_t(b.source);
                const bool skipping = b.skip & (1u << r.pedal_offset);
                if (skipping || (release_only ? sink.submit(index, p) : sink.submit_owned(batch_owner, index, p, &r.counts.transmitted))) {
                    if (!skipping && (release_only || p.source == 65535)) ++r.counts.transmitted;
                    r.pedal_offset += n;
                    if (r.pedal_offset == b.size) {
                        if (b.leased) { sink.finish_stream(batch_owner); r.pedal_lease = false; }
                        r.pedal_offset = 0; r.pedal.erase(r.pedal.begin());
                    }
                }
                continue;
            }
            if (r.queue.empty()) continue;
            const Packet &p = r.queue.front().packet;
            if (!(p.source == 65535 ? sink.submit(index, p) : sink.submit_live(index, p, &r.counts.transmitted))) {
                if (sink.memory_refused() && p.cin == 9 && p.bytes[2]) {
                    const auto token = r.queue.front().token;
                    notes_.reject(token);
                    r.queue.erase_if([token](const auto &entry) { return entry.token == token; });
                    ++r.counts.dropped;
                }
                continue;
            }
            if (p.cin == 4) r.wire_sysex = true;
            else if (p.cin >= 5 && p.cin <= 7 && p.bytes[p.size() - 1] == 0xf7) r.wire_sysex = false;
            if (p.source == 65535) ++r.counts.transmitted;
            r.queue.erase(r.queue.begin());
        }
        if (destinations()) cursor_ = (cursor_ + 1) % destinations();
        unsigned budget = 4;
        for (unsigned i = 0; i < destinations() && budget; ++i) {
            const unsigned index = (cursor_ + i) % destinations();
            auto &route = routes_[index];
            const unsigned needed = route.queue.size() + notes_.outstanding(index);
            if (route.queue.capacity() > needed) budget -= route.queue.trim(std::min(budget, route.queue.capacity() - needed));
            const unsigned pedal_needed = route.pedal.size() + notes_.outstanding(index + 65536);
            if (budget && route.pedal.capacity() > pedal_needed)
                budget -= route.pedal.trim(std::min(budget, route.pedal.capacity() - pedal_needed));
        }
        if (budget) budget -= notes_.strikes().trim(budget);
        if (budget) owned_.trim(budget);
    }
private:
    // Setup owner IDs are 16-bit counts; keep internal route leases separate.
    static constexpr unsigned pedal_owner(unsigned route) { return 0x80000000u | route; }
    unsigned invalidated_ = 0;
    struct Owned {
        unsigned owner = 0;
        unsigned reservations = 0;
        uint8_t bytes[14] = {};
        const uint8_t *source = nullptr;
        unsigned size = 0, offset = 0, route = 0;
        Parser parser;
        Packet packet;
        unsigned parsed_offset = 0;
        bool sysex = false, packet_ready = false, leased = false, retire = false;
        Delivery delivery = Delivery::idle;
    };
    CheckedList<Owned> owned_;
    Owned *find_owner(unsigned owner) {
        for (auto &entry : owned_) if (entry.owner == owner) return &entry;
        return nullptr;
    }
    unsigned owner_cursor_ = 0;
    bool service_realtime(unsigned route, Sink &sink) {
        auto &r = routes_[route];
        for (auto &b : owned_) {
            if (b.route != route || b.delivery != Delivery::pending || b.size != 1 || b.source[0] < 0xf8) continue;
            if (sink.submit(route, {15, {b.source[0], 0, 0}})) { ++r.counts.transmitted; b.delivery = Delivery::complete; }
            return true;
        }
        for (auto i = r.queue.begin(); i != r.queue.end(); ++i) {
            if (i->packet.cin != 15) continue;
            if (!sink.submit(route, i->packet)) return true;
            ++r.counts.transmitted;
            r.queue.erase(i); return true;
        }
        return false;
    }
    void cancel_route(unsigned route) {
        for (auto &b : owned_) if (b.route == route && b.delivery == Delivery::pending) b.delivery = Delivery::cancelled;
    }
    bool service_owned(unsigned route, Sink &sink) {
        if (owned_.empty()) return false;
        int chosen = -1;
        for (unsigned i = 0; i < owned_.size(); ++i) {
            unsigned owner = (owner_cursor_ + i) % owned_.size();
            const auto &b = owned_[owner];
            if (b.route != route || b.delivery != Delivery::pending) continue;
            if (chosen < 0 || b.offset) chosen = int(owner);
            if (b.offset) break;
        }
        if (chosen < 0) return false;
        auto &b = owned_[chosen];
        if (!b.leased && !sink.prepare_stream(b.owner, route, b.source, b.size)) {
            b.delivery = Delivery::cancelled; ++routes_[route].counts.dropped;
            owner_cursor_ = (unsigned(chosen) + 1) % owned_.size(); return true;
        }
        b.leased = true;
        while (!b.packet_ready && b.parsed_offset < b.size)
            b.packet_ready = b.parser.receive(b.source[b.parsed_offset++], b.packet);
        if (!b.packet_ready) { b.delivery = Delivery::cancelled; return true; }
        const Packet &p = b.packet;
        if (sink.submit_owned(b.owner, route, p)) {
            ++routes_[route].counts.transmitted; b.offset = b.parsed_offset; b.packet_ready = false;
            if (p.cin == 4) b.sysex = true;
            else if (p.cin >= 5 && p.cin <= 7 && p.bytes[p.size() - 1] == 0xf7) b.sysex = false;
            if (b.offset == b.size) { sink.finish_stream(b.owner); b.leased = false; b.delivery = Delivery::complete; owner_cursor_ = (unsigned(chosen) + 1) % owned_.size(); }
        }
        return true;
    }
    void terminate(unsigned index, Sink &sink) {
        auto &r = routes_[index];
        if (sink.submit(index, {5, {0xf7, 0, 0}})) r.terminate = r.wire_sysex = false;
    }
    struct Batch {
        uint8_t bytes[14] = {}, offsets[4] = {};
        uint64_t tokens[4] = {};
        unsigned size = 0, source = 65535, notes = 0;
        uint16_t skip = 0;
        bool leased = false;
    };
    struct Queued { Packet packet; uint64_t token = 0; };
    struct Route {
        CheckedList<Queued> queue; CheckedList<Batch> pedal; Parser parser; Counters counts;
        unsigned pedal_offset = 0;
        uint32_t last_sysex = 0;
        bool wire_sysex = false, terminate = false, relay = false, pedal_closed = false, pedal_lease = false;
    };
    Outputs outputs_;
    CheckedArray<Route> routes_;
    InputNotes notes_;
    uint64_t note_serial_ = 0;
    bool prepared_ = false;
    unsigned device_routes_ = 0, host_routes_ = 0, physical_routes_ = 0;
    bool usb_device_ = true, usb_host_ = true, device_present_ = false, host_present_ = false;
    bool enabled_ = false, connected_ = false, host_connected_ = false;
    bool forwarding_ = false, waiting_ = false;
    uint32_t checked_ = 0;
    unsigned cursor_ = 0;
    static bool valid(const Packet &p, bool sysex) {
        unsigned n = p.size();
        if (!n) return false;
        // USB MIDI 1.0 Table 4-1 Note 2 permits any unparsed byte here.
        if (p.cin == 15) return true;
        if (p.cin >= 8 && p.cin <= 14) {
            if (p.bytes[0] >> 4 != p.cin) return false;
        } else if (p.cin == 2) {
            if (p.bytes[0] != 0xf1 && p.bytes[0] != 0xf3) return false;
        } else if (p.cin == 3) {
            if (p.bytes[0] != 0xf2) return false;
        } else if (p.cin == 5 && p.bytes[0] == 0xf6) return true;
        else {
            if (p.cin < 4 || p.cin > 7) return false;
            if (!sysex && p.bytes[0] != 0xf0) return false;
            for (unsigned i = 0; i < n; ++i) {
                if (i == 0 && p.bytes[i] == 0xf0) continue;
                if (i == n - 1 && p.cin != 4) return p.bytes[i] == 0xf7;
                if (p.bytes[i] & 0x80) return false;
            }
            return p.cin == 4;
        }
        for (unsigned i = 1; i < n; ++i) if (p.bytes[i] & 0x80) return false;
        return true;
    }
    unsigned clock_owner_ = 0;
    static bool clock_transport(const Packet &p) {
        return p.cin == 15 && (p.bytes[0] == 0xf8 || (p.bytes[0] >= 0xfa && p.bytes[0] <= 0xfc));
    }
    void enqueue(unsigned index, const Packet &p) {
        if ((clock_owner_ & (1u << index)) && clock_transport(p)) return;
        auto &r = routes_[index]; ++r.counts.received;
        const bool onset = p.cin == 9 && p.bytes[2];
        const bool release = p.cin == 8 || (p.cin == 9 && !p.bytes[2]);
        const unsigned input = p.source == 65535 ? index : p.source / 16;
        uint64_t token = 0;
        if (onset) {
            const unsigned before = r.queue.capacity();
            const bool ready = r.queue.reserve(r.queue.size() + notes_.outstanding(index) + 2);
            token = ++note_serial_;
            if (!notes_.admit(input, p.source, p, token, ready, index).accepted) {
                r.queue.trim(r.queue.capacity() - before); ++r.counts.dropped; return;
            }
        } else if (release) {
            const auto match = notes_.release(input, p.source, p, index);
            if (match.matched && !match.token) return;
            token = match.token;
        }
        const unsigned required = r.queue.size() + notes_.outstanding(index) + 1;
        if ((!token && !r.queue.reserve(required)) || !r.queue.append({p, token})) {
            ++r.counts.dropped;
            if (p.cin >= 4 && p.cin <= 7 && p.bytes[0] != 0xf6) abort(index);
            return;
        }
        r.counts.high_water = std::max(r.counts.high_water, r.queue.size());
    }
    void abort(unsigned index) {
        auto &r = routes_[index];
        const unsigned previous = r.queue.size();
        r.queue.erase_if([](const auto &entry) { return entry.packet.cin >= 4 && entry.packet.cin <= 7 && entry.packet.bytes[0] != 0xf6; });
        r.counts.dropped += previous - r.queue.size(); ++r.counts.aborted;
        r.terminate = r.wire_sysex;
        r.parser.discard_sysex();
    }
};
} // namespace cntrl::midi
