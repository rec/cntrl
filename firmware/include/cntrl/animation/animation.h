#pragma once
#include <cstdint>
#include <cstring>

namespace cntrl::animation {
constexpr uint32_t header_bytes = 20, packet_bytes = 320;
inline uint16_t u16(const uint8_t *p) { return p[0] | uint16_t(p[1]) << 8; }
inline uint32_t u32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline uint64_t payload_frame_bytes(const uint8_t *p) {
    return 4 + uint64_t(u16(p + 6)) * u16(p + 8) * u16(p + 10) * 2;
}
inline bool valid_header(const uint8_t *p, uint32_t size) {
    return !std::memcmp(p, "CNSA", 4) && u16(p + 4) == 2 && u16(p + 6)
        && u16(p + 8) && u16(p + 10) && u16(p + 12) && u16(p + 14) == 1
        && u32(p + 16) == size && payload_frame_bytes(p) <= size
        && uint64_t(header_bytes) + uint64_t(u16(p + 12)) * payload_frame_bytes(p) == size;
}
struct Source {
    virtual ~Source() = default;
    virtual uint32_t size() = 0;
    virtual bool read(uint32_t offset, uint8_t *data, uint32_t length) = 0;
};
struct Output {
    virtual ~Output() = default;
    virtual bool begin(unsigned width, unsigned height, unsigned streams) = 0;
    virtual bool ready() const = 0;
    // RGB565, most significant byte first. Accepted storage remains valid until ready().
    // A false return must not retain data; retry the same region on a later poll.
    virtual bool pixels(unsigned stream, unsigned x, unsigned y, const uint8_t *data, unsigned count) = 0;
};
enum class Step { inactive, playing, dismissed, failed };
class Player {
public:
    Player(Source &source, Output &output) : source_(source), output_(output) {}
    bool start(uint32_t now) {
        active_ = false;
        uint8_t header[header_bytes];
        if (source_.size() < header_bytes || !source_.read(0, header, sizeof(header))
            || !valid_header(header, source_.size())) return false;
        width_ = u16(header + 6); height_ = u16(header + 8); streams_ = u16(header + 10);
        count_ = u16(header + 12); frame_bytes_ = uint32_t(payload_frame_bytes(header));
        frame_ = row_ = x_ = 0; started_ = now;
        active_ = output_.begin(width_, height_, streams_) && duration();
        return active_;
    }
    void stop() { active_ = false; }
    bool frame_ready() const { return row_ == height_ * streams_ && output_.ready(); }
    Step poll(uint32_t now, bool pressed) {
        if (!active_) return Step::inactive;
        if (pressed) { active_ = false; return Step::dismissed; }
        if (!output_.ready()) return Step::playing;
        if (row_ == height_ * streams_) {
            if (uint32_t(now - started_) < duration_) return Step::playing;
            frame_ = (frame_ + 1) % count_; row_ = x_ = 0; started_ = now;
            if (!duration()) { active_ = false; return Step::failed; }
        }
        unsigned count = width_ - x_;
        if (count > packet_bytes / 2) count = packet_bytes / 2;
        if (!source_.read(header_bytes + frame_ * frame_bytes_ + 4 + (row_ * width_ + x_) * 2, pixels_, count * 2)) {
            active_ = false; return Step::failed;
        }
        if (output_.pixels(row_ % streams_, x_, row_ / streams_, pixels_, count)) {
            x_ += count;
            if (x_ == width_) { x_ = 0; ++row_; }
        }
        return Step::playing;
    }
private:
    bool duration() {
        uint8_t bytes[4];
        if (!source_.read(header_bytes + frame_ * frame_bytes_, bytes, sizeof(bytes))) return false;
        duration_ = u32(bytes);
        return duration_ && duration_ <= 0x7fffffff;
    }
    Source &source_;
    Output &output_;
    uint32_t width_ = 0, height_ = 0, streams_ = 0, frame_bytes_ = 0;
    uint32_t count_ = 0, frame_ = 0, row_ = 0, x_ = 0, started_ = 0, duration_ = 0;
    uint8_t pixels_[packet_bytes];
    bool active_ = false;
};
} // namespace cntrl::animation
