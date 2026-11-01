#include "patch_switcher/renderer.h"
#include "patch_switcher/scroll_timing.h"
#include <cassert>
#include <algorithm>
#include <cstring>
#include <vector>

static uint16_t screens[3][128][160];
static uint16_t ram[3][128][display_ram_columns];
static unsigned ring = 0;
static bool check_motion = false, check_undo = false;
static unsigned checked_progress = 0;
static unsigned writes = 0, pixels_sent = 0;
static unsigned scroll_updates = 0;
static bool patch_only = false;
static bool audit_motion = false, audit_undo = false;
static unsigned audit_step = 0, audit_panel = 0, audit_origin = 0;
static uint16_t audit_strip[4 * 232][128];
struct ScrollStep {
    unsigned column = 0, offset = 0;
    uint16_t pixels[3][128] = {};
};
static std::vector<ScrollStep> scroll_trace;
static bool scroll_only = false;
static uint32_t probe_now = 0, probe_last = 0;
static uint32_t probe_batch = 0, probe_previous = 0;
static unsigned probe_steps = 0;
static unsigned probe_writes = 0;
static uint16_t probe_ram[3][128][162];
static uint16_t probe_baseline[3][128][162];
static bool probe_loading = false;
static unsigned probe_preloads = 0;

static uint16_t audit_pixel(unsigned panel, unsigned x, unsigned y, unsigned step) {
    unsigned start = panel * 232 + (audit_undo ? 232 - step : step);
    return audit_strip[start + x][y];
}

static uint16_t rounded_background(unsigned x, unsigned y, uint16_t color) {
    // Expected discrete 8-pixel arc, independent of the renderer's circle test.
    constexpr unsigned cut[] = {5, 3, 2, 1, 1};
    unsigned edge = std::min(y, 127 - y);
    return edge < 5 && std::min(x, 159 - x) < cut[edge] ? 0 : color;
}

void scroll_display_panorama(unsigned offset) {
    if (scroll_only) {
        assert(std::memcmp(probe_ram, ram, sizeof(ram)) == 0);
        bool undo = (probe_steps / 232) % 2;
        assert(probe_writes == 3);
        probe_writes = 0;
        assert(offset == (ring + (undo ? 161 : 1)) % 162);
        if (probe_steps % 232 == 0) probe_last = probe_now;
        if (probe_steps % 2 == 0) {
            if (probe_steps % 232) {
                assert(uint32_t(probe_now - probe_batch) >= 4311);
                assert(uint32_t(probe_now - probe_previous) >= 1000);
            }
            probe_batch = probe_now;
        }
        probe_previous = probe_now;
        if (probe_steps % 232 == 231) {
            assert(uint32_t(probe_now - probe_last) >= 495000);
            assert(uint32_t(probe_now - probe_last) <= 520000);
        }
        ++probe_steps;
    }
    if (audit_motion) {
        assert(audit_panel == 3); // All three writes must precede this command.
        ++audit_step; audit_panel = 0;
        scroll_trace.back().offset = offset;
        assert(offset == (audit_origin + (audit_undo ? 324 - audit_step : audit_step)) % 162);
        if (audit_step <= 3 || audit_step == 7 || audit_step == 8
            || audit_step == 161 || audit_step == 162 || audit_step == 163 || audit_step == 232)
            std::printf("audit %s origin=%u step=%u write=%u offset=%u\n",
                audit_undo ? "right" : "left", audit_origin, audit_step,
                (offset + (audit_undo ? 0 : 159)) % 162, offset);
        for (unsigned panel = 0; panel < 3; ++panel)
            for (unsigned y = 0; y < 128; ++y)
                for (unsigned x = 0; x < 160; ++x)
                    assert(ram[panel][y][(offset + x) % 162] == audit_pixel(panel, x, y, audit_step));
    }
    ++scroll_updates;
    if (check_motion) checked_progress += check_undo ? (ring + display_ram_columns - offset) % display_ram_columns : (offset + display_ram_columns - ring) % display_ram_columns;
    ring = offset;
    for (unsigned panel = 0; panel < 3; ++panel)
        for (unsigned y = 0; y < 128; ++y)
                for (unsigned x = 0; x < 160; ++x) screens[panel][y][x] = ram[panel][y][(ring + x) % display_ram_columns];
    if (check_motion) {
        assert(checked_progress <= display_pitch);
        for (unsigned panel = 0; panel < 3; ++panel) for (unsigned x = 0; x < 160; ++x) {
            auto sample = panorama_sample(panel, x, checked_progress, check_undo);
            uint16_t expected = sample.x >= 160 ? 0 : uint16_t(0x1111 * (sample.pane + (check_undo ? 2 : 1)));
            for (unsigned y = 0; y < 128; ++y)
                assert(screens[panel][y][x] == (sample.x >= 160 ? 0 : rounded_background(sample.x, y, expected)));
        }
    }
}

void initialize_display_panorama() {
    bool checking = scroll_only;
    if (checking) { probe_loading = true; probe_preloads = 0; scroll_only = false; }
    scroll_display_panorama(0);
    scroll_only = checking;
}

void write_display_column(unsigned panel, unsigned column, const uint8_t *pixels) {
    if (scroll_only && probe_loading) {
        assert(panel == probe_preloads / 162 && column == probe_preloads % 162);
        for (unsigned y = 0; y < 128; ++y) {
            unsigned pixel = unsigned(pixels[2 * y]) << 8 | pixels[2 * y + 1];
            assert(pixel == probe_baseline[panel][y][column]);
            probe_ram[panel][y][column] = pixel;
        }
        if (++probe_preloads == 486) probe_loading = false;
    } else if (scroll_only) {
        bool undo = (probe_steps / 232) % 2;
        assert(panel == probe_writes++);
        unsigned next = (ring + (undo ? 161 : 1)) % 162;
        assert(column == (next + (undo ? 0 : 159)) % 162);
        for (unsigned y = 0; y < 128; ++y) {
            unsigned pixel = unsigned(pixels[2 * y]) << 8 | pixels[2 * y + 1];
            assert(pixel == (y % 16 < 8 ? 0xffffu : 0u));
            probe_ram[panel][y][column] = pixel;
        }
    }
    assert(panel < 3 && column < display_ram_columns);
    ++writes; pixels_sent += 128;
    unsigned x = (column + display_ram_columns - ring) % display_ram_columns;
    if (audit_motion) {
        if (!panel) { scroll_trace.emplace_back(); scroll_trace.back().column = column; }
        assert(scroll_trace.back().column == column);
        for (unsigned y = 0; y < 128; ++y)
            scroll_trace.back().pixels[panel][y] = unsigned(pixels[2 * y]) << 8 | pixels[2 * y + 1];
        assert(panel == audit_panel++ && x == (audit_undo ? 161 : 160));
        for (unsigned y = 0; y < 128; ++y)
            assert((unsigned(pixels[2 * y]) << 8 | pixels[2 * y + 1])
                == audit_pixel(panel, audit_undo ? 0 : 159, y, audit_step + 1));
    }
    // Column writes happen before the scroll register update. Updating a
    // visible column here produces the wrong-image sliver at the outgoing edge.
    if (check_motion && checked_progress < display_pitch) assert(x >= 160);
    for (unsigned y = 0; y < 128; ++y) {
        uint16_t pixel = uint16_t(pixels[2 * y]) << 8 | pixels[2 * y + 1];
        if (patch_only) {
            assert(panel == 2);
            if (y < 90 || y >= 98) assert(screens[panel][y][x] == pixel);
        }
        ram[panel][y][column] = pixel;
        if (x < 160) screens[panel][y][x] = pixel;
    }
}

static void drain(DisplayRenderer &renderer) {
    for (unsigned i = 0; i < 3 * 512; ++i) {
        unsigned before = writes, glyphs = ST7735_t3::glyphs;
        renderer.draw();
        assert(writes - before <= 1 && ST7735_t3::glyphs - glyphs <= 1);
        assert(writes == before || ST7735_t3::glyphs == glyphs);
    }
    assert(renderer.ready());
}

// Hypothetical panels, not measured timing. Scan state is private to the replay:
// schedules below depend only on step number, elapsed time and bus occupancy.
static void replay_delayed_offsets() {
    enum Schedule { batches, even, command_first, slow_control };
    struct Model { unsigned period, active; int direction; };
    struct Result {
        unsigned overwritten = 0, incoming = 0, unprepared = 0, edge_width = 0;
        unsigned scanned_wrong = 0, failing_phases = 0;
    };
    const Model models[] = {{0, 0, 0}, {17000, 0, 0},
        {17000, 17000, 1}, {17000, 17000, -1},
        {17000, 10000, 1}, {17000, 10000, -1}};
    for (auto model : models) for (auto schedule : {batches, even, command_first, slow_control}) {
        const unsigned period = model.period;
        Result total;
        unsigned phases = period ? 17 : 1;
        for (unsigned phase = 0; phase < phases; ++phase) {
            unsigned incoming_before = total.incoming;
            unsigned scanned_before = total.scanned_wrong, final_scanned = 0;
            bool final_check = false;
            uint16_t memory[3][162][128] = {};
            for (unsigned panel = 0; panel < 3; ++panel)
                for (unsigned y = 0; y < 128; ++y)
                    for (unsigned x = 0; x < 162; ++x)
                        memory[panel][(audit_origin + x) % 162][y] = x < 160
                            ? audit_pixel(panel, x, y, 0) : 0xdead;
            unsigned visible = audit_origin, requested = audit_origin;
            unsigned visible_step = 0, requested_step = 0;
            unsigned frame_start = phase * 1000, next_scan = frame_start, line = 0, clock = 0;
            auto latch = [&]() {
                visible = requested; visible_step = requested_step;
                if (model.direction) return;
                for (unsigned panel = 0; panel < 3; ++panel)
                    for (unsigned x = 0; x < 160; ++x) {
                        unsigned source = panel * 232 + (audit_undo ? 232 - visible_step : visible_step) + x;
                        bool wrong = std::memcmp(memory[panel][(visible + x) % 162], audit_strip[source], 256) != 0;
                        total.unprepared += wrong;
                    }
            };
            auto advance = [&](unsigned deadline) {
                // Scan/latch events tied with a bus event happen first.
                while (period && next_scan <= deadline) {
                    if (!line) latch();
                    if (model.direction) {
                        unsigned x = model.direction > 0 ? line : 159 - line;
                        for (unsigned panel = 0; panel < 3; ++panel) {
                            unsigned source = panel * 232 + (audit_undo ? 232 - visible_step : visible_step) + x;
                            bool wrong = std::memcmp(memory[panel][(visible + x) % 162], audit_strip[source], 256) != 0;
                            total.scanned_wrong += wrong;
                            if (final_check && visible_step == 232) { assert(!wrong); ++final_scanned; }
                        }
                    }
                    if (!model.direction || ++line == 160) {
                        line = 0; frame_start += period;
                    }
                    next_scan = frame_start + line * model.active / 160;
                }
                clock = deadline;
            };
            unsigned batch_ms = 0;
            for (unsigned step = 0; step < scroll_trace.size(); ++step) {
                const auto &event = scroll_trace[step];
                while (transition_progress(batch_ms, 500) < step + 1) ++batch_ms;
                unsigned deadline = schedule == even ? (step + 1) * 500000 / 232
                    : schedule == slow_control ? (step + 1) * 20000 : batch_ms * 1000;
                advance(std::max(clock, deadline));
                auto command = [&]() {
                    advance(clock + 9); // Three 3-byte SSA commands at 8 MHz.
                    requested = event.offset; requested_step = step + 1;
                    if (!period) latch();
                };
                if (schedule == command_first) command();
                for (unsigned panel = 0; panel < 3; ++panel) {
                    advance(clock + 267); // CASET, RASET, RAMWR and 128 pixels.
                    unsigned x = (event.column + 162 - visible) % 162;
                    bool wrong = false, incoming = false;
                    for (unsigned y = 0; y < 128; ++y) {
                        uint16_t pixel = event.pixels[panel][y];
                        if (x < 160 && pixel != audit_pixel(panel, x, y, visible_step)) {
                            wrong = true;
                            incoming |= pixel != 0; // Exclude black gap-only damage.
                        }
                        memory[panel][event.column][y] = pixel;
                    }
                    total.overwritten += wrong; total.incoming += incoming;
                    if (wrong) {
                        unsigned width = audit_undo ? 160 - x : x + 1;
                        total.edge_width = std::max(total.edge_width, width);
                        assert(width <= 16); // Confined to the outgoing edge.
                    }
                }
                if (schedule != command_first) command();
            }
            final_check = true;
            advance(clock + 2 * period);
            assert(visible_step == 232);
            if (model.direction) {
                assert(final_scanned >= 3 * 160);
                total.failing_phases += total.scanned_wrong > scanned_before;
            }
            if (period && (schedule == batches || schedule == even))
                assert(total.incoming > incoming_before); // Potential damage; scan may already have passed.
            for (unsigned panel = 0; panel < 3; ++panel)
                for (unsigned x = 0; x < 160; ++x)
                    for (unsigned y = 0; y < 128; ++y)
                        assert(memory[panel][(visible + x) % 162][y] == audit_pixel(panel, x, y, 232));
        }
        const char *names[] = {"batches", "even", "command-first", "slow-control"};
        std::printf("lag-model %s origin=%u period=%u scan=%d active=%u schedule=%s phases=%u overwritten=%u incoming=%u unprepared=%u edge-width=%u scanned-wrong=%u failing-phases=%u\n",
            audit_undo ? "right" : "left", audit_origin, period, model.direction, model.active, names[schedule], phases,
            total.overwritten, total.incoming, total.unprepared, total.edge_width, total.scanned_wrong, total.failing_phases);
        if (model.direction) {
            if (schedule == slow_control) assert(total.scanned_wrong == 0);
            else if ((model.direction > 0) != audit_undo && schedule != command_first)
                assert(total.scanned_wrong == 0); // Outgoing edge was already scanned.
            else if ((model.direction > 0) == audit_undo)
                assert(total.scanned_wrong > 0 && total.failing_phases == phases);
        } else if (schedule == command_first) assert(total.unprepared > 0);
        else if (!period || schedule == slow_control)
            assert(total.overwritten == 0 && total.unprepared == 0);
        else assert(total.overwritten > 0 && total.incoming > 0);
    }
}

static cntrl::Pane audit_pane(unsigned identity) {
    cntrl::Pane view;
    view.graphics.pad = 0;
    view.graphics.background = 0;
    for (unsigned band = 0; band < 8; ++band) {
        auto &line = view.lines[band];
        line.graphics.pad = 0;
        line.graphics.background = ((identity + band) % 4 < 2) ? 0xffff : 0x4208;
        line.graphics.foreground = 0;
        line.graphics.line_height = 16;
        line.x = 0; line.y = int16_t(band * 16);
        std::snprintf(line.text, sizeof(line.text), "%c01234567890123456789012345", 'A' + identity);
    }
    return view;
}

static void audit_animation_batches() {
    DisplayRenderer renderer;
    drain(renderer);
    // Build a literal four-pane panorama from independently rendered snapshots.
    // The reference deliberately does not call panorama_sample().
    for (unsigned identity = 0; identity < 4; ++identity) {
        renderer.show(0, audit_pane(identity)); drain(renderer);
        for (unsigned y = 0; y < 128; ++y)
            for (unsigned x = 0; x < 160; ++x)
                audit_strip[identity * 232 + x][y] = screens[0][y][x];
    }
    for (bool undo : {false, false, true, true}) {
        for (unsigned panel = 0; panel < 3; ++panel)
            renderer.show(panel, audit_pane(panel + unsigned(undo)));
        drain(renderer);
        // A newly exposed, unwritten column must fail the pixel comparison.
        for (unsigned panel = 0; panel < 3; ++panel)
            for (unsigned y = 0; y < 128; ++y)
                for (unsigned x = 160; x < 162; ++x) ram[panel][y][(ring + x) % 162] = 0xdead;
        audit_origin = ring; audit_undo = undo; audit_step = audit_panel = 0;
        scroll_trace.clear();
        renderer.transition(undo, 500);
        for (unsigned panel = 0; panel < 3; ++panel)
            renderer.show(panel, audit_pane(panel + unsigned(!undo)));
        for (unsigned i = 0; i < 1600; ++i) renderer.draw(0);
        audit_motion = true;
        unsigned batches7 = 0, batches8 = 0;
        for (unsigned now = 1; now <= 500; ++now) {
            unsigned before = audit_step;
            for (unsigned work = 0; work < 8; ++work) {
                renderer.draw(now);
                if (audit_step == 232) break;
            }
            unsigned steps = audit_step - before;
            assert(steps == 0 || steps == 7 || steps == 8);
            batches7 += steps == 7; batches8 += steps == 8;
        }
        assert(audit_step == 232 && audit_panel == 0 && batches7 == 8 && batches8 == 22);
        audit_motion = false;
        replay_delayed_offsets();
        drain(renderer);
    }

    ScrollTimingProbe probe;
    unsigned before = scroll_updates;
    probe.poll(0); assert(scroll_updates == before);
    probe.arm();
    unsigned initial_writes = writes;
    for (unsigned i = 0; i < 486; ++i) {
        unsigned previous = writes;
        probe.poll(0); assert(writes == previous + 1);
    }
    assert(writes == initial_writes + 486 && ring == 0);
    // Check visible and hidden RAM, including every horizontal stripe boundary.
    for (unsigned panel = 0; panel < 3; ++panel)
        for (unsigned x = 0; x < 162; ++x)
            for (unsigned y = 0; y < 128; ++y) {
                assert(ram[panel][y][x] == (y % 16 >= 8 ? 0x7bef : 0));
            }
    std::memcpy(probe_ram, ram, sizeof(ram));
    std::memcpy(probe_baseline, ram, sizeof(ram));
    scroll_only = true; probe_steps = 0;
    before = scroll_updates;
    probe.poll(10000); assert(scroll_updates == before);
    for (unsigned repetition = 0; repetition < 2; ++repetition) {
        probe.start();
        // Exercise micros rollover and a complete C-triggered repeat.
        for (uint32_t elapsed = 0; elapsed < 22000000; elapsed += 100) {
            probe_now = UINT32_MAX - 500000 + repetition * 22000000 + elapsed;
            probe.poll(probe_now);
        }
        assert(scroll_updates == before + (repetition + 1) * (8 * 232 + 8) && ring == 0);
        assert(writes == initial_writes + 486 + (repetition + 1) * (8 * 232 * 3 + 8 * 486));
    }
    probe.start(); probe.poll(probe_now);
    probe_now += 4400; probe.poll(probe_now); // First column of a batch.
    unsigned after_first = scroll_updates;
    probe_now += 20000; probe.poll(probe_now); // Delayed second column.
    assert(scroll_updates == after_first + 1);
    probe_now += 100; probe.poll(probe_now);
    assert(scroll_updates == after_first + 1); // No immediate catch-up batch.
    probe.stop(); before = scroll_updates;
    probe.poll(probe_now + 1000000);
    assert(!probe.active() && scroll_updates == before);
    scroll_only = false;
}

static void blank(unsigned panel, uint16_t background = 0xffff) {
    for (unsigned y = 0; y < 128; ++y) for (unsigned x = 0; x < 160; ++x)
        assert(screens[panel][y][x] == rounded_background(x, y, background));
}

static void padding(unsigned panel, unsigned pad, uint16_t color) {
    for (unsigned y = 0; y < 128; ++y) for (unsigned x = 0; x < 160; ++x)
        if (x < pad || y < pad || x >= 160 - pad || y >= 128 - pad)
            assert(screens[panel][y][x] == rounded_background(x, y, color));
}

static void sparse_text() {
    // One two-pixel diagonal glyph, referenced twice in a prepared UTF-8 run.
    font_assets::Entry entry;
    entry.bytes[0] = 24; entry.bytes[1] = 2; entry.bytes[7] = 1;
    entry.bytes[9] = 1; entry.bytes[11] = 1;
    entry.bytes[13] = 3; entry.bytes[14] = 2;
    entry.bytes[15] = 46; entry.bytes[19] = 2;
    auto data = std::make_unique<uint8_t[]>(48);
    data[0] = 9; data[4] = 2; data[6] = 2; data[10] = 2;
    data[16] = '1'; data[22] = 3;
    data[24] = 2; data[28] = 5; data[30] = 2;
    data[32] = 0xc3; data[33] = 0xa9; data[42] = 3;
    data[46] = 128; data[47] = 64;
    assert(font_assets::valid_pixels(entry, data.get()));
    font_assets::cache.begin();
    assert(font_assets::cache.add(123, entry, std::move(data)));
    font_assets::cache.finish(true);
    ShowGraphics graphics; cntrl::ScreenGraphics settings;
    settings.font = 255; settings.asset = 123; settings.size = 24;
    settings.foreground = 0x1234; settings.background = 0xffff; settings.pad = 4;
    graphics.configure(settings);
    assert(graphics.sparse_letter(settings, "é", 0, 5, 10, false));
    assert(graphics.sparse_letter(settings, "é", 1, 5, 10, false));
    assert(!graphics.sparse_letter(settings, "é", 2, 5, 10, false));
    auto pixels = graphics.getFrameBuffer();
    assert(pixels[8 * 160 + 5] == 0x1234 && pixels[9 * 160 + 6] == 0x1234);
    assert(pixels[8 * 160 + 8] == 0x1234 && pixels[9 * 160 + 9] == 0x1234);
    assert(pixels[8 * 160 + 6] == 0xffff);
    assert(graphics.sparse_letter(settings, "11", 1, 10, 20, false));
    assert(pixels[18 * 160 + 13] == 0x1234);
    assert(!graphics.sparse_letter(settings, "2", 0, 10, 20, false));
    assert(graphics.sparse_letter(settings, "é", 0, 0, 2, false));
    assert(pixels[0] == 0xffff); // Padding clips sparse glyphs too.
    assert(graphics.sparse_letter(settings, "é", 0, 0, 30, true));
    assert(pixels[28 * 160 + 77] == 0x1234);
    assert(cntrl::label_valid(reinterpret_cast<const uint8_t *>("Café"), 6));
    assert(!cntrl::label_valid(reinterpret_cast<const uint8_t *>("\xc0\xaf"), 3));
    assert(!cntrl::label_valid(reinterpret_cast<const uint8_t *>("\xed\xa0\x80"), 4));
    font_assets::cache.begin(); font_assets::cache.finish(true);
}

int main() {
    sparse_text();
    {
        ShowGraphics graphics;
        cntrl::ScreenGraphics settings;
        settings.font = 5; settings.size = 28;
        graphics.configure(settings);
        graphics.setCursor(10, 10); graphics.write('S');
        uint16_t before[160 * 128];
        std::memcpy(before, graphics.getFrameBuffer(), sizeof(before));
        // An overlapping space must preserve the previous letter's strokes.
        graphics.setCursor(10, 10); graphics.write(' ');
        assert(std::memcmp(before, graphics.getFrameBuffer(), sizeof(before)) == 0);
    }
    std::memset(screens, 123, sizeof(screens));
    DisplayRenderer renderer;
    assert(ShowGraphics::native_font(0, 32) == &Arial_32);
    assert(!ShowGraphics::supports_font(0, 56)); // No invented, magnified size.
    assert(!renderer.ready());
    cntrl::Pane empty;
    drain(renderer);
    assert(writes == 480 && pixels_sent == 3 * 160 * 128);
    for (unsigned i = 0; i < 3; ++i) blank(i);
    cntrl::Pane empty_sparse;
    empty_sparse.graphics.background = 0;
    empty_sparse.lines[0].graphics.font = 255;
    renderer.show(0, empty_sparse); drain(renderer); blank(0, 0);
    renderer.show(0, empty); drain(renderer); blank(0);
    cntrl::Pane corner_text;
    corner_text.graphics.pad = corner_text.lines[0].graphics.pad = 0;
    corner_text.graphics.background = 0x07e0;
    corner_text.lines[0].graphics.background = 0x001f;
    corner_text.lines[0].graphics.foreground = 0xf800;
    corner_text.lines[0].x = 0; corner_text.lines[0].y = 0;
    std::strcpy(corner_text.lines[0].text, "A");
    renderer.show(0, corner_text); drain(renderer);
    assert(screens[0][0][0] == 0 && screens[0][4][0] == 0);
    assert(screens[0][1][4] == 0xf800); // Text inside the arc survives.
    assert(screens[0][0][5] == 0x001f && screens[0][64][80] == 0x001f);
    assert(screens[0][127][159] == 0 && screens[0][127][154] == 0x001f);
    renderer.show(0, empty); drain(renderer); blank(0);
    cntrl::Pane no_setups;
    no_setups.lines[1].centered = true;
    no_setups.lines[1].graphics.size = no_setups.lines[1].graphics.line_height = 16;
    std::strcpy(no_setups.lines[1].text, "No setups");
    renderer.show(0, no_setups); drain(renderer);
    assert(screens[0][56][26] == 0 && screens[0][55][26] == 0xffff);
    renderer.show(0, empty); drain(renderer);
    cntrl::Pane editor;
    editor.lines[0].graphics.size = editor.lines[0].graphics.line_height = 16;
    editor.lines[1].graphics.size = 12; editor.lines[1].graphics.line_height = 16;
    editor.lines[1].wrap = true;
    std::strcpy(editor.lines[0].text, "Patch");
    std::memset(editor.lines[1].text, 'X', 63); editor.lines[1].text[63] = 0;
    renderer.show(0, editor); drain(renderer);
    for (unsigned y : {6u, 26u, 46u, 66u, 86u, 106u}) assert(screens[0][y][4] == 0);
    assert(screens[0][106][150] == 0xffff); // Final row ends before the right edge.
    padding(0, 4, 0xffff);
    renderer.show(0, empty); drain(renderer);
    unsigned before = writes;
    renderer.show(0, empty); renderer.show(3, empty); drain(renderer);
    assert(writes == before);

    cntrl::Pane scaled;
    std::strcpy(scaled.lines[0].text, "A"); std::strcpy(scaled.lines[1].text, "B");
    scaled.lines[0].x = scaled.lines[1].x = 4;
    scaled.lines[1].graphics.foreground = 0;
    scaled.lines[0].graphics.size = scaled.lines[0].graphics.line_height = 32;
    scaled.lines[1].graphics.size = scaled.lines[1].graphics.line_height = 16;
    renderer.show(0, scaled); drain(renderer);
    assert(screens[0][22][4] == 0 && screens[0][21][4] == 0xffff);
    assert(screens[0][49][4] == 0 && screens[0][50][4] == 0xffff);
    assert(screens[0][90][4] == 0 && screens[0][103][4] == 0);
    assert(screens[0][104][4] == 0xffff && screens[0][89][4] == 0xffff);
    padding(0, 4, 0xffff);
    scaled.lines[0].text[0] = 0;
    renderer.show(0, scaled); drain(renderer);
    assert(screens[0][56][4] == 0 && screens[0][55][4] == 0xffff);
    assert(screens[0][69][4] == 0 && screens[0][70][4] == 0xffff);
    scaled.lines[1].text[0] = 0;
    renderer.show(0, scaled); drain(renderer); blank(0);
    renderer.show(0, empty); drain(renderer);

    cntrl::Pane back;
    back.graphics.background = 0;
    std::strcpy(back.lines[0].text, "Back");
    back.lines[0].graphics.background = 0;
    back.lines[0].graphics.foreground = 0xffff;
    back.lines[0].graphics.size = back.lines[0].graphics.line_height = 16;
    back.lines[0].x = 108; back.lines[0].y = 4;
    renderer.show(2, back); drain(renderer);
    unsigned label_pixels = 0;
    for (unsigned y = 0; y < 128; ++y) for (unsigned x = 0; x < 160; ++x) {
        if (screens[2][y][x]) {
            assert(x >= 108 && x < 156 && y >= 4 && y < 20);
            ++label_pixels;
        }
    }
    assert(label_pixels);
    renderer.show(2, empty); drain(renderer);

    cntrl::Pane bands;
    for (unsigned i = 0; i < 3; ++i) {
        std::strcpy(bands.lines[i].text, "A");
        bands.lines[i].graphics.background = uint16_t(1u << i);
    }
    renderer.show(0, bands); drain(renderer);
    assert(screens[0][4][150] == 1 && screens[0][43][150] == 1);
    assert(screens[0][44][150] == 2 && screens[0][83][150] == 2);
    assert(screens[0][84][150] == 4 && screens[0][123][150] == 4);
    assert(screens[0][3][150] == 0xffff && screens[0][124][150] == 0xffff);
    bands.lines[1].graphics.background = 8;
    renderer.show(0, bands); drain(renderer);
    assert(screens[0][44][150] == 8 && screens[0][83][150] == 8);
    renderer.show(0, empty); drain(renderer);

    cntrl::Pane view;
    std::strcpy(view.lines[0].text, "trumpet");
    std::strcpy(view.lines[1].text, "A very long patch name that must be clipped");
    view.lines[0].x = 40; view.lines[1].x = 4;
    view.graphics.foreground = 0xf800; view.graphics.background = 0x001f;
    view.lines[0].graphics = view.lines[1].graphics = view.graphics;
    view.lines[1].graphics.foreground = view.graphics.foreground;
    renderer.show(2, view); drain(renderer);
    assert(screens[2][30][40] == 0xf800 && screens[2][90][4] == 0xf800);
    assert(screens[2][29][40] == 0x001f);
    padding(2, 4, 0x001f);
    before = writes;
    renderer.show(2, view); drain(renderer); assert(writes == before);
    std::memset(screens, 123, sizeof(screens)); // Splash bypassed the cached pixels.
    renderer.invalidate(); assert(!renderer.ready()); drain(renderer);
    assert(writes == before + 480);
    padding(2, 4, 0x001f); blank(0); blank(1);

    uint16_t solid[128][160];
    std::memcpy(solid, screens[2], sizeof(solid));
    view.lines[1].stippled = true;
    patch_only = true;
    renderer.show(2, view); drain(renderer);
    for (unsigned y = 0; y < 128; ++y) for (unsigned x = 0; x < 160; ++x) {
        bool hole = y >= 64 && y < 124 && x >= 4 && x < 156 && ((x + y) & 1);
        assert(screens[2][y][x] == (hole ? view.lines[1].graphics.background : solid[y][x]));
    }
    assert(screens[2][90][4] == 0xf800 && screens[2][90][5] == 0x001f);
    view.lines[1].stippled = false;
    renderer.show(2, view); drain(renderer);
    assert(std::memcmp(solid, screens[2], sizeof(solid)) == 0);
    patch_only = false;

    std::strcpy(view.lines[1].text, "Trumpet");
    renderer.show(2, view); drain(renderer);
    assert(screens[2][90][80] == 0x001f);
    renderer.show(2, empty); renderer.draw();
    view.graphics.pad = 20; view.graphics.size = 16; view.graphics.line_height = 16;
    view.lines[0].graphics = view.lines[1].graphics = view.graphics;
    view.lines[0].x = view.lines[1].x = 0;
    renderer.show(2, view); drain(renderer);
    padding(2, 20, 0x001f);
    assert(screens[2][78][24] == 0xf800);
    blank(0); blank(1);
    renderer.show(2, empty); drain(renderer); blank(2);

    view = empty;
    view.graphics.background = 0x07e0;
    renderer.show(1, view); drain(renderer);
    blank(1, 0x07e0);
    assert(ShowGraphics::supports_font(0, 8) && ShowGraphics::supports_font(1, 13));
    assert(ShowGraphics::supports_font(2, 96) && !ShowGraphics::supports_font(3, 8));
    assert(ShowGraphics::supports_font(5, 28)); // Fixed cable-test UI title.
    assert(!ShowGraphics::supports_font(5, 32));
    assert(!ShowGraphics::supports_font(8, 24)); // Shows use sparse SD assets.
    assert(!ShowGraphics::supports_font(10, 24));
    assert(ShowGraphics::supports_font(0, 13) && !ShowGraphics::supports_font(1, 15));

    // A complete send shifts C/R left and introduces the new grey candidate.
    for (unsigned i = 0; i < 3; ++i) {
        cntrl::Pane pane; pane.graphics.background = uint16_t(0x1111 * (i + 1));
        renderer.show(i, pane);
    }
    drain(renderer);
    renderer.transition(false, 2000);
    check_motion = true; checked_progress = 0;
    for (unsigned i = 0; i < 3; ++i) {
        cntrl::Pane pane; pane.graphics.background = uint16_t(0x1111 * (i + 2));
        renderer.show(i, pane);
    }
    for (unsigned now = 0; now < 1000; ++now)
        for (unsigned work = 0; work < 8; ++work) renderer.draw(now);
    assert(!renderer.ready() && checked_progress < display_pitch);
    for (unsigned now = 1000; now < 3000; ++now)
        for (unsigned work = 0; work < 8; ++work) renderer.draw(now);
    assert(renderer.ready());
    check_motion = false; assert(checked_progress == display_pitch);
    for (unsigned i = 0; i < 3; ++i) blank(i, uint16_t(0x1111 * (i + 2)));
    assert(ring == display_pitch % display_ram_columns);
    renderer.transition(true, 500);
    check_motion = check_undo = true; checked_progress = 0;
    for (unsigned i = 0; i < 3; ++i) {
        cntrl::Pane pane; pane.graphics.background = uint16_t(0x1111 * (i + 1));
        renderer.show(i, pane);
    }
    for (unsigned now = 3000; now < 4500; ++now)
        for (unsigned work = 0; work < 8; ++work) renderer.draw(now);
    assert(renderer.ready() && ring == 0);
    check_motion = false; assert(checked_progress == display_pitch);
    for (unsigned i = 0; i < 3; ++i) blank(i, uint16_t(0x1111 * (i + 1)));
    // An unspecified duration uses the renderer default, even after an explicit one.
    renderer.transition(false, std::nullopt);
    for (unsigned now = 4500; now < 4750; ++now)
        for (unsigned work = 0; work < 8; ++work) renderer.draw(now);
    assert(!renderer.ready());
    for (unsigned now = 4750; now < 5250; ++now)
        for (unsigned work = 0; work < 8; ++work) renderer.draw(now);
    assert(renderer.ready());
    assert(transition_progress(1000, 2000) == display_pitch / 2);
    // A fast send after browsing must animate even during a candidate repaint.
    cntrl::Pane browsing;
    browsing.graphics.background = 0xf800;
    renderer.show(2, browsing); renderer.draw();
    assert(!renderer.ready());
    unsigned before_scroll = scroll_updates;
    renderer.transition(false, 500);
    browsing.graphics.background = 0x001f;
    renderer.show(1, browsing);
    for (unsigned now = 5250; now < 6250; ++now)
        for (unsigned work = 0; work < 8; ++work) renderer.draw(now);
    assert(renderer.ready() && scroll_updates > before_scroll);
    blank(1, 0x001f); blank(2, 0xf800);
    assert(transition_progress(1999, 2000) < display_pitch);
    assert(transition_progress(2000, 2000) == display_pitch);
    assert(transition_progress(500, 500) == display_pitch);
    for (unsigned panel = 0; panel < 3; ++panel) for (unsigned x = 0; x < 160; ++x) {
        auto left = panorama_sample(panel, x, display_pitch, false);
        assert(left.pane == int(panel) + 1 && left.x == x);
        auto right = panorama_sample(panel, x, display_pitch, true);
        assert(right.pane == int(panel) - 1 && right.x == x);
    }
    audit_animation_batches();
}
