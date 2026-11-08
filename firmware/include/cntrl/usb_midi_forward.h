#pragma once
#include <cstdint>
#include <cstring>

namespace cntrl::midi {
// Publish descriptor changes only while detached. A replacement during the
// detach interval publishes the latest device, never an intermediate identity.
class UsbMidiForward {
public:
    template<class Services>
    void poll(bool enabled, bool connected, uint32_t generation, const char *name,
              uint32_t now, bool busy, Services &services) {
        if (connected != connected_ || generation != observed_generation_) {
            services.clear_device(); connected_ = connected; observed_generation_ = generation;
        }
        if (enabled_ && !enabled) services.stop_forwarding();
        enabled_ = enabled;
        // Let the final serial acknowledgement leave the controller after a
        // configuration session closes, without blocking the input scheduler.
        if (busy) { idle_since_ = now; settling_ = true; }
        else if (settling_ && uint32_t(now - idle_since_) >= 10) settling_ = false;
        const bool present = enabled && connected;
        if (detached_) {
            if (uint32_t(now - detached_at_) < 250) return;
            visible_ = present;
            generation_ = present ? generation : 0;
            services.publish(present ? name : nullptr);
            services.attach(); detached_ = false;
        } else if (!busy && !settling_ && (visible_ != present || (present && generation_ != generation))) {
            services.detach(); detached_at_ = now; detached_ = true;
        }
    }
    bool active(uint32_t generation) const { return visible_ && !detached_ && generation_ == generation; }
private:
    uint32_t generation_ = 0, observed_generation_ = 0, detached_at_ = 0, idle_since_ = 0;
    bool visible_ = false, detached_ = false, connected_ = false, enabled_ = false, settling_ = false;
};

// Append one bidirectional MIDI cable to an existing USB MIDI 1.0 streaming
// interface, retaining the other composite interfaces and cable indices.
inline unsigned append_usb_midi_port(const uint8_t *source, unsigned length,
                                     uint8_t *target, unsigned capacity, uint8_t string_index) {
    if (length < 9 || source[0] != 9 || source[1] != 2 || length + 32 > capacity) return 0;
    unsigned midi = length, end = length, maximum_jack = 0;
    for (unsigned i = 9; i < length;) {
        unsigned size = source[i];
        if (size < 2 || size > length - i) return 0;
        if (source[i + 1] == 4 && size >= 9) {
            if (midi != length) { end = i; break; }
            if (source[i + 5] == 1 && source[i + 6] == 3) midi = i;
        }
        if (midi != length && source[i + 1] == 0x24 && size >= 5
            && (source[i + 2] == 2 || source[i + 2] == 3))
            if (source[i + 4] > maximum_jack) maximum_jack = source[i + 4];
        i += size;
    }
    if (midi == length || maximum_jack > 251) return 0;
    const uint8_t a = maximum_jack + 1, b = maximum_jack + 2,
                  c = maximum_jack + 3, d = maximum_jack + 4;
    const uint8_t jacks[] = {
        6, 0x24, 2, 1, a, string_index,
        6, 0x24, 2, 2, b, string_index,
        9, 0x24, 3, 1, c, 1, b, 1, string_index,
        9, 0x24, 3, 2, d, 1, a, 1, string_index,
    };
    unsigned used = 0, associations = 0;
    bool inserted = false, input = false;
    for (unsigned i = 0; i < length;) {
        unsigned size = source[i];
        if (size < 2 || size > length - i) return 0;
        const bool streaming = i > midi && i < end;
        if (streaming && source[i + 1] == 5) {
            if (size < 9) return 0;
            if (!inserted) {
                if (used + sizeof(jacks) > capacity) return 0;
                std::memcpy(target + used, jacks, sizeof(jacks));
                used += sizeof(jacks); inserted = true;
            }
            input = source[i + 2] & 0x80;
        }
        const unsigned extra = streaming && source[i + 1] == 0x25 ? 1 : 0;
        if (used + size + extra > capacity) return 0;
        std::memcpy(target + used, source + i, size);
        if (streaming && source[i + 1] == 0x24 && size == 7 && source[i + 2] == 1) {
            unsigned total = source[i + 5] | unsigned(source[i + 6]) << 8;
            total += sizeof(jacks); target[used + 5] = total; target[used + 6] = total >> 8;
        }
        if (streaming && source[i + 1] == 0x25) {
            if (size < 4 || source[i + 2] != 1 || source[i + 3] != size - 4 || source[i + 3] >= 16) return 0;
            target[used] = size + 1; ++target[used + 3];
            target[used + size] = input ? c : a; ++used; ++associations;
        }
        used += size; i += size;
    }
    if (!inserted || associations != 2 || used != length + 32) return 0;
    target[2] = used; target[3] = used >> 8;
    return used;
}
} // namespace cntrl::midi
