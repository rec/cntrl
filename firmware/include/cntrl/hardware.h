#pragma once
#include <cstddef>
#include <cstdint>

namespace cntrl {
constexpr unsigned max_document = 65536;
constexpr unsigned max_instruments = 32;
constexpr unsigned max_categories = 128;
constexpr unsigned history_capacity = 128;

struct Pane {
    char title[64] = {};
    char instrument[32] = {};
    char category[64] = {};
    char patch[64] = {};
};

struct Hardware {
    virtual ~Hardware() = default;
    // Accept the complete message sequence, or accept none of it.
    virtual bool send(const char *output, const uint8_t *bytes, unsigned length) = 0;
    virtual bool supports(const char *output) const = 0;
    virtual void show(unsigned pane, const Pane &content) = 0;
    virtual void set_brightness(uint8_t value) = 0;
    virtual void error(const char *message) = 0;
};

struct Storage {
    virtual ~Storage() = default;
    virtual unsigned size(unsigned slot) = 0;
    virtual bool read(unsigned slot, unsigned offset, uint8_t *data, unsigned length) = 0;
    // Only the inactive slot is ever truncated or written.
    virtual bool begin(unsigned slot) = 0;
    virtual bool write(const uint8_t *data, unsigned length) = 0;
    virtual bool finish() = 0;
};
} // namespace cntrl
