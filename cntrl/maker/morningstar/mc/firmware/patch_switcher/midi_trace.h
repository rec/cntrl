#pragma once
#include <cntrl/midi_packet.h>
#include <cntrl/usb_frame.h>
#include <cstdint>
#include <cstdio>

#ifndef CNTRL_MIDI_TRACE_COLD
#define CNTRL_MIDI_TRACE_COLD
#endif

namespace cntrl::device {
struct MidiBytes {
    uint32_t count = 0, checksum = 0xffffffff;
    void add(const uint8_t *bytes, unsigned size) {
        count += size;
        for (unsigned i = 0; i < size; ++i)
            checksum = (checksum >> 8) ^ usb_crc_table[(checksum ^ bytes[i]) & 255];
    }
};

// Diagnostic working state, not a performance queue. Keep the first rejected
// event and count all rejections; a count above one explicitly exceeds this
// capture's event detail. No allocation or serial output occurs while recording.
struct MidiTrace {
    MidiBytes usb_raw, usb_valid, din_tx, din_rx, usb_return;
    midi::Packet rejected{};
    uint32_t rejects = 0, rejected_route = 0, rejected_state = 0, rejected_ms = 0;
    uint32_t rejected_offset = 0, rejected_valid_offset = 0;
    uint32_t last_poll = 0, last_drops = 0, last_overruns = 0;
    uint32_t drop_gap_us = 0, drop_available = 0, drop_delta = 0;
    uint32_t overrun_gap_us = 0, overrun_available = 0, overrun_delta = 0;
    bool active = false;

    void reset(uint32_t drops = 0, uint32_t overruns = 0, uint32_t now = 0) {
        *this = MidiTrace{}; active = true;
        last_drops = drops; last_overruns = overruns; last_poll = now;
    }
    void poll(uint32_t now, uint32_t drops, uint32_t overruns, unsigned available) {
        if (!active) return;
        if (drops != last_drops && !drop_delta) {
            drop_gap_us = now - last_poll; drop_available = available;
            drop_delta = drops - last_drops;
        }
        if (overruns != last_overruns && !overrun_delta) {
            overrun_gap_us = now - last_poll; overrun_available = available;
            overrun_delta = overruns - last_overruns;
        }
        last_poll = now; last_drops = drops; last_overruns = overruns;
    }
    void reject(unsigned route, const midi::Packet &packet, unsigned state, uint32_t now) {
        if (!active || route != 0) return;
        if (!rejects++) {
            rejected = packet; rejected_route = route;
            rejected_state = state; rejected_ms = now;
            rejected_offset = usb_raw.count - packet.size();
            rejected_valid_offset = usb_valid.count;
        }
    }
    CNTRL_MIDI_TRACE_COLD
    int format(char *output, unsigned size, const char *build,
               uint32_t din_drops, uint32_t din_overruns,
               uint32_t trs_drops, uint32_t trs_overruns) const {
        return std::snprintf(output, size,
            "CNTRL MIDI TRACE version=1 build=%s active=%u "
            "usb_raw_n=%lu usb_raw_crc=0x%lx usb_valid_n=%lu usb_valid_crc=0x%lx "
            "din_tx_n=%lu din_tx_crc=0x%lx din_rx_n=%lu din_rx_crc=0x%lx "
            "usb_return_n=%lu usb_return_crc=0x%lx rejects=%lu "
            "reject_packet=0x%lx reject_source=%u reject_route=%lu reject_state=%lu reject_ms=%lu "
            "reject_offset=%lu reject_valid_offset=%lu "
            "din_drops=%lu din_overruns=%lu trs_drops=%lu trs_overruns=%lu "
            "drop_gap_us=%lu drop_available=%lu drop_delta=%lu "
            "overrun_gap_us=%lu overrun_available=%lu overrun_delta=%lu",
            build, unsigned(active),
            (unsigned long)usb_raw.count, (unsigned long)~usb_raw.checksum,
            (unsigned long)usb_valid.count, (unsigned long)~usb_valid.checksum,
            (unsigned long)din_tx.count, (unsigned long)~din_tx.checksum,
            (unsigned long)din_rx.count, (unsigned long)~din_rx.checksum,
            (unsigned long)usb_return.count, (unsigned long)~usb_return.checksum,
            (unsigned long)rejects, (unsigned long)rejected.packed(rejected.source & 15),
            unsigned(rejected.source), (unsigned long)rejected_route,
            (unsigned long)rejected_state, (unsigned long)rejected_ms,
            (unsigned long)rejected_offset, (unsigned long)rejected_valid_offset,
            (unsigned long)din_drops, (unsigned long)din_overruns,
            (unsigned long)trs_drops, (unsigned long)trs_overruns,
            (unsigned long)drop_gap_us, (unsigned long)drop_available, (unsigned long)drop_delta,
            (unsigned long)overrun_gap_us, (unsigned long)overrun_available, (unsigned long)overrun_delta);
    }
};
}
