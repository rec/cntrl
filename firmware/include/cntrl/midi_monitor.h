#pragma once
#include "midi_bridge.h"
#include "memory.h"
#include "checked_list.h"
#include "ui/messages.h"
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
        active_ = true; previous_ = now;
        elapsed_ = next_ = cursor_ = 0; scan_ = 0; frozen_ = advanced_ = false;
        dropped_ = 0;
        return true;
    }
    void close() { active_ = false; entries_.release(); decoders_.release(); connections_.release(); }
    bool active() const { return active_; }
    bool frozen() const { return frozen_; }
    bool advanced() const { return advanced_; }
    uint64_t oldest() const { return next_ - entries_.size(); }
    uint64_t tail() const { return next_; }
    uint64_t cursor() const { return cursor_; }
    uint64_t overwritten() const { return oldest(); }
    unsigned memory_bytes() const {
        return entries_.memory_bytes() + decoders_.memory_bytes() + connections_.memory_bytes();
    }
    unsigned dropped() const { return dropped_; }
    const Entry *entry(uint64_t sequence) const {
        return active_ && sequence >= oldest() && sequence < next_ ? &entries_[unsigned(sequence - oldest())] : nullptr;
    }
    void freeze() { frozen_ = !frozen_; cursor_ = next_; advanced_ = false; }
    void browse(bool backward) {
        if (!active_ || !frozen_ || next_ == oldest()) return;
        if (backward && cursor_ > oldest() + 1) --cursor_;
        if (!backward && cursor_ < next_) ++cursor_;
    }
    void byte(Direction direction, unsigned route, unsigned cable, uint8_t value, uint32_t now, bool diagnostic_traffic = false) {
        if (!active_) return;
        time(now);
        if (route >= max_routes || cable >= 16) {
            append(direction, route, cable, raw, &value, 1); return;
        }
        if (value >= 0xf8) {
            append(direction, route, cable, uint8_t((diagnostic_traffic ? diagnostic : 0) | (value == 0xf9 || value == 0xfd ? raw : 0)), &value, 1);
            return;
        }
        Context *context = nullptr;
        for (auto &c : decoders_) if (c.direction == direction && c.route == route && c.cable == cable) { context = &c; break; }
        if (!context) context = decoders_.append({direction, route, cable, {}});
        if (!context) { ++dropped_; return; }
        Decoder &d = context->decoder; d.diagnostic_traffic = diagnostic_traffic;
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
        if (!active_ || route >= max_routes) return;
        time(now);
        Connection *connection = nullptr;
        for (auto &c : connections_) if (c.route == route) { connection = &c; break; }
        if (!connection) connection = connections_.append({route, 0, 0, false, false});
        if (!connection) { ++dropped_; return; }
        auto &c = *connection;
        if (c.known && (c.present != present || c.identity != identity || c.generation != generation)) {
            for (auto &context : decoders_) if (context.route == route) {
                auto &d = context.decoder;
                if (d.used || d.parser.sysex()) flush(d, context.direction, route, context.cable);
            }
            decoders_.erase_if([route](const Context &context) { return context.route == route; });
            append(Direction::both, route, 0, present ? connected : disconnected, nullptr, 0);
        }
        c = {route, identity, generation, true, present};
    }
    void poll(uint32_t now) {
        if (!active_) return;
        time(now);
        // Bound maintenance independently of message rates and endpoint count.
        for (unsigned i = 0; i < std::min(8u, decoders_.size()); ++i) {
            auto &context = decoders_[scan_++ % decoders_.size()];
            auto &d = context.decoder;
            if ((d.used || d.parser.sysex()) && uint32_t(now - d.last) >= 250000) {
                flush(d, context.direction, context.route, context.cable);
                d = Decoder();
            }
        }
        decoders_.trim(8);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(monitor_describe)
#endif
    static void describe(const Entry &e, char *text, unsigned size) {
        using ui::i18n::Message;
        if (e.flags & connected) { ui::utf8::copy(text, size, ui::i18n::text(Message::monitor_connected)); return; }
        if (e.flags & disconnected) { ui::utf8::copy(text, size, ui::i18n::text(Message::monitor_disconnected)); return; }
        if (e.flags & raw) { ui::i18n::format(text, size, Message::monitor_incomplete, e.flags & sysex ? "SysEx " : ""); return; }
        if (e.flags & sysex) {
            ui::i18n::format(text, size, Message::monitor_sysex, ui::i18n::text(e.flags & start ? Message::monitor_sysex_start : Message::monitor_sysex_continue), e.flags & end ? ui::i18n::text(Message::monitor_sysex_end) : "", e.size); return;
        }
        const unsigned channel = (e.bytes[0] & 15) + 1;
        const unsigned a = e.bytes[1], b = e.bytes[2];
        switch (e.bytes[0] >> 4) {
        case 8: ui::i18n::format(text, size, Message::monitor_note_off, channel, a, b); return;
        case 9: ui::i18n::format(text, size, Message::monitor_note_on, channel, a, b); return;
        case 10: ui::i18n::format(text, size, Message::monitor_poly_pressure, channel, a, b); return;
        case 11: ui::i18n::format(text, size, Message::monitor_control, channel, a, b); return;
        case 12: ui::i18n::format(text, size, Message::monitor_program, channel, a + 1); return;
        case 13: ui::i18n::format(text, size, Message::monitor_pressure, channel, a); return;
        case 14: ui::i18n::format(text, size, Message::monitor_bend, channel, int(a | (b << 7)) - 8192); return;
        }
        const auto name = e.bytes[0] == 0xf1 ? Message::monitor_time_code : e.bytes[0] == 0xf2 ? Message::monitor_song_position : e.bytes[0] == 0xf3 ? Message::monitor_song_select
            : e.bytes[0] == 0xf6 ? Message::monitor_tune_request : e.bytes[0] == 0xf8 ? Message::monitor_clock : e.bytes[0] == 0xfa ? Message::monitor_start
            : e.bytes[0] == 0xfb ? Message::monitor_continue : e.bytes[0] == 0xfc ? Message::monitor_stop : e.bytes[0] == 0xfe ? Message::monitor_active_sensing : Message::monitor_reset;
        if (e.size > 1) ui::i18n::format(text, size, Message::monitor_system_value, ui::i18n::text(name), e.size == 3 ? a | (b << 7) : a);
        else ui::utf8::copy(text, size, ui::i18n::text(name));
    }
private:
    struct Decoder { Parser parser; uint32_t last = 0; uint8_t bytes[3] = {}, used = 0, status = 0; bool diagnostic_traffic = false; };
    struct Context { Direction direction; unsigned route, cable; Decoder decoder; };
    struct Connection { unsigned route; uint64_t identity = 0, generation = 0; bool known = false, present = false; };
    CheckedList<Entry> entries_;
    CheckedList<Context> decoders_;
    CheckedList<Connection> connections_;
    bool active_ = false;
    uint64_t elapsed_ = 0, next_ = 0, cursor_ = 0;
    uint32_t previous_ = 0;
    unsigned scan_ = 0, dropped_ = 0;
    bool frozen_ = false, advanced_ = false;
    void time(uint32_t now) { elapsed_ += uint32_t(now - previous_); previous_ = now; }
    void flush(Decoder &d, Direction direction, unsigned route, unsigned cable) {
        append(direction, route, cable, uint8_t(raw | (d.parser.sysex() ? sysex : 0) | (d.diagnostic_traffic ? diagnostic : 0)), d.bytes, d.used);
        d.used = 0;
    }
    void append(Direction direction, unsigned route, unsigned cable, uint8_t flags, const uint8_t *bytes, unsigned size) {
        Entry e;
        e.time = elapsed_; e.direction = direction; e.route = uint8_t(route); e.cable = uint8_t(cable); e.flags = flags; e.size = uint8_t(size);
        if (size) std::memcpy(e.bytes, bytes, size);
        if (entries_.size() == capacity) entries_.erase(entries_.begin());
        if (!entries_.append(e)) { ++dropped_; return; }
        ++next_;
        if (!frozen_) cursor_ = next_;
        else if (next_ > capacity && cursor_ <= oldest()) { cursor_ = oldest() + 1; advanced_ = true; }
    }
};
} // namespace cntrl::midi
