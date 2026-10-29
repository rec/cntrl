#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>

namespace cntrl {
constexpr unsigned max_document = 65536;
constexpr unsigned max_instruments = 32;
constexpr unsigned max_patch_lists = 128;
constexpr unsigned history_capacity = 128;
constexpr unsigned max_lines = 16;

struct ScreenGraphics {
    uint8_t font = 0, size = 8, pad = 4, reserved = 0;
    uint16_t foreground = 0, background = 0xffff, line_height = 8, top = 0;
    uint32_t asset = 0;
};

struct TextLine {
    char text[64] = {};
    ScreenGraphics graphics;
    uint16_t x = 4;
    int16_t y = -1; // Negative uses the normal vertical distribution.
    bool centered = false;
    bool wrap = false;
    bool stippled = false;
};

struct Pane {
    ScreenGraphics graphics;
    TextLine lines[max_lines];
};

inline bool button_pair_valid(unsigned mask) {
    return mask < 64 && mask && (mask & (mask - 1)) && !( (mask & (mask - 1)) & ((mask & (mask - 1)) - 1));
}

struct SongContinuation;
struct Hardware {
    virtual ~Hardware() = default;
    // Accept the complete message sequence, or accept none of it.
    virtual bool send(const char *output, const uint8_t *bytes, unsigned length) = 0;
    virtual bool supports_function(unsigned) const { return false; }
    virtual bool stock_firmware(uint8_t, const SongContinuation * = nullptr) { return false; }
    virtual bool output_idle() const { return true; }
    virtual unsigned switch_capacity() const { return 0; }
    virtual bool configuration_locked() const { return false; }
    virtual bool supports(const char *output) const = 0;
    virtual bool supports_font(unsigned font, unsigned size) const = 0;
    virtual bool supports_font_asset(uint32_t, unsigned) const { return false; }
    virtual void begin_fonts() {}
    virtual bool stage_font(uint32_t, unsigned) { return false; }
    virtual void finish_fonts(bool) {}
    virtual unsigned display_width() const = 0;
    virtual unsigned display_height() const = 0;
    virtual void show(unsigned pane, const Pane &content) = 0;
    virtual void transition(bool undo, std::optional<unsigned> duration_ms) { (void)undo; (void)duration_ms; }
    // Temporary performance feedback bypasses dimming; a normal call clears it.
    virtual void set_brightness(uint8_t value, bool temporary = false) = 0;
    virtual void set_usb_midi_interface(bool) {}
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
    virtual void choose_revision(const char *name, uint64_t number) = 0;
    virtual void revision_scan(const char *name, uint64_t before, bool records_only) = 0;
    virtual bool revision_scan_step(uint64_t &best) = 0;
    virtual bool remove_revision(const char *name, uint64_t number) = 0;
    virtual bool available() const = 0;
    virtual void choose(const char *name) = 0;
    virtual bool next(const char *after, char *name) = 0;
    virtual void catalogue_begin() = 0;
    // Visit one directory entry. Empty name skips it; true closes the scan.
    virtual bool catalogue_step(char *name) = 0;
};
} // namespace cntrl
