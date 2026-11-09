#pragma once
#include "midi_state.h"
#include "midi_packet.h"
#include "midi_pickup.h"
#include <cstdint>
#include <vector>
#include "midi_outputs.h"
#include <array>
#include <cstring>

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

struct Sink {
    virtual ~Sink() = default;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(sink_submit)
#endif
    bool submit(unsigned destination, const Packet &packet) {
        if (!pickup_.flush(destination, now_, state_, Writer{*this}, true)) return false;
        if (!write(destination, packet)) return false;
        pickup_.observe(state_);
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(sink_submit_live)
#endif
    bool submit_live(unsigned destination, const Packet &packet, uint32_t *transmitted = nullptr) {
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
        if (!transaction.position && observer_ && !observer_->parameter_write(output, bytes, 3, &transaction.parameter, transaction.origin)) return false;
        Packet packet{uint8_t(bytes[0] >> 4), {bytes[0], bytes[1], bytes[2]}};
        if (!write_packet(output, packet)) return false;
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
    void forget_output(unsigned output) {
        if (output < 16) parameter_locks_[output] = {};
        state_.forget(output);
        pickup_.forget(output);
    }
protected:
    virtual void packet_sent(unsigned, const Packet &) {}
    virtual bool write_packet(unsigned destination, const Packet &packet) = 0;
private:
    struct Writer {
        Sink &sink;
        bool operator()(unsigned destination, const Packet &packet) const { return sink.write(destination, packet); }
    };
    Pickup pickup_;
    uint32_t now_ = 0;
    MidiState state_;
    ParameterTransaction parameter_locks_[16] = {};
    ParameterObserver *observer_ = nullptr;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(sink_write)
#endif
    bool write(unsigned destination, const Packet &packet) {
        if (destination >= 16) return false;
        bool live_selection = packet.source != 65535 && packet.bytes[0] >> 4 == 11 && packet.bytes[1] >= 98 && packet.bytes[1] <= 101;
        if (observer_ && !live_selection && !observer_->parameter_write(destination, packet.bytes, packet.size(), nullptr, 0)) return false;
        if (parameter_locks_[destination].token && packet.bytes[0] < 0xf8) return false;
        if (!write_packet(destination, packet)) return false;
        state_.record(destination, packet.bytes, packet.size());
        packet_sent(destination, packet);
        return true;
    }
};

// Route storage follows the supplied endpoint table. One service attempts
// at most one packet per destination. USB framing belongs to the adapter.
class Bridge {
public:
    explicit Bridge(Outputs outputs) : routes_(outputs.route_count()),
        device_routes_(outputs.transport_mask(OutputTransport::usb_device)),
        host_routes_(outputs.transport_mask(OutputTransport::usb_host)),
        physical_routes_(outputs.transport_mask(OutputTransport::physical)) {}
    unsigned destinations() const { return unsigned(routes_.size()); }
    unsigned busy_outputs() const {
        unsigned mask = 0;
        for (unsigned i = 0; i < destinations(); ++i) {
            const auto &r = routes_[i];
            if (r.count || r.pedal_count || r.wire_sysex || r.terminate) mask |= 1u << i;
        }
        for (const auto &b : owned_)
            if (b.delivery == Delivery::pending) mask |= 1u << b.route;
        return mask;
    }
    enum class Delivery { idle, pending, complete, cancelled };
    static constexpr unsigned owners = 12;
    Delivery delivery(unsigned owner) const { return owner < owned_.size() ? owned_[owner].delivery : Delivery::idle; }
    bool owned_send(unsigned owner, unsigned route, const uint8_t *bytes, unsigned size) {
        if (owner >= owned_.size() || route >= destinations() || !usb_enabled(route) || !batch_valid(bytes, size)
            || owned_[owner].delivery == Delivery::pending) return false;
        auto &b = owned_[owner];
        std::memcpy(b.bytes, bytes, size); b.size = size; b.offset = 0;
        b.source = b.bytes; b.sysex = b.packet_ready = false; b.parsed_offset = 0; b.parser.reset();
        b.route = route; b.delivery = Delivery::pending; return true;
    }
    // The caller pins the source until delivery or cancellation. This keeps long
    // SysEx bounded to one USB packet per service tick without copying its data.
    bool owned_stream(unsigned owner, unsigned route, const uint8_t *bytes, unsigned size) {
        if (owner >= owned_.size() || route >= destinations() || !usb_enabled(route) || !stream_valid(bytes, size)
            || owned_[owner].delivery == Delivery::pending) return false;
        auto &b = owned_[owner];
        b.source = bytes; b.size = size; b.offset = 0; b.route = route;
        b.sysex = b.packet_ready = false; b.parsed_offset = 0; b.parser.reset();
        b.delivery = Delivery::pending; return true;
    }
    void cancel_owner(unsigned owner) {
        if (owner >= owned_.size()) return;
        auto &b = owned_[owner];
        if (b.delivery == Delivery::pending) invalidated_ |= 1u << b.route;
        if (b.delivery == Delivery::pending && b.sysex) routes_[b.route].terminate = true;
        b.delivery = Delivery::cancelled;
    }
    static constexpr unsigned capacity = 256, pedal_capacity = 8;
    // Only forwarded clock/transport is filtered. Notes, SysEx and other real-time
    // bytes keep their order, including those queued before a source change.
    void clock_owner(unsigned mask) {
        unsigned acquired = mask & ~clock_owner_;
        clock_owner_ = mask;
        for (unsigned i = 0; i < destinations(); ++i) if (acquired & (1u << i)) {
            auto &r = routes_[i];
            unsigned kept = 0;
            for (unsigned j = 0; j < r.count; ++j) {
                Packet packet = r.queue[(r.head + j) % capacity];
                if (!clock_transport(packet))
                    r.queue[(r.head + kept++) % capacity] = packet;
            }
            r.count = kept;
        }
    }
    bool enabled() const { return enabled_; }
    void discard_forwarded(unsigned index) {
        auto &r = routes_[index];
        if (r.parser.sysex() || r.wire_sysex) abort(index);
        else { r.counts.dropped += r.count; r.count = r.head = 0; }
        r.parser.reset();
    }
    const Counters &counters(unsigned route) const { return routes_[route].counts; }
    bool idle() const {
        for (const auto &b : owned_) if (b.delivery == Delivery::pending) return false;
        for (const auto &r : routes_) if (r.pedal_count) return false;
        return true;
    }
    bool usb_enabled(unsigned route) const {
        return route < destinations() && ((host_routes_ & (1u << route)) ? usb_host_ : (device_routes_ & (1u << route)) ? usb_device_ : true);
    }
    void disconnect_output(unsigned route) {
        invalidated_ |= 1u << route;
        discard_forwarded(route);
        cancel_route(route);
        auto &r = routes_[route];
        r.counts.dropped += r.pedal_count;
        r.pedal_count = r.pedal_head = r.pedal_offset = 0;
        r.wire_sysex = r.terminate = false;
    }
    void configure_usb(bool device, bool host, uint32_t now) {
        usb_device_ = device; usb_host_ = host;
        connected(device_present_, now);
        host_connected(host_present_);
        for (unsigned route = 0; route < destinations(); ++route)
            if (!usb_enabled(route)) disconnect_output(route);
    }
    void configure(bool enabled) {
        // Also reset when switching between two enabled setups.
        enabled_ = enabled;
        forwarding_ = enabled && connected_; waiting_ = false;
        for (unsigned i = 0; i < destinations(); ++i)
            if (!(host_routes_ & (1u << i)) && !routes_[i].relay) discard_forwarded(i);
    }
    void connected(bool connected, uint32_t now) {
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
        for (unsigned i = 0; i < destinations(); ++i) if (!(host_routes_ & (1u << i))) discard_forwarded(i);
        if (!connected) {
            for (unsigned i = 0; i < destinations(); ++i) if (device_routes_ & (1u << i)) disconnect_output(i);
        }
    }
    void host_connected(bool connected) {
        host_present_ = connected; connected = connected && usb_host_;
        if (host_connected_ == connected) return;
        host_connected_ = connected;
        if (!connected) for (unsigned i = 0; i < destinations(); ++i) if (host_routes_ & (1u << i)) disconnect_output(i);
    }
    bool pedal(unsigned route, const uint8_t *bytes, unsigned size, unsigned source = 65535) {
        if (route >= destinations() || !usb_enabled(route) || !batch_valid(bytes, size) || ((device_routes_ & (1u << route)) && !connected_)) return false;
        auto &r = routes_[route];
        if (r.pedal_count == pedal_capacity) return false;
        auto &b = r.pedal[(r.pedal_head + r.pedal_count) % pedal_capacity];
        std::memcpy(b.bytes, bytes, size); b.size = size; b.source = source; ++r.pedal_count;
        return true;
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
            ++routes_[route].counts.dropped; return;
        }
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
        for (unsigned i = 0; i < destinations(); ++i) if (invalidated_ & (1u << i)) sink.forget_output(i);
        invalidated_ = 0;
        for (unsigned i = 0; i < destinations(); ++i) {
            unsigned index = (cursor_ + i) % destinations();
            auto &r = routes_[index];
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
            if (r.pedal_count && !r.wire_sysex) {
                auto &b = r.pedal[r.pedal_head];
                uint8_t status = b.bytes[r.pedal_offset];
                unsigned n = status >> 4 == 12 || status >> 4 == 13 ? 2 : 3;
                Packet p{uint8_t(status >> 4), {status, b.bytes[r.pedal_offset + 1], 0}};
                if (n == 3) p.bytes[2] = b.bytes[r.pedal_offset + 2];
                p.source = uint16_t(b.source);
                if (p.source == 65535 ? sink.submit(index, p) : sink.submit_live(index, p, &r.counts.transmitted)) {
                    if (p.source == 65535) ++r.counts.transmitted;
                    r.pedal_offset += n;
                    if (r.pedal_offset == b.size) {
                        r.pedal_offset = 0; r.pedal_head = (r.pedal_head + 1) % pedal_capacity; --r.pedal_count;
                    }
                }
                continue;
            }
            if (!r.count) continue;
            const Packet &p = r.queue[r.head];
            if (!(p.source == 65535 ? sink.submit(index, p) : sink.submit_live(index, p, &r.counts.transmitted))) continue;
            if (p.cin == 4) r.wire_sysex = true;
            else if (p.cin >= 5 && p.cin <= 7 && p.bytes[p.size() - 1] == 0xf7) r.wire_sysex = false;
            if (p.source == 65535) ++r.counts.transmitted;
            r.head = (r.head + 1) % capacity; --r.count;
        }
        if (destinations()) cursor_ = (cursor_ + 1) % destinations();
    }
private:
    unsigned invalidated_ = 0;
    struct Owned {
        uint8_t bytes[14] = {};
        const uint8_t *source = nullptr;
        unsigned size = 0, offset = 0, route = 0;
        Parser parser;
        Packet packet;
        unsigned parsed_offset = 0;
        bool sysex = false, packet_ready = false;
        Delivery delivery = Delivery::idle;
    };
    std::array<Owned, owners> owned_;
    unsigned owner_cursor_ = 0;
    bool service_realtime(unsigned route, Sink &sink) {
        auto &r = routes_[route];
        for (auto &b : owned_) {
            if (b.route != route || b.delivery != Delivery::pending || b.size != 1 || b.source[0] < 0xf8) continue;
            if (sink.submit(route, {15, {b.source[0], 0, 0}})) { ++r.counts.transmitted; b.delivery = Delivery::complete; }
            return true;
        }
        for (unsigned i = 0; i < r.count; ++i) {
            unsigned index = (r.head + i) % capacity;
            if (r.queue[index].cin != 15) continue;
            if (!sink.submit(route, r.queue[index])) return true;
            ++r.counts.transmitted;
            for (unsigned j = i + 1; j < r.count; ++j)
                r.queue[(r.head + j - 1) % capacity] = r.queue[(r.head + j) % capacity];
            --r.count; return true;
        }
        return false;
    }
    void cancel_route(unsigned route) {
        for (auto &b : owned_) if (b.route == route && b.delivery == Delivery::pending) b.delivery = Delivery::cancelled;
    }
    bool service_owned(unsigned route, Sink &sink) {
        if (owned_.empty()) return false;
        int chosen = -1;
        for (unsigned i = 0; i < owners; ++i) {
            unsigned owner = (owner_cursor_ + i) % owners;
            const auto &b = owned_[owner];
            if (b.route != route || b.delivery != Delivery::pending) continue;
            if (chosen < 0 || b.offset) chosen = int(owner);
            if (b.offset) break;
        }
        if (chosen < 0) return false;
        auto &b = owned_[chosen];
        while (!b.packet_ready && b.parsed_offset < b.size)
            b.packet_ready = b.parser.receive(b.source[b.parsed_offset++], b.packet);
        if (!b.packet_ready) { b.delivery = Delivery::cancelled; return true; }
        const Packet &p = b.packet;
        if (sink.submit(route, p)) {
            ++routes_[route].counts.transmitted; b.offset = b.parsed_offset; b.packet_ready = false;
            if (p.cin == 4) b.sysex = true;
            else if (p.cin >= 5 && p.cin <= 7 && p.bytes[p.size() - 1] == 0xf7) b.sysex = false;
            if (b.offset == b.size) { b.delivery = Delivery::complete; owner_cursor_ = (unsigned(chosen) + 1) % owners; }
        }
        return true;
    }
    void terminate(unsigned index, Sink &sink) {
        auto &r = routes_[index];
        if (sink.submit(index, {5, {0xf7, 0, 0}})) r.terminate = r.wire_sysex = false;
    }
    struct Batch { uint8_t bytes[14] = {}; unsigned size = 0, source = 65535; };
    struct Route {
        Packet queue[capacity]; Batch pedal[pedal_capacity]; Parser parser; Counters counts;
        unsigned head = 0, count = 0, pedal_head = 0, pedal_count = 0, pedal_offset = 0;
        uint32_t last_sysex = 0;
        bool wire_sysex = false, terminate = false, relay = false;
    };
    std::vector<Route> routes_;
    unsigned device_routes_ = 0, host_routes_ = 0, physical_routes_ = 0;
    bool usb_device_ = true, usb_host_ = true, device_present_ = false, host_present_ = false;
    bool enabled_ = false, connected_ = false, host_connected_ = false;
    bool forwarding_ = false, waiting_ = false;
    uint32_t checked_ = 0;
    unsigned cursor_ = 0;
    static bool valid(const Packet &p, bool sysex) {
        unsigned n = p.size();
        if (!n) return false;
        if (p.cin == 15) return p.bytes[0] >= 0xf8;
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
        if (r.count == capacity) {
            ++r.counts.dropped;
            if (p.cin >= 4 && p.cin <= 7 && p.bytes[0] != 0xf6) abort(index);
            return;
        }
        r.queue[(r.head + r.count) % capacity] = p;
        if (++r.count > r.counts.high_water) r.counts.high_water = r.count;
    }
    void abort(unsigned index) {
        auto &r = routes_[index];
        r.counts.dropped += r.count; ++r.counts.aborted;
        r.count = r.head = 0; r.terminate = r.wire_sysex;
        r.parser.discard_sysex();
    }
};
} // namespace cntrl::midi
