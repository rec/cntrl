#pragma once
#include "memory.h"
#include "midi_file.h"

namespace cntrl::midi {
// Prepared live generations never touch storage. Allocation is charged to the
// same RAM2 budget as documents and fonts before the looper can be armed.
class LoopFile final : public MidiFile {
public:
    bool reserve(unsigned capacity) {
        if (!memory_.resize(capacity)) return false;
        length_ = 0; return true;
    }
    unsigned capacity() const { return memory_.size; }
    bool trim() { return memory_.resize(length_); }
    void reset() { length_ = 0; }
    void swap(LoopFile &other) { memory_.swap(other.memory_); std::swap(length_, other.length_); }
    uint32_t size() const override { return length_; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(loop_file_read)
#endif
    bool read(uint32_t offset, uint8_t *data, unsigned length) override {
        if (offset > length_ || length > length_ - offset) return false;
        if (length) std::memcpy(data, memory_.data + offset, length);
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(loop_file_write)
#endif
    bool write(uint32_t offset, const uint8_t *data, unsigned length) override {
        if (offset > memory_.size || length > memory_.size - offset) return false;
        if (length) std::memcpy(memory_.data + offset, data, length);
        length_ = std::max(length_, offset + uint32_t(length)); return true;
    }
    bool sync() override { return true; }
private:
    MemoryBuffer memory_;
    uint32_t length_ = 0;
};
} // namespace cntrl::midi
