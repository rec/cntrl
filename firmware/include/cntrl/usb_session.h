#pragma once
#include "usb_config.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace cntrl {
// Stop-and-wait transport. A reply stays owned by the session until the host
// acknowledges it; duplicate input frames never reach the command interpreter.
class UsbSession {
public:
    void reset() {
        session_ = sequence_ = 0; reply_id_ = 1;
        used_ = size_ = offset_ = sent_ = 0;
        collecting_ = reset_ = closed_ = false;
        line_[0] = reply_[0] = output_[0] = 0;
    }
    bool active() const { return session_ != 0 && !closed_; }
    bool resetting() const { return reset_; }
    void reset_done() { reset_ = false; if (closed_) respond(); else finish_input(); }
    bool reply_ready() const { return !active() || reset_ || !reply_[0]; }
    void reply(const char *text) {
        if (!active() || reset_) return;
        std::snprintf(reply_, sizeof(reply_), "%s\n", text);
    }
    unsigned input_size() const { return reset_ ? 0 : size_ - offset_; }
    uint8_t input() const { return bytes_[offset_]; }
    void consumed() { if (++offset_ == size_) finish_input(); }
    const char *output() const { return output_ + sent_; }
    void sent(unsigned n) { sent_ += n; }
    bool output_pending() const { return output_[sent_] != 0; }
    void receive(uint8_t byte) {
        // Every retry starts with an out-of-band separator, including after a
        // partial write. Payloads are hex encoded, so this cannot occur in data.
        if (byte == 30) { used_ = 0; collecting_ = true; return; }
        if (!collecting_) return;
        if (byte != '\n') {
            if (used_ + 1 < sizeof(line_)) line_[used_++] = char(byte);
            else collecting_ = false;
            return;
        }
        collecting_ = false; line_[used_] = 0;
        unsigned long session; unsigned long seq, ack; int end = 0;
        if (std::sscanf(line_, "CNTRL USB %lx %lu %lu %n", &session, &seq, &ack, &end) != 3
            || !end || !line_[end] || std::strlen(line_ + end) > usb_block_size * 2 || !session || session > 0xfffffffful || seq > 0xfffffffful || ack > 0xfffffffful) return;
        const char *hex = line_ + end;
        if (session == session_ && !seq && !std::strcmp(hex, "-")) {
            closed_ = true; sequence_ = 0; reply_[0] = 0; size_ = offset_ = 0; reset_ = true; return;
        }
        if (session != session_) {
            if (seq != 1 || ack || std::strcmp(hex, "-")) { respond("LOST"); return; }
            session_ = session; sequence_ = 0; reply_id_ = 1; closed_ = false;
            reply_[0] = 0; size_ = offset_ = 0; reset_ = true;
        }
        if (reset_ && sequence_) return;
        if (ack == reply_id_ && reply_[0]) { reply_[0] = 0; ++reply_id_; }
        else if (ack >= reply_id_) { respond("INVALID"); return; }
        if (seq == sequence_) { respond(); return; }
        if (seq != uint64_t(sequence_) + 1) { respond("INVALID"); return; }
        if (size_ != offset_) return;
        unsigned n = std::strcmp(hex, "-") ? unsigned(std::strlen(hex)) : 0;
        if (n % 2) { respond("INVALID"); return; }
        for (unsigned i = 0; i < n; ++i) {
            char c = hex[i]; int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (v < 0) { respond("INVALID"); return; }
            if (i % 2) bytes_[i / 2] |= uint8_t(v); else bytes_[i / 2] = uint8_t(v << 4);
        }
        offset_ = 0; size_ = n / 2;
        if (!size_ && !reset_) finish_input();
    }
private:
    uint32_t session_ = 0;
    uint32_t sequence_ = 0, reply_id_ = 1;
    unsigned used_ = 0, size_ = 0, offset_ = 0, sent_ = 0;
    bool collecting_ = false, reset_ = false, closed_ = false;
    char line_[usb_block_size * 2 + 64] = {}, reply_[usb_text_size] = {}, output_[usb_frame_size] = {};
    uint8_t bytes_[usb_block_size] = {};
    void finish_input() { ++sequence_; respond(); }
    void respond(const char *error = nullptr) {
        sent_ = 0;
        if (error) { std::snprintf(output_, sizeof(output_), "CNTRL USB %s\n", error); return; }
        unsigned n = unsigned(std::snprintf(output_, sizeof(output_), "CNTRL USB %lx %lu %lu %u ",
            (unsigned long)session_, (unsigned long)sequence_, (unsigned long)reply_id_, usb_block_size));
        if (!reply_[0]) output_[n++] = '-';
        else for (const unsigned char *p = reinterpret_cast<const unsigned char *>(reply_); *p; ++p) {
            std::snprintf(output_ + n, 3, "%02x", *p); n += 2;
        }
        output_[n++] = '\n'; output_[n] = 0;
    }
};
} // namespace cntrl
