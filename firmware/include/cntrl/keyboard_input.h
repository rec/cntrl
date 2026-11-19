#pragma once
#include <atomic>
#include <cstdint>

namespace cntrl {
// USB host callbacks can run in interrupt context. Consume keys on the UI loop.
class KeyboardInput {
public:
    bool press(unsigned key, unsigned modifiers) {
        if (modifiers & 0xdd) return false; // Control, Alt and GUI shortcuts are not text.
        if (key == 127) key = '\b'; // USBHost_t36 translates Backspace to ASCII DEL.
        if (key != '\n' && key != '\b' && key != 27 && (key < 32 || key > 126)) return false;
        const unsigned write = write_.load(std::memory_order_relaxed);
        const unsigned next = (write + 1) % capacity;
        if (next == read_.load(std::memory_order_acquire)) return false;
        keys_[write] = uint8_t(key);
        write_.store(next, std::memory_order_release);
        return true;
    }
    bool read(uint8_t &key) {
        const unsigned read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) return false;
        key = keys_[read];
        read_.store((read + 1) % capacity, std::memory_order_release);
        return true;
    }
    static constexpr unsigned capacity = 32;
private:
    static_assert(std::atomic<unsigned>::is_always_lock_free);
    uint8_t keys_[capacity] = {};
    std::atomic<unsigned> read_{0}, write_{0};
};
} // namespace cntrl
