#pragma once
#include <cstddef>
#include <cstdint>

namespace cntrl {
constexpr unsigned max_document = 65536;
constexpr unsigned max_instruments = 32;
constexpr unsigned max_categories = 128;
constexpr unsigned history_capacity = 128;

struct ScreenGraphics {
    uint8_t font = 0, size = 8, pad = 4, reserved = 0;
    uint16_t foreground = 0, background = 0xffff, line_height = 8, top = 0;
    uint16_t half = 0x7bef;
};

struct Pane {
    char category[64] = {};
    char patch[64] = {};
    ScreenGraphics graphics;
    uint16_t category_x = 4, patch_x = 4, patch_color = 0;
    bool centered = false;
    bool wrap_patch = false;
};

struct Hardware {
    virtual ~Hardware() = default;
    // Accept the complete message sequence, or accept none of it.
    virtual bool send(const char *output, const uint8_t *bytes, unsigned length) = 0;
    virtual bool output_idle() const { return true; }
    virtual bool supports(const char *output) const = 0;
    virtual bool supports_font(unsigned font, unsigned size) const = 0;
    virtual unsigned display_width() const = 0;
    virtual unsigned display_height() const = 0;
    virtual void show(unsigned pane, const Pane &content) = 0;
    virtual void transition(bool undo) { (void)undo; }
    virtual void set_brightness(uint8_t value) = 0;
    virtual void set_splash(bool enabled, uint32_t idle_ms) = 0;
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
struct SetupStorage : Storage {
    virtual bool available() const = 0;
    virtual void choose(const char *name) = 0;
    virtual bool next(const char *after, char *name) = 0;
};
} // namespace cntrl
