#pragma once
#include <cstdint>
#include <cstring>

namespace cntrl::midi {
// A complete short message, or up to three bytes of a streamed SysEx.
struct Packet {
    uint8_t cin = 0, bytes[3] = {};
    unsigned size() const {
        static constexpr uint8_t lengths[] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
        return lengths[cin & 15];
    }
    uint32_t packed(unsigned cable = 0) const {
        return cin | (cable << 4) | uint32_t(bytes[0]) << 8
            | uint32_t(bytes[1]) << 16 | uint32_t(bytes[2]) << 24;
    }
};

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
    virtual bool submit(unsigned destination, const Packet &packet) = 0;
};

// Routes 0..5 are physical outputs, 6..7 computer inputs, 8 USB host output.
// All storage is fixed. One service() attempts at most one packet per route.
class Bridge {
public:
    static constexpr unsigned destinations = 9, capacity = 256, pedal_capacity = 8;
    bool enabled() const { return enabled_; }
    const Counters &counters(unsigned route) const { return routes_[route].counts; }
    bool idle() const {
        for (const auto &r : routes_) if (r.pedal_count) return false;
        return true;
    }
    void configure(bool enabled) {
        // Also reset when switching between two enabled setups.
        enabled_ = enabled;
        for (unsigned i = 0; i < 8; ++i) reset_route(i);
    }
    void connected(bool connected) {
        if (connected_ == connected) return;
        connected_ = connected;
        for (unsigned i = 0; i < 8; ++i) reset_route(i);
        if (!connected) {
            for (unsigned i = 6; i < 8; ++i) {
                auto &r = routes_[i];
                r.counts.dropped += r.pedal_count;
                r.pedal_count = r.pedal_head = r.pedal_offset = 0;
                r.wire_sysex = r.terminate = false;
            }
        }
    }
    void host_connected(bool connected) {
        if (host_connected_ == connected) return;
        host_connected_ = connected;
        if (!connected) {
            auto &r = routes_[8];
            r.counts.dropped += r.pedal_count;
            r.pedal_count = r.pedal_head = r.pedal_offset = 0;
        }
    }
    bool pedal(unsigned route, const uint8_t *bytes, unsigned size) {
        if (route >= destinations || !size || size > 14 || (route == 6 && !connected_)) return false;
        for (unsigned offset = 0; offset < size;) {
            unsigned kind = bytes[offset] >> 4;
            if (kind < 8 || kind > 14) return false;
            unsigned n = kind == 12 || kind == 13 ? 2 : 3;
            if (n > size - offset) return false;
            for (unsigned i = 1; i < n; ++i) if (bytes[offset + i] & 0x80) return false;
            offset += n;
        }
        auto &r = routes_[route];
        if (r.pedal_count == pedal_capacity) return false;
        auto &b = r.pedal[(r.pedal_head + r.pedal_count) % pedal_capacity];
        std::memcpy(b.bytes, bytes, size); b.size = size; ++r.pedal_count;
        return true;
    }
    // Report physical packet completion even without an available USB receiver.
    bool receive(unsigned route, uint8_t b, uint32_t now) {
        if (route >= 8 || !enabled_ || (!connected_ && route < 6)) return false;
        auto &r = routes_[route];
        bool was_sysex = r.parser.sysex();
        // Close an interrupted physical SysEx before forwarding a new status.
        if (was_sysex && b >= 0x80 && b < 0xf7 && b != 0xf0) abort(route);
        if (b == 0xf0 && was_sysex) abort(route);
        Packet p;
        bool complete = r.parser.receive(b, p);
        if (b < 0xf8 && (was_sysex || r.parser.sysex())) r.last_sysex = now;
        if (complete && connected_) enqueue(route, p);
        return complete;
    }
    void usb(uint32_t packed, uint32_t now) {
        unsigned route = (packed >> 4) & 15;
        Packet p{uint8_t(packed & 15), {uint8_t(packed >> 8), uint8_t(packed >> 16), uint8_t(packed >> 24)}};
        if (route >= 6 || !enabled_ || !connected_) return;
        if (!valid(p, routes_[route].parser.sysex() || routes_[route].parser.discarding())) {
            ++routes_[route].counts.dropped; return;
        }
        for (unsigned i = 0; i < p.size(); ++i) receive(route, p.bytes[i], now);
    }
    void service(Sink &sink, uint32_t now) {
        for (unsigned i = 0; i < destinations; ++i) {
            unsigned index = (cursor_ + i) % destinations;
            auto &r = routes_[index];
            if (r.parser.sysex() && uint32_t(now - r.last_sysex) >= 1000) abort(index);
            if (r.terminate) {
                if (sink.submit(index, {5, {0xf7, 0, 0}})) r.terminate = r.wire_sysex = false;
                continue;
            }
            // A pedal batch is indivisible relative to ordinary bridge traffic.
            if (r.pedal_count && !r.wire_sysex) {
                auto &b = r.pedal[r.pedal_head];
                uint8_t status = b.bytes[r.pedal_offset];
                unsigned n = status >> 4 == 12 || status >> 4 == 13 ? 2 : 3;
                Packet p{uint8_t(status >> 4), {status, b.bytes[r.pedal_offset + 1], 0}};
                if (n == 3) p.bytes[2] = b.bytes[r.pedal_offset + 2];
                if (sink.submit(index, p)) {
                    ++r.counts.transmitted; r.pedal_offset += n;
                    if (r.pedal_offset == b.size) {
                        r.pedal_offset = 0; r.pedal_head = (r.pedal_head + 1) % pedal_capacity; --r.pedal_count;
                    }
                }
                continue;
            }
            if (!r.count) continue;
            const Packet &p = r.queue[r.head];
            if (!sink.submit(index, p)) continue;
            if (p.cin == 4) r.wire_sysex = true;
            else if (p.cin >= 5 && p.cin <= 7 && p.bytes[p.size() - 1] == 0xf7) r.wire_sysex = false;
            ++r.counts.transmitted;
            r.head = (r.head + 1) % capacity; --r.count;
        }
        cursor_ = (cursor_ + 1) % destinations;
    }
private:
    struct Batch { uint8_t bytes[14] = {}; unsigned size = 0; };
    struct Route {
        Packet queue[capacity]; Batch pedal[pedal_capacity]; Parser parser; Counters counts;
        unsigned head = 0, count = 0, pedal_head = 0, pedal_count = 0, pedal_offset = 0;
        uint32_t last_sysex = 0;
        bool wire_sysex = false, terminate = false;
    } routes_[destinations];
    bool enabled_ = false, connected_ = false, host_connected_ = false;
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
    void enqueue(unsigned index, const Packet &p) {
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
    void reset_route(unsigned index) {
        auto &r = routes_[index];
        if (r.parser.sysex() || r.wire_sysex) abort(index);
        else { r.counts.dropped += r.count; r.count = r.head = 0; }
        r.parser.reset();
    }
};
} // namespace cntrl::midi
