#include "displays.h"

#include <Arduino.h>
#include <SPI.h>

namespace {
struct Panel {
    uint8_t cs;
    uint8_t dc;
    uint8_t reset;
};

void send_byte(uint8_t value) {
    SPI.transfer(value);
}

void begin_transfer() {
    // begin() restores the SPI mux after the switch reader's GPIO use.
    SPI.begin();
    SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
}

void end_transfer() {
    SPI.endTransaction(); // Blocking transfers have drained before deselection.
    digitalWriteFast(13, LOW);
    pinMode(13, OUTPUT);
    pinMode(12, INPUT); // SPI.begin() also muxes the switch-data pin as MISO.
}

void command(const Panel &panel, uint8_t code, const uint8_t *data, unsigned size) {
    digitalWrite(panel.dc, LOW);
    digitalWrite(panel.cs, LOW);
    send_byte(code);
    digitalWrite(panel.dc, HIGH);
    for (unsigned i = 0; i < size; ++i) {
        send_byte(data[i]);
    }
    digitalWrite(panel.cs, HIGH);
}

uint8_t glyph_row(char character, unsigned row);

bool text_pixel(const char *text, unsigned length, int x, int y) {
    // Original 5x7 glyphs, scaled by two, with a one-column character gap.
    if (x < 0 || y < 0 || y >= 14 || unsigned(x) >= length * 12) {
        return false;
    }
    const unsigned column = (x / 2) % 6;
    return column < 5 && (glyph_row(text[x / 12], y / 2) & (1u << (4 - column)));
}

void draw(const Panel &panel, char number) {
    // Rotation 1 exchanges the traced native offsets: x starts at 1, y at 2.
    const uint8_t columns[] = {0, 1, 0, 160};
    const uint8_t rows[] = {0, 2, 0, 129};
    command(panel, 0x2a, columns, sizeof(columns));
    command(panel, 0x2b, rows, sizeof(rows));
    digitalWrite(panel.dc, LOW);
    digitalWrite(panel.cs, LOW);
    send_byte(0x2c);
    digitalWrite(panel.dc, HIGH);
    const char label[] = {'D', 'I', 'S', 'P', 'L', 'A', 'Y', ' ', number};
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 160; ++x) {
            const bool lit = text_pixel("CNTRL", 5, x - 50, y - 32)
                || text_pixel(label, 9, x - 26, y - 72);
            send_byte(lit ? 0xff : 0);
            send_byte(lit ? 0xff : 0);
        }
    }
    digitalWrite(panel.cs, HIGH);
}

void initialize(const Panel &panel, char number, bool greeting) {
    command(panel, 0x01, nullptr, 0);
    delay(150);
    command(panel, 0x11, nullptr, 0);
    delay(500);
    // Panel register values traced at stock flash 0x60013828 and 0x60013894.
    // Records contain command, argument count, then arguments. No vendor code.
    const uint8_t settings[] = {
        0xb1, 3, 5, 0x3c, 0x3c,
        0xb2, 3, 5, 0x3c, 0x3c,
        0xb3, 6, 5, 0x3c, 0x3c, 5, 0x3c, 0x3c,
        0xb4, 1, 3,
        0xc0, 3, 0x28, 8, 4,
        0xc1, 1, 0xc0,
        0xc2, 2, 0x0d, 0,
        0xc3, 2, 0x8d, 0x2a,
        0xc4, 2, 0x8d, 0xee,
        0xc5, 1, 0x10,
        0x20, 0,
        0x3a, 1, 5,
        0xe0, 16, 4, 0x22, 7, 0x0a, 0x2e, 0x30, 0x25, 0x2a,
            0x28, 0x26, 0x2e, 0x3a, 0, 1, 3, 0x13,
        0xe1, 16, 4, 0x16, 6, 0x0d, 0x2d, 0x26, 0x23, 0x27,
            0x27, 0x25, 0x2d, 0x3b, 0, 1, 4, 0x13,
        0x36, 1, 0xa8, // Stock rotation 1, including BGR color order.
    };
    for (unsigned i = 0; i < sizeof(settings);) {
        const uint8_t code = settings[i++];
        const uint8_t count = settings[i++];
        command(panel, code, settings + i, count);
        i += count;
    }
    command(panel, 0x13, nullptr, 0);
    delay(10);
    if (greeting) draw(panel, number);
    command(panel, 0x29, nullptr, 0);
    delay(100);
}

uint8_t glyph_row(char character, unsigned row) {
    struct Glyph {
        char character;
        uint8_t rows[7];
    };
    // Only the glyphs used by this fixed greeting, drawn independently.
    static const Glyph glyphs[] = {
        {'A', {14, 17, 17, 31, 17, 17, 17}},
        {'C', {14, 17, 16, 16, 16, 17, 14}},
        {'D', {30, 17, 17, 17, 17, 17, 30}},
        {'I', {31, 4, 4, 4, 4, 4, 31}},
        {'L', {16, 16, 16, 16, 16, 16, 31}},
        {'N', {17, 25, 25, 21, 19, 19, 17}},
        {'P', {30, 17, 17, 30, 16, 16, 16}},
        {'R', {30, 17, 17, 30, 20, 18, 17}},
        {'S', {15, 16, 16, 14, 1, 1, 30}},
        {'T', {31, 4, 4, 4, 4, 4, 4}},
        {'Y', {17, 17, 10, 4, 4, 4, 4}},
        {'1', {4, 12, 4, 4, 4, 4, 14}},
        {'2', {14, 17, 1, 2, 4, 8, 31}},
        {'3', {30, 1, 1, 14, 1, 1, 30}},
    };
    for (const Glyph &glyph : glyphs) {
        if (glyph.character == character) {
            return glyph.rows[row];
        }
    }
    return 0;
}
} // namespace

void initialize_displays(bool greeting) {
    const Panel panels[] = {{15, 34, 21}, {24, 23, 22}, {27, 25, 20}};
    // Setup has already deselected all panels and configured the shared clock.
    digitalWriteFast(11, LOW);
    pinMode(11, OUTPUT);
    digitalWriteFast(34, HIGH);
    digitalWriteFast(23, HIGH);
    digitalWriteFast(25, HIGH);
    digitalWriteFast(21, HIGH);
    digitalWriteFast(22, HIGH);
    digitalWriteFast(20, HIGH);
    for (const Panel &panel : panels) {
        pinMode(panel.dc, OUTPUT);
        pinMode(panel.reset, OUTPUT);
    }
    analogWrite(6, 5); // Stock startup brightness, before drawing.
    delay(100);
    for (const Panel &panel : panels) {
        digitalWrite(panel.reset, LOW);
    }
    delay(100);
    for (const Panel &panel : panels) {
        digitalWrite(panel.reset, HIGH);
    }
    delay(200);
    begin_transfer();
    for (unsigned i = 0; i < 3; ++i) {
        initialize(panels[i], char('1' + i), greeting);
    }
    end_transfer();
    analogWrite(6, 64); // Startup default; a loaded show applies its brightness.
}

void write_display_segment(unsigned index, unsigned x, unsigned y, const uint8_t *pixels, unsigned count) {
    if (index < 3) write_display_group(1u << index, x, y, pixels, count);
}

void write_display_group(uint8_t selected, unsigned x, unsigned y, const uint8_t *pixels, unsigned count) {
    const Panel panels[] = {{15, 34, 21}, {24, 23, 22}, {27, 25, 20}};
    if (!selected || selected > 7 || x >= 160 || y >= 128 || count == 0 || count > 160 - x) return;
    begin_transfer();
    const uint8_t columns[] = {0, uint8_t(x + 1), 0, uint8_t(x + count)};
    const uint8_t rows[] = {0, uint8_t(y + 2), 0, uint8_t(y + 2)};
    for (unsigned i = 0; i < 3; ++i) if (selected & (1u << i)) {
        command(panels[i], 0x2a, columns, sizeof(columns));
        command(panels[i], 0x2b, rows, sizeof(rows));
    }
    for (unsigned i = 0; i < 3; ++i) if (selected & (1u << i)) {
        digitalWrite(panels[i].dc, LOW);
        digitalWrite(panels[i].cs, LOW);
    }
    send_byte(0x2c);
    for (unsigned i = 0; i < 3; ++i)
        if (selected & (1u << i)) digitalWrite(panels[i].dc, HIGH);
    for (unsigned i = 0; i < count * 2; ++i) send_byte(pixels[i]);
    for (unsigned i = 0; i < 3; ++i)
        if (selected & (1u << i)) digitalWrite(panels[i].cs, HIGH);
    end_transfer();
}
