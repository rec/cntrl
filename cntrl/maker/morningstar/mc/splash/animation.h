#pragma once
#include <cstdint>
#include <cstring>

namespace cntrl::splash {
constexpr uint32_t width = 160, height = 128, row_bytes = width * 2;
constexpr uint32_t header_bytes = 16, frame_bytes = 4 + row_bytes * height * 2;

inline uint16_t u16(const uint8_t *p) { return p[0] | uint16_t(p[1]) << 8; }
inline uint32_t u32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

struct Source {
    virtual ~Source() = default;
    virtual uint32_t size() = 0;
    virtual bool read(uint32_t offset, uint8_t *data, uint32_t length) = 0;
};

struct Output {
    virtual ~Output() = default;
    // 0b101 selects both outer panels, 0b010 selects the centre. Pixels already
    // have the correct orientation and RGB565 wire byte order; no conversion.
    virtual void row(uint8_t panels, unsigned y, const uint8_t *pixels) = 0;
};

inline bool valid_header(const uint8_t *p, uint32_t size) {
    return !std::memcmp(p, "CNSA", 4) && u16(p + 4) == 1
        && u16(p + 6) == width && u16(p + 8) == height && u16(p + 10)
        && u32(p + 12) == size
        && uint64_t(header_bytes) + uint64_t(u16(p + 10)) * frame_bytes == size;
}

enum class Step { inactive, playing, dismissed, failed };

class Player {
public:
    Player(Source &source, Output &output) : source_(source), output_(output) {}
    bool start(uint32_t now) {
        active_ = false;
        uint8_t header[header_bytes];
        if (source_.size() < header_bytes || !source_.read(0, header, sizeof(header))
            || !valid_header(header, source_.size())) return false;
        count_ = u16(header + 10); frame_ = packet_ = 0; started_ = now;
        active_ = duration();
        return active_;
    }
    void stop() { active_ = false; }
    Step poll(uint32_t now, bool pressed) {
        if (!active_) return Step::inactive;
        if (pressed) { active_ = false; return Step::dismissed; }
        if (packet_ == height * 2) {
            if (uint32_t(now - started_) < duration_) return Step::playing;
            frame_ = (frame_ + 1) % count_; packet_ = 0; started_ = now;
            if (!duration()) { active_ = false; return Step::failed; }
        }
        uint8_t pixels[row_bytes];
        if (!source_.read(header_bytes + frame_ * frame_bytes + 4 + packet_ * row_bytes,
                          pixels, sizeof(pixels))) {
            active_ = false; return Step::failed;
        }
        output_.row(packet_ & 1 ? 0b010 : 0b101, packet_ / 2, pixels);
        ++packet_;
        return Step::playing;
    }
private:
    bool duration() {
        uint8_t bytes[4];
        if (!source_.read(header_bytes + frame_ * frame_bytes, bytes, sizeof(bytes))) return false;
        duration_ = u32(bytes);
        return duration_ && duration_ <= 0x7fffffff;
    }
    Source &source_;
    Output &output_;
    uint32_t count_ = 0, frame_ = 0, packet_ = 0, started_ = 0, duration_ = 0;
    bool active_ = false;
};
} // namespace cntrl::splash
