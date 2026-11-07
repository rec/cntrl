#pragma once
#include "midi_outputs.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include "page_settings.h"
#include "app_layout.h"
#include "memory.h"

namespace cntrl {
constexpr unsigned max_instruments = 32;
constexpr unsigned max_patch_cycles = 128;
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
    uint64_t color_mask = 0; // Characters using alternate_color instead of foreground.
    uint16_t alternate_color = 0;
};

struct Circle {
    uint16_t x = 0, y = 0, diameter = 0, annulus = 0, color = 0, ring_color = 0;
};

struct ToggleIcon {
    uint16_t x = 0, y = 0;
    bool visible = false, enabled = false, disabled = false;
};

struct Pane {
    bool performance = false;
    bool three_bands = false;
    bool heading_band = false;
    int pending_patch = -1; // Document patch index, or -1 when no unsent patch is displayed.
    ScreenGraphics graphics;
    TextLine lines[max_lines];
    Circle circles[4];
    ToggleIcon toggle;
};

inline bool button_pair_valid(unsigned mask) {
    return mask < 64 && mask && (mask & (mask - 1)) && !( (mask & (mask - 1)) & ((mask & (mask - 1)) - 1));
}

struct SongContinuation;
class Document;
struct Hardware {
    virtual ~Hardware() = default;
    // Accept the complete message sequence, or accept none of it.
    virtual bool send(const char *output, const uint8_t *bytes, unsigned length) = 0;
    virtual bool supports_function(unsigned) const { return false; }
    virtual bool stock_firmware(uint8_t, const SongContinuation * = nullptr) { return false; }
    virtual void configure_trs_ports(const uint8_t *) {}
    virtual bool supports_keyboard() const { return false; }
    virtual bool supports_trs_mode(unsigned, midi::TrsMode mode) const { return mode == midi::TrsMode::off; }
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
    // Pane indices follow physical left-to-right order: 0, 1, 2.
    virtual void show(unsigned pane, const Pane &content) = 0;
    virtual void transition(bool undo, std::optional<unsigned> duration_ms, int pane = -1, bool vertical = false, bool panorama = true) { (void)undo; (void)duration_ms; (void)pane; (void)vertical; (void)panorama; }
    virtual void configure_page_settings(const PageSettings &) {}
    virtual void configure_apps(const Document &) {}
    virtual bool refresh_apps() { return false; }
    virtual void browse_page(bool) {}
    virtual void tap_tempo(uint32_t) {}
    // Temporary performance feedback bypasses dimming; a normal call clears it.
    virtual void set_brightness(uint8_t value, bool temporary = false) = 0;
    virtual void set_target_pulse(unsigned, uint8_t, uint8_t, unsigned) {}
    virtual void set_usb_midi(bool, bool) {}
    virtual void set_midi_interface(bool) {}
    virtual void set_splash(bool enabled, uint32_t idle_ms) = 0;
    virtual void set_movies(const uint8_t *, unsigned, const uint8_t *, unsigned) {}
    virtual void set_animation_color_cycle(bool) {}
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
    virtual bool patch(unsigned, const uint8_t *, unsigned) { return false; }
    virtual bool flush() { return false; }
};
struct SetupStorage : Storage {
    virtual void prepare_document(const Document &) {}
    virtual unsigned setup_count() const { return 0; }
    virtual const char *setup_name_at(unsigned) const { return nullptr; }
    virtual uint32_t setup_names_signature() const { return 0; }
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
