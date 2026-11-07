#pragma once
#include <cstdint>
struct Session {
    unsigned received = 0, consumed_count = 0;
    bool blocked = false, reset = false;
    bool output_pending() const { return blocked; }
    void receive(int) { ++received; }
    bool resetting() const { return reset; }
    void reset_done() { reset = false; }
    unsigned input_size() const { return received - consumed_count; }
    char input() const { return 'x'; }
    void consumed() { ++consumed_count; }
    bool active() const { return true; }
};
struct Job {
    unsigned polls = 0, keepalives = 0, cancellations = 0;
    bool processing() const { return false; }
    bool accepts_input() const { return true; }
    void cancel_transfer() { ++cancellations; }
    void cancel() { ++cancellations; }
    void keepalive(uint32_t) { ++keepalives; }
    void receive(char, uint32_t) {}
    void poll(uint32_t) { ++polls; }
    template<class Replies> void poll(uint32_t, Replies &) { ++polls; }
    void tick(uint32_t) { ++polls; }
    void tick() { ++polls; }
};
