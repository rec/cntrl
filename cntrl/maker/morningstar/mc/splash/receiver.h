#pragma once
#include "animation.h"
#include <cntrl/hardware.h>
#include <cstdio>

namespace cntrl::splash {
inline uint32_t crc_update(uint32_t crc, const uint8_t *p, unsigned n) {
    for (unsigned i = 0; i < n; ++i) {
        crc ^= p[i];
        for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return crc;
}
inline void put32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(n >> (8 * i));
}

struct Replies {
    virtual ~Replies() = default;
    virtual void reply(const char *line) = 0;
};

// Separate SD-sized transfer protocol. Only the inactive slot is overwritten.
// Boot and commit verification each read at most 512 asset bytes per poll.
class Receiver : public Source {
public:
    Receiver(cntrl::Storage &storage, Replies &replies) : storage_(storage), replies_(replies) {}
    void boot() {
        active_ = -1; generation_ = length_ = checksum_ = 0;
        state_ = State::boot; slot_ = 0; offset_ = 0;
    }
    bool busy() const { return state_ != State::command; }
    bool accepts_input() const { return state_ == State::command || state_ == State::receiving; }
    uint32_t size() override { return length_; }
    bool read(uint32_t offset, uint8_t *data, uint32_t length) override {
        return active_ >= 0 && offset <= length_ && length <= length_ - offset
            && storage_.read(unsigned(active_), 16 + offset, data, length);
    }
    void receive(uint8_t byte, uint32_t now) {
        if (state_ == State::receiving) {
            last_ = now;
            buffer_[used_++] = byte;
            if (used_ == sizeof(buffer_) || offset_ + used_ == incoming_) {
                bool valid = true;
                for (unsigned i = 0; i < used_; ++i) {
                    uint32_t position = offset_ + i;
                    if (position < header_bytes) header_[position] = buffer_[i];
                    if (position == header_bytes - 1 && !valid_header(header_, incoming_)) valid = false;
                    if (position >= header_bytes && (position - header_bytes) % frame_bytes < 4) {
                        unsigned b = (position - header_bytes) % frame_bytes;
                        if (b == 0) frame_duration_ = 0;
                        frame_duration_ |= uint32_t(buffer_[i]) << (b * 8);
                        if (b == 3 && (!frame_duration_ || frame_duration_ > 0x7fffffff)) valid = false;
                    }
                }
                if (!valid || !storage_.write(buffer_, used_)) { fail("invalid-or-storage"); return; }
                crc_ = crc_update(crc_, buffer_, used_); offset_ += used_; used_ = 0;
                if (offset_ < incoming_) {
                    char response[64];
                    std::snprintf(response, sizeof(response), "CNTRL SPLASH NEXT %lu", (unsigned long)offset_);
                    replies_.reply(response);
                } else {
                    if (~crc_ != expected_) { fail("checksum"); return; }
                    if (!storage_.write(reinterpret_cast<const uint8_t *>("DONE"), 4)
                        || !storage_.finish()) { fail("storage-commit"); return; }
                    state_ = State::verify; offset_ = 0;
                }
            }
        } else if (state_ == State::command) {
            if (byte == '\n') {
                line_[line_used_] = 0; line_used_ = 0; command(now);
            } else if (line_used_ < sizeof(line_) - 1) {
                line_[line_used_++] = char(byte);
            } else {
                line_used_ = 0; replies_.reply("CNTRL SPLASH ERROR command-too-long");
            }
        }
    }
    void poll(uint32_t now) {
        if (state_ == State::receiving && uint32_t(now - last_) > 10000) {
            fail("timeout"); return;
        }
        if (state_ != State::boot && state_ != State::verify) return;
        if (!offset_) {
            uint8_t trailer[4], asset_header[header_bytes];
            if (!storage_.read(slot_, 0, envelope_, 16)
                || std::memcmp(envelope_, "CNSP", 4)
                || u32(envelope_ + 8) < header_bytes
                || uint64_t(u32(envelope_ + 8)) + 20 != storage_.size(slot_)
                || !storage_.read(slot_, 16 + u32(envelope_ + 8), trailer, 4)
                || std::memcmp(trailer, "DONE", 4)
                || !storage_.read(slot_, 16, asset_header, sizeof(asset_header))
                || !valid_header(asset_header, u32(envelope_ + 8))) {
                verified(false); return;
            }
            crc_ = 0xffffffff;
        }
        unsigned amount = u32(envelope_ + 8) - offset_;
        if (amount > sizeof(buffer_)) amount = sizeof(buffer_);
        if (!storage_.read(slot_, 16 + offset_, buffer_, amount)) { verified(false); return; }
        crc_ = crc_update(crc_, buffer_, amount); offset_ += amount;
        if (offset_ == u32(envelope_ + 8)) verified(~crc_ == u32(envelope_ + 12));
    }
private:
    enum class State { command, boot, receiving, verify };
    void command(uint32_t now) {
        if (!std::strcmp(line_, "CNTRL SPLASH STATUS")) {
            report("READY"); return;
        }
        unsigned long size, crc;
        int end = 0;
        if (std::sscanf(line_, "CNTRL SPLASH BEGIN %lu %lx%n", &size, &crc, &end) != 2
            || line_[end] || size > 0xfffffffful || crc > 0xfffffffful
            || size < header_bytes + frame_bytes || size > 0xffffffffu - 20
            || (size - header_bytes) % frame_bytes
            || (size - header_bytes) / frame_bytes > 65535) {
            replies_.reply("CNTRL SPLASH ERROR command"); return;
        }
        incoming_ = uint32_t(size); expected_ = uint32_t(crc);
        slot_ = active_ == 0 ? 1 : 0;
        uint8_t envelope[16];
        std::memcpy(envelope, "CNSP", 4);
        put32(envelope + 4, generation_ + 1); put32(envelope + 8, incoming_);
        put32(envelope + 12, expected_);
        if (!storage_.begin(slot_) || !storage_.write(envelope, sizeof(envelope))) {
            storage_.finish(); replies_.reply("CNTRL SPLASH ERROR storage-begin"); return;
        }
        state_ = State::receiving; offset_ = used_ = 0; crc_ = 0xffffffff; last_ = now;
        replies_.reply("CNTRL SPLASH SEND");
    }
    void fail(const char *reason) {
        storage_.finish(); state_ = State::command; used_ = line_used_ = 0;
        char response[80];
        std::snprintf(response, sizeof(response), "CNTRL SPLASH ERROR %s", reason);
        replies_.reply(response);
    }
    void report(const char *kind) {
        char response[80];
        std::snprintf(response, sizeof(response), "CNTRL SPLASH %s %lu %08lx", kind,
                      (unsigned long)length_, (unsigned long)checksum_);
        replies_.reply(response);
    }
    void verified(bool valid) {
        if (valid && (active_ < 0 || int32_t(u32(envelope_ + 4) - generation_) > 0)) {
            active_ = int(slot_); generation_ = u32(envelope_ + 4);
            length_ = u32(envelope_ + 8); checksum_ = u32(envelope_ + 12);
        }
        if (state_ == State::boot) {
            if (++slot_ < 2) { offset_ = 0; return; }
            state_ = State::command;
        } else {
            state_ = State::command;
            if (valid) report("DONE");
            else replies_.reply("CNTRL SPLASH ERROR storage-verify");
        }
    }
    cntrl::Storage &storage_;
    Replies &replies_;
    State state_ = State::command;
    int active_ = -1;
    unsigned slot_ = 0, used_ = 0, line_used_ = 0;
    uint32_t generation_ = 0, length_ = 0, checksum_ = 0, incoming_ = 0, expected_ = 0;
    uint32_t offset_ = 0, crc_ = 0, last_ = 0, frame_duration_ = 0;
    uint8_t buffer_[512], envelope_[16], header_[header_bytes];
    char line_[96];
};
} // namespace cntrl::splash
