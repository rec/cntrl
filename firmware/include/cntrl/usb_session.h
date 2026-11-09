#pragma once
#include "usb_config.h"
#include <algorithm>
#include <cassert>

namespace cntrl {
// CRC-protected binary frames. Cached replies are released only by an explicit
// acknowledgement; duplicate requests never reach the application twice.
class UsbSession {
public:
    void reset() {
        session_ = sequence_ = 0; reply_id_ = 1;
        used_ = size_ = offset_ = sent_ = output_size_ = reply_size_ = 0;
        reset_ = closed_ = false;
    }
    bool active() const { return session_ != 0 && !closed_; }
    bool resetting() const { return reset_; }
    void reset_done() { reset_ = false; if (closed_) respond(); else finish_input(); }
    bool reply_ready() const { return !active() || reset_ || !reply_size_; }
    void reply(const char *text) {
        unsigned n = unsigned(std::strlen(text));
        assert(n + 1 <= sizeof(reply_));
        if (!active() || reset_) return;
        std::memcpy(reply_, text, n); reply_[n] = '\n'; reply_size_ = n + 1; reply_kind_ = UsbKind::text;
    }
    void data(const uint8_t *bytes, unsigned length) {
        assert(length && length <= usb_block_size);
        if (!active() || reset_) return;
        std::memcpy(reply_, bytes, length); reply_size_ = length; reply_kind_ = UsbKind::data;
    }
    unsigned input_size() const { return reset_ ? 0 : size_ - offset_; }
    const uint8_t *input_bytes() const { return bytes_ + offset_; }
    UsbKind input_kind() const { return input_kind_; }
    uint8_t input() const { return bytes_[offset_]; }
    void consumed(unsigned n = 1) { offset_ += n; assert(offset_ <= size_); if (offset_ == size_) finish_input(); }
    const uint8_t *output() const { return output_ + sent_; }
    unsigned output_size() const { return output_size_ - sent_; }
    void sent(unsigned n) { sent_ += n; assert(sent_ <= output_size_); }
    bool output_pending() const { return sent_ < output_size_; }
    void receive(uint8_t byte) {
        assert(used_ < sizeof(incoming_)); incoming_[used_++] = byte;
        while (used_) {
            unsigned prefix = std::min(used_, unsigned(sizeof(usb_magic)));
            if (std::memcmp(incoming_, usb_magic, prefix)) { discard(); continue; }
            if (used_ < usb_header_size) return;
            if (!UsbFrame::valid_header(incoming_)) { discard(); continue; }
            UsbFrame frame = UsbFrame::decode(incoming_);
            if (frame.length > usb_block_size) { discard(); continue; }
            if (used_ < usb_header_size + frame.length) return;
            if (usb_crc(incoming_ + usb_header_size, frame.length) != frame.checksum) { discard(); continue; }
            accept(frame, incoming_ + usb_header_size);
            unsigned remaining = used_ - usb_header_size - frame.length;
            std::memmove(incoming_, incoming_ + usb_header_size + frame.length, remaining); used_ = remaining;
        }
    }
private:
    uint32_t session_ = 0, sequence_ = 0, reply_id_ = 1;
    unsigned used_ = 0, size_ = 0, offset_ = 0, sent_ = 0, output_size_ = 0, reply_size_ = 0;
    bool reset_ = false, closed_ = false;
    UsbKind input_kind_ = UsbKind::command, reply_kind_ = UsbKind::text;
    uint8_t incoming_[usb_header_size + usb_block_size] = {}, bytes_[usb_block_size] = {};
    uint8_t reply_[usb_payload_size] = {}, output_[usb_frame_size] = {};
    void discard() { --used_; std::memmove(incoming_, incoming_ + 1, used_); }
    void accept(const UsbFrame &frame, const uint8_t *payload) {
        if (incoming_[8] != 1) { respond(UsbStatus::version, frame.session, frame.sequence); return; }
        if (!frame.session || frame.status != UsbStatus::ok) { respond(UsbStatus::invalid, frame.session, frame.sequence); return; }
        if (frame.kind == UsbKind::close && frame.session == session_ && !frame.sequence && !frame.length) {
            closed_ = true; sequence_ = 0; reply_size_ = size_ = offset_ = 0; reset_ = true; return;
        }
        if (frame.session != session_) {
            if (frame.kind != UsbKind::open || frame.sequence != 1 || frame.acknowledged || frame.length) {
                respond(UsbStatus::lost, frame.session, frame.sequence); return;
            }
            session_ = frame.session; sequence_ = 0; reply_id_ = 1; closed_ = false;
            reply_size_ = size_ = offset_ = 0; reset_ = true;
        }
        if (reset_ && sequence_) return;
        if (frame.acknowledged == reply_id_ && reply_size_) { reply_size_ = 0; ++reply_id_; }
        else if (frame.acknowledged >= reply_id_) { respond(UsbStatus::invalid, frame.session, frame.sequence); return; }
        if (frame.sequence == sequence_) { respond(); return; }
        bool control = frame.kind == UsbKind::open || frame.kind == UsbKind::poll;
        bool data = unsigned(frame.kind) >= unsigned(UsbKind::setup) && unsigned(frame.kind) <= unsigned(UsbKind::disk);
        if (frame.sequence != uint64_t(sequence_) + 1 || (control && frame.length) || (data && !frame.length)
            || (!control && frame.kind != UsbKind::command && !data)) { respond(UsbStatus::invalid, frame.session, frame.sequence); return; }
        if (size_ != offset_) return;
        if (frame.length) std::memcpy(bytes_, payload, frame.length);
        input_kind_ = frame.kind; offset_ = 0; size_ = frame.length;
        if (!size_ && !reset_) finish_input();
    }
    void finish_input() { ++sequence_; respond(); }
    void respond(UsbStatus status = UsbStatus::ok, uint32_t session = 0, uint32_t sequence = 0) {
        UsbFrame frame{status == UsbStatus::ok ? reply_kind_ : UsbKind::error, status,
            session ? session : session_, session ? sequence : sequence_, reply_id_, usb_block_size,
            status == UsbStatus::ok ? reply_size_ : 0, 0};
        frame.encode(output_, reply_); sent_ = 0; output_size_ = usb_header_size + frame.length;
    }
};
} // namespace cntrl
