#pragma once
#include <cstdint>
struct Session {
    unsigned received = 0, consumed_count = 0;
    cntrl::UsbKind kind = cntrl::UsbKind::command;
    bool blocked = false, reset = false;
    bool output_pending() const { return blocked; }
    void receive(int, uint32_t) { ++received; }
    bool resetting() const { return reset; }
    void reset_done() { reset = false; }
    unsigned input_size() const { return received - consumed_count; }
    char input() const { return 'x'; }
    cntrl::UsbKind input_kind() const { return kind; }
    const uint8_t *input_bytes() const { return nullptr; }
    void consumed(unsigned n = 1) { consumed_count += n; }
    bool active() const { return true; }
};
struct Job {
    unsigned polls = 0, keepalives = 0, cancellations = 0;
    unsigned blocks = 0, bytes = 0;
    bool processing() const { return false; }
    bool accepts_input() const { return true; }
    void cancel_transfer() { ++cancellations; }
    void cancel() { ++cancellations; }
    void keepalive(uint32_t) { ++keepalives; }
    void receive(char, uint32_t) {}
    void receive_data(cntrl::UsbKind, const uint8_t *, unsigned n, uint32_t) { ++blocks; bytes += n; }
    void poll(uint32_t) { ++polls; }
    template<class Replies> void poll(uint32_t, Replies &) { ++polls; }
    void tick(uint32_t) { ++polls; }
    void tick() { ++polls; }
};
