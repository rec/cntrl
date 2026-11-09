#pragma once
#include "midi_bridge.h"
#include "memory.h"
#include <algorithm>
#include <cstdio>
#include <new>

namespace cntrl::midi {
// Observation owns its bytes; it never retains a transport or recorder buffer.
class Monitor {
public:
    static constexpr unsigned capacity = 128, refresh_ms = 100;
    enum class Direction : uint8_t { incoming, outgoing, both };
    enum Flag : uint8_t { raw = 1, sysex = 2, start = 4, end = 8, running = 16, diagnostic = 32, connected = 64, disconnected = 128 };
    struct Entry {
        uint64_t time = 0;
        uint8_t route = 0, cable = 0, flags = 0, size = 0, bytes[3] = {};
        Direction direction = Direction::incoming;
    };
    bool open(uint32_t now) {
        close();
        if (!buffer_.resize(sizeof(Storage))) return false;
        storage_ = new(buffer_.data) Storage(); previous_ = now;
        elapsed_ = next_ = cursor_ = 0; scan_ = 0; frozen_ = advanced_ = false;
        return true;
    }
    void close() { storage_ = nullptr; buffer_.clear(); }
    bool active() const { return storage_; }
    bool frozen() const { return frozen_; }
    bool advanced() const { return advanced_; }
    uint64_t oldest() const { return next_ > capacity ? next_ - capacity : 0; }
    uint64_t tail() const { return next_; }
    uint64_t cursor() const { return cursor_; }
    uint64_t overwritten() const { return oldest(); }
    unsigned memory_bytes() const { return sizeof(Storage); }
    const Entry *entry(uint64_t sequence) const {
        return storage_ && sequence >= oldest() && sequence < next_ ? &storage_->entries[sequence % capacity] : nullptr;
    }
    void freeze() { frozen_ = !frozen_; cursor_ = next_; advanced_ = false; }
    void browse(bool backward) {
        if (!storage_ || !frozen_ || next_ == oldest()) return;
        if (backward && cursor_ > oldest() + 1) --cursor_;
        if (!backward && cursor_ < next_) ++cursor_;
    }
    void byte(Direction direction, unsigned route, unsigned cable, uint8_t value, uint32_t now, bool diagnostic_traffic = false) {
        if (!storage_) return;
        time(now);
        if (route >= max_routes || cable >= 16) {
            append(direction, route, cable, raw, &value, 1); return;
        }
        Decoder &d = storage_->decoders[slot(direction, route, cable)];
        d.diagnostic_traffic = diagnostic_traffic;
        if (value >= 0xf8) {
            append(direction, route, cable, uint8_t((diagnostic_traffic ? diagnostic : 0) | (value == 0xf9 || value == 0xfd ? raw : 0)), &value, 1);
            return;
        }
        if ((value & 0x80) && !(value == 0xf7 && d.parser.sysex())) {
            if (d.used || d.parser.sysex()) flush(d, direction, route, cable);
            d.parser.reset();
        }
        d.last = now;
        d.bytes[d.used++] = value;
        const bool in_sysex = d.parser.sysex() || value == 0xf0;
        Packet packet;
        if (d.parser.receive(value, packet)) {
            uint8_t flags = diagnostic_traffic ? diagnostic : 0;
            if (in_sysex) flags |= sysex | (packet.bytes[0] == 0xf0 ? start : 0) | (value == 0xf7 ? end : 0);
            else if (d.bytes[0] < 0x80) flags |= running;
            append(direction, route, cable, flags, packet.bytes, packet.size());
            d.used = 0;
            if (packet.bytes[0] >= 0xf0) d.status = 0;
        } else if (value == 0xf4 || value == 0xf5 || value == 0xf7 || (value < 0x80 && !in_sysex && !d.status)) {
            flush(d, direction, route, cable);
        }
        if (value & 0x80) d.status = value < 0xf0 || value == 0xf1 || value == 0xf2 || value == 0xf3 ? value : 0;
    }
    void packet(Direction direction, unsigned route, unsigned cable, const Packet &packet, uint32_t now, bool diagnostic_traffic = false) {
        for (unsigned i = 0; i < packet.size(); ++i) byte(direction, route, cable, packet.bytes[i], now, diagnostic_traffic);
    }
    void connection(unsigned route, bool present, uint64_t identity, uint64_t generation, uint32_t now) {
        if (!storage_ || route >= max_routes) return;
        time(now);
        auto &c = storage_->connections[route];
        if (c.known && (c.present != present || c.identity != identity || c.generation != generation)) {
            for (unsigned direction = 0; direction < 2; ++direction) for (unsigned cable = 0; cable < 16; ++cable) {
                auto &d = storage_->decoders[slot(Direction(direction), route, cable)];
                if (d.used || d.parser.sysex()) flush(d, Direction(direction), route, cable);
                d = Decoder();
            }
            append(Direction::both, route, 0, present ? connected : disconnected, nullptr, 0);
        }
        c = {identity, generation, true, present};
    }
    void poll(uint32_t now) {
        if (!storage_) return;
        time(now);
        // Bound maintenance independently of message rates and endpoint count.
        for (unsigned i = 0; i < 8; ++i) {
            unsigned index = scan_++ % (2 * max_routes * 16);
            auto &d = storage_->decoders[index];
            if ((d.used || d.parser.sysex()) && uint32_t(now - d.last) >= 250000) {
                flush(d, Direction(index / (max_routes * 16)), index / 16 % max_routes, index % 16);
                d = Decoder();
            }
        }
    }
    static void describe(const Entry &e, char *text, unsigned size) {
        if (e.flags & connected) { std::snprintf(text, size, "Connected / changed"); return; }
        if (e.flags & disconnected) { std::snprintf(text, size, "Disconnected"); return; }
        if (e.flags & raw) { std::snprintf(text, size, "%sraw / incomplete", e.flags & sysex ? "SysEx " : ""); return; }
        if (e.flags & sysex) {
            std::snprintf(text, size, "SysEx %s%s %uB", e.flags & start ? "start" : "cont", e.flags & end ? "/end" : "", e.size); return;
        }
        const unsigned channel = (e.bytes[0] & 15) + 1;
        const unsigned a = e.bytes[1], b = e.bytes[2];
        switch (e.bytes[0] >> 4) {
        case 8: std::snprintf(text, size, "Ch%u Off %u v%u", channel, a, b); return;
        case 9: std::snprintf(text, size, "Ch%u On %u v%u", channel, a, b); return;
        case 10: std::snprintf(text, size, "Ch%u Poly %u p%u", channel, a, b); return;
        case 11: std::snprintf(text, size, "Ch%u CC %u = %u", channel, a, b); return;
        case 12: std::snprintf(text, size, "Ch%u Program %u", channel, a + 1); return;
        case 13: std::snprintf(text, size, "Ch%u Pressure %u", channel, a); return;
        case 14: std::snprintf(text, size, "Ch%u Bend %d", channel, int(a | (b << 7)) - 8192); return;
        }
        const char *name = e.bytes[0] == 0xf1 ? "Time code" : e.bytes[0] == 0xf2 ? "Song position" : e.bytes[0] == 0xf3 ? "Song select"
            : e.bytes[0] == 0xf6 ? "Tune request" : e.bytes[0] == 0xf8 ? "Clock" : e.bytes[0] == 0xfa ? "Start"
            : e.bytes[0] == 0xfb ? "Continue" : e.bytes[0] == 0xfc ? "Stop" : e.bytes[0] == 0xfe ? "Active sensing" : "Reset";
        if (e.size > 1) std::snprintf(text, size, "%s %u", name, e.size == 3 ? a | (b << 7) : a);
        else std::snprintf(text, size, "%s", name);
    }
private:
    struct Decoder { Parser parser; uint32_t last = 0; uint8_t bytes[3] = {}, used = 0, status = 0; bool diagnostic_traffic = false; };
    struct Connection { uint64_t identity = 0, generation = 0; bool known = false, present = false; };
    struct Storage { Entry entries[capacity]; Decoder decoders[2 * max_routes * 16]; Connection connections[max_routes]; };
    static_assert(sizeof(Storage) <= 16 * 1024);
    MemoryBuffer buffer_;
    Storage *storage_ = nullptr;
    uint64_t elapsed_ = 0, next_ = 0, cursor_ = 0;
    uint32_t previous_ = 0;
    unsigned scan_ = 0;
    bool frozen_ = false, advanced_ = false;
    static unsigned slot(Direction direction, unsigned route, unsigned cable) { return (unsigned(direction) * max_routes + route) * 16 + cable; }
    void time(uint32_t now) { elapsed_ += uint32_t(now - previous_); previous_ = now; }
    void flush(Decoder &d, Direction direction, unsigned route, unsigned cable) {
        append(direction, route, cable, uint8_t(raw | (d.parser.sysex() ? sysex : 0) | (d.diagnostic_traffic ? diagnostic : 0)), d.bytes, d.used);
        d.used = 0;
    }
    void append(Direction direction, unsigned route, unsigned cable, uint8_t flags, const uint8_t *bytes, unsigned size) {
        Entry &e = storage_->entries[next_ % capacity]; e = Entry();
        e.time = elapsed_; e.direction = direction; e.route = uint8_t(route); e.cable = uint8_t(cable); e.flags = flags; e.size = uint8_t(size);
        if (size) std::memcpy(e.bytes, bytes, size);
        ++next_;
        if (!frozen_) cursor_ = next_;
        else if (next_ > capacity && cursor_ <= oldest()) { cursor_ = oldest() + 1; advanced_ = true; }
    }
};
} // namespace cntrl::midi
