#include "patch_switcher/renderer.h"
#include "patch_switcher/pages.h"
#include "patch_switcher/scroll_timing.h"
#include <array>
#include <map>
#include <cassert>
#include <algorithm>
#include <cstring>
#include <vector>

static uint16_t screens[3][128][160];
static uint16_t ram[3][128][display_ram_columns];
static unsigned ring = 0;
static unsigned panel_rings[3] = {};
static bool check_motion = false, check_undo = false;
static unsigned checked_progress = 0;
static unsigned writes = 0, pixels_sent = 0;
static bool scan_due = false;
bool switch_scan_due() { return scan_due; }
static unsigned scroll_updates = 0;
static bool patch_only = false;
static bool audit_motion = false, audit_right = false;
static unsigned audit_step = 0, audit_panel = 0, audit_origin = 0;
static uint32_t audit_now = 0, audit_last_scroll = 0;
static uint16_t audit_strip[3 * 464][128];
static uint16_t audit_images[4][160][128];
struct ScrollStep {
    unsigned column = 0, offset = 0;
    uint16_t pixels[3][128] = {};
};
static std::vector<ScrollStep> scroll_trace;
#ifdef CNTRL_DIAGNOSTICS
static std::vector<bool> refresh_settings;
void set_display_fast_refresh(bool enabled) {
    refresh_settings.push_back(enabled);
}
#endif

static uint16_t audit_pixel(unsigned panel, unsigned x, unsigned y, unsigned step) {
    unsigned start = panel * 464 + (audit_right ? 232 - step : step);
    return audit_strip[start + x][y];
}

static uint16_t rounded_background(unsigned x, unsigned y, uint16_t color) {
    // Expected discrete 8-pixel arc, independent of the renderer's circle test.
    constexpr unsigned cut[] = {5, 3, 2, 1, 1};
    unsigned edge = std::min(y, 127 - y);
    return edge < 5 && std::min(x, 159 - x) < cut[edge] ? 0 : color;
}

void scroll_display_panorama(unsigned offset) {
    if (audit_motion) {
        assert(audit_panel == 6); // Two columns on all panels before revealing either.
        audit_step += 2; audit_panel = 0;
        audit_last_scroll = audit_now;
        scroll_trace.back().offset = offset;
        assert(offset == (audit_origin + (audit_right ? 324 - audit_step : audit_step)) % 162);
        if (audit_step <= 3 || audit_step == 7 || audit_step == 8
            || audit_step == 161 || audit_step == 162 || audit_step == 163 || audit_step == 232)
            std::printf("audit %s origin=%u step=%u write=%u offset=%u\n",
                audit_right ? "right" : "left", audit_origin, audit_step,
                (offset + (audit_right ? 0 : 159)) % 162, offset);
        for (unsigned panel = 0; panel < 3; ++panel)
            for (unsigned y = 0; y < 128; ++y)
                for (unsigned x = 0; x < 160; ++x)
                    assert(ram[panel][y][(offset + x) % 162] == audit_pixel(panel, x, y, audit_step));
    }
    ++scroll_updates;
    if (check_motion) checked_progress += !check_undo ? (ring + display_ram_columns - offset) % display_ram_columns : (offset + display_ram_columns - ring) % display_ram_columns;
    ring = offset;
    for (auto &value : panel_rings) value = offset;
    for (unsigned panel = 0; panel < 3; ++panel)
        for (unsigned y = 0; y < 128; ++y)
                for (unsigned x = 0; x < 160; ++x) screens[panel][y][x] = ram[panel][y][(ring + x) % display_ram_columns];
    if (check_motion) {
        assert(checked_progress <= display_pitch);
        for (unsigned panel = 0; panel < 3; ++panel) for (unsigned x = 0; x < 160; ++x) {
            int source = int(x) + (check_undo ? int(checked_progress) : -int(checked_progress));
            unsigned identity = panel + (check_undo ? 1 : 2);
            if (source < 0 || source >= 160) {
                source += check_undo ? -232 : 232;
                identity = panel + (check_undo ? 2 : 1);
            }
            for (unsigned y = 0; y < 128; ++y) {
                uint16_t expected = source < 0 || source >= 160 ? 0
                    : rounded_background(unsigned(source), y, uint16_t(0x1111 * identity));
                assert(screens[panel][y][x] == expected);
            }
        }
    }
}

void scroll_display_panel(unsigned panel, unsigned offset) {
    panel_rings[panel] = offset;
    for (unsigned y = 0; y < 128; ++y)
        for (unsigned x = 0; x < 160; ++x)
            screens[panel][y][x] = ram[panel][y][(offset + x) % display_ram_columns];
}

void initialize_display_panorama() {
    scroll_display_panorama(0);
}

void write_display_column(unsigned panel, unsigned column, const uint8_t *pixels) {
    write_display_column_segment(panel, column, 0, pixels, 128);
}

void write_display_column_segment(unsigned panel, unsigned column, unsigned first, const uint8_t *pixels, unsigned count) {
    assert(count && first + count <= 128);
    assert(panel < 3 && column < display_ram_columns);
    ++writes; pixels_sent += count;
    unsigned x = (column + display_ram_columns - panel_rings[panel]) % display_ram_columns;
    if (audit_motion) {
        if (!panel) { scroll_trace.emplace_back(); scroll_trace.back().column = column; }
        assert(scroll_trace.back().column == column);
        for (unsigned y = 0; y < 128; ++y)
            scroll_trace.back().pixels[panel][y] = unsigned(pixels[2 * y]) << 8 | pixels[2 * y + 1];
        if (audit_panel == 0 && audit_step) {
            assert(uint32_t(audit_now - audit_last_scroll) >= 500);
        }
        const unsigned staged = audit_panel / 3;
        assert(panel == audit_panel % 3 && x == (audit_right ? 161 - staged : 160 + staged));
        ++audit_panel;
        scroll_trace.back().offset = (audit_origin + (audit_right ? 324 - audit_step - staged - 1 : audit_step + staged + 1)) % 162;
        for (unsigned y = 0; y < 128; ++y)
            assert((unsigned(pixels[2 * y]) << 8 | pixels[2 * y + 1])
                == audit_pixel(panel, audit_right ? 0 : 159, y, audit_step + staged + 1));
    }
    // Column writes happen before the scroll register update. Updating a
    // visible column here produces the wrong-image sliver at the outgoing edge.
    if (check_motion && checked_progress < display_pitch) assert(x >= 160);
    for (unsigned y = first; y < first + count; ++y) {
        uint16_t pixel = uint16_t(pixels[2 * (y - first)]) << 8 | pixels[2 * (y - first) + 1];
        if (patch_only) {
            assert(panel == 2);
            if (y < 90 || y >= 98) assert(screens[panel][y][x] == pixel);
        }
        ram[panel][y][column] = pixel;
        if (x < 160) screens[panel][y][x] = pixel;
    }
}

static DisplayBand simulated_bands[2];
static unsigned active_dma = 2, queued_dma = 2, dma_polls = 0;
static std::vector<uint8_t> in_flight_pixels;
DisplayBand *acquire_display_band() {
    if (queued_dma < 2) return nullptr;
    for (unsigned i = 0; i < 2; ++i) if (i != active_dma) return &simulated_bands[i];
    return nullptr;
}
void queue_display_band(DisplayBand &band) { queued_dma = unsigned(&band - simulated_bands); }
bool display_spi_busy() { return active_dma < 2; }
bool display_dma_pending() { return display_spi_busy() || queued_dma < 2; }
bool take_display_dma_error() { return false; }
void start_display_dma() {
    if (active_dma < 2 || queued_dma >= 2) return;
    active_dma = queued_dma; queued_dma = 2; dma_polls = 0;
    auto &band = simulated_bands[active_dma];
    assert(band.columns <= 8 && (band.column + 1) % 162 + band.columns <= 162);
    in_flight_pixels.assign(band.pixels, band.pixels + 2 * band.columns * band.count);
}
void poll_display_dma() {
    if (active_dma >= 2 || ++dma_polls < 2) return;
    auto &band = simulated_bands[active_dma];
    assert(!std::memcmp(band.pixels, in_flight_pixels.data(), in_flight_pixels.size()));
    for (unsigned x = 0; x < band.columns; ++x)
        write_display_column_segment(band.panel, (band.column + x) % 162,
            band.first, band.pixels + x * band.count * 2, band.count);
    active_dma = 2;
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
                        unsigned source = panel * 464 + (audit_right ? 232 - visible_step : visible_step) + x;
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
                            unsigned source = panel * 464 + (audit_right ? 232 - visible_step : visible_step) + x;
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
                        unsigned width = audit_right ? 160 - x : x + 1;
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
            audit_right ? "right" : "left", audit_origin, period, model.direction, model.active, names[schedule], phases,
            total.overwritten, total.incoming, total.unprepared, total.edge_width, total.scanned_wrong, total.failing_phases);
        if (model.direction) {
            if (schedule == slow_control) assert(total.scanned_wrong == 0);
            else if ((model.direction > 0) != audit_right && schedule != command_first)
                assert(total.scanned_wrong == 0); // Outgoing edge was already scanned.
            else if ((model.direction > 0) == audit_right)
                assert(total.scanned_wrong > 0 && total.failing_phases == phases);
        } else if (schedule == command_first) assert(total.unprepared > 0);
        else if (!period || schedule == slow_control)
            assert(total.overwritten == 0 && total.unprepared == 0);
        else assert(total.overwritten > 0 && total.incoming > 0);
    }
}

// Separate latch and fetch times; this is a host-only schedule search.
static void search_scroll_schedules() {
    struct Event {
        unsigned time, panel, step, column, pixels;
        bool command;
    };
    struct Model { unsigned active, gap; int direction; };
    struct Result {
        unsigned outgoing = 0, incoming = 0, failing = 0, worst = 0;
    };
    // Intern exact 128-pixel columns, preserving equal black/patterned columns.
    std::map<std::array<uint16_t, 128>, unsigned> columns;
    auto intern = [&](const uint16_t *pixels) {
        std::array<uint16_t, 128> value;
        std::copy(pixels, pixels + 128, value.begin());
        return columns.emplace(value, unsigned(columns.size()) + 1).first->second;
    };
    unsigned reference[3 * 464];
    for (unsigned x = 0; x < 3 * 464; ++x) {
        reference[x] = intern(audit_strip[x]);
    }
    unsigned payload[232][3];
    for (unsigned step = 0; step < 232; ++step) {
        for (unsigned panel = 0; panel < 3; ++panel) {
            payload[step][panel] = intern(scroll_trace[step].pixels[panel]);
        }
    }
    auto expected = [&](unsigned panel, unsigned step, unsigned x) {
        return reference[panel * 464 + (audit_right ? 232 - step : step) + x];
    };
    auto schedule = [&](unsigned delay, bool reverse_writes, bool slow) {
        std::vector<Event> events;
        unsigned clock = 0, previous = 0;
        const unsigned batches = slow ? 232 : 30;
        for (unsigned batch = 0; batch < batches; ++batch) {
            const unsigned target = slow ? batch + 1 : (batch + 1) * 232 / 30;
            clock = std::max(clock, slow ? (batch + 1) * 20000 : batch * 500000 / 30);
            auto command = [&]() {
                for (unsigned panel = 0; panel < 3; ++panel) {
                    clock += 3;
                    events.push_back({clock, panel, target, 0, 0, true});
                }
            };
            if (!slow) {
                command();
                clock += delay;
            }
            for (unsigned n = 0; n < target - previous; ++n) {
                const unsigned step = reverse_writes ? target - 1 - n : previous + n;
                for (unsigned panel = 0; panel < 3; ++panel) {
                    clock += 267;
                    events.push_back({clock, panel, step + 1, scroll_trace[step].column,
                        payload[step][panel], false});
                }
            }
            if (slow) {
                command();
            }
            previous = target;
        }
        return events;
    };
    auto replay = [&](const std::vector<Event> &events, Model model, unsigned phase, unsigned panel, bool poison) {
        unsigned memory[162] = {};
        for (unsigned x = 0; x < 160; ++x) {
            memory[(audit_origin + x) % 162] = expected(panel, 0, x);
        }
        if (poison) {
            memory[(audit_origin + 80) % 162] = 0;
        }
        unsigned requested = 0, visible = 0, frame = phase, line = 0;
        unsigned next_latch = phase, next_scan = phase + model.gap;
        unsigned final_samples = 0;
        const unsigned end = events.empty() ? 0 : events.back().time;
        Result result;
        auto advance = [&](unsigned deadline) {
            // Latch, scan, then bus completion on timestamp ties.
            while (std::min(next_latch, next_scan) <= deadline) {
                if (next_latch <= next_scan) {
                    visible = requested;
                    frame = next_latch;
                    next_latch += 17000;
                } else {
                    const unsigned x = model.direction > 0 ? line : 159 - line;
                    const unsigned offset = (audit_origin + (audit_right ? 324 - visible : visible)) % 162;
                    const bool wrong = memory[(offset + x) % 162] != expected(panel, visible, x);
                    if (wrong) {
                        if ((x < 80) != audit_right) {
                            ++result.outgoing;
                        } else {
                            ++result.incoming;
                        }
                    }
                    if (!events.empty() && frame > end && visible == 232) {
                        assert(!wrong);
                        ++final_samples;
                    }
                    if (++line == 160) {
                        line = 0;
                        next_scan = next_latch + model.gap;
                    } else {
                        next_scan = frame + model.gap + line * model.active / 160;
                    }
                }
            }
        };
        for (const auto &event : events) {
            if (event.panel != panel) {
                continue;
            }
            advance(event.time);
            if (event.command) {
                requested = event.step;
            } else {
                memory[event.column] = event.pixels;
            }
        }
        advance(end + 34000);
        if (!events.empty()) {
            assert(requested == 232 && visible == 232 && final_samples >= 160);
        }
        result.worst = result.outgoing + result.incoming;
        result.failing = result.worst != 0;
        return result;
    };
    const Model models[] = {
        {17000, 0, 1}, {17000, 0, -1}, {10000, 0, 1}, {10000, 0, -1},
        {10000, 1000, 1}, {10000, 1000, -1},
        {10000, 3500, 1}, {10000, 3500, -1},
        {10000, 7000, 1}, {10000, 7000, -1},
    };
    const auto slow = schedule(0, false, true);
    for (auto model : models) {
        assert(replay({}, model, 0, 0, false).worst == 0);
        assert(replay({}, model, 0, 0, true).worst > 0);
        for (unsigned phase = 0; phase < 17000; phase += 1000) {
            for (unsigned panel = 0; panel < 3; ++panel) {
                assert(replay(slow, model, phase, panel, false).worst == 0);
            }
        }
    }
    for (bool reverse_writes : {false, true}) {
        for (unsigned delay = 0; delay <= 17000; delay += 1000) {
            const auto events = schedule(delay, reverse_writes, false);
            const unsigned finish = events.back().time;
            for (auto model : models) {
                Result total;
                for (unsigned phase = 0; phase < 17000; phase += 1000) {
                    for (unsigned panel = 0; panel < 3; ++panel) {
                        const auto result = replay(events, model, phase, panel, false);
                        total.outgoing += result.outgoing;
                        total.incoming += result.incoming;
                        total.failing += result.failing;
                        total.worst = std::max(total.worst, result.worst);
                    }
                }
                std::printf("schedule-search motion=%s origin=%u scan=%d active=%u gap=%u delay=%u reverse=%u finish=%u within-budget=%u failing=%u/51 worst=%u outgoing=%u incoming=%u\n",
                    audit_right ? "right" : "left", audit_origin, model.direction, model.active, model.gap,
                    delay, unsigned(reverse_writes), finish, unsigned(finish <= 500000), total.failing,
                    total.worst, total.outgoing, total.incoming);
            }
        }
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
    constexpr unsigned expected_buttons[] = {2, 1, 0, 5, 4, 3};
    for (unsigned i = 0; i < 6; ++i) {
        assert(mirrored_button(i) == expected_buttons[i]);
    }
    DisplayRenderer renderer;
    drain(renderer);
    // Build independent snapshots, then literal old/gap/new strips per panel.
    for (unsigned identity = 0; identity < 4; ++identity) {
        renderer.show(0, audit_pane(identity)); drain(renderer);
        for (unsigned y = 0; y < 128; ++y)
            for (unsigned x = 0; x < 160; ++x)
                audit_images[identity][x][y] = screens[0][y][x];
    }
    for (bool undo : {false, false, true, true}) {
        for (unsigned panel = 0; panel < 3; ++panel)
            renderer.show(panel, audit_pane(panel + unsigned(!undo)));
        drain(renderer);
        // A newly exposed, unwritten column must fail the pixel comparison.
        for (unsigned panel = 0; panel < 3; ++panel)
            for (unsigned y = 0; y < 128; ++y)
                for (unsigned x = 160; x < 162; ++x) ram[panel][y][(ring + x) % 162] = 0xdead;
        audit_origin = ring;
        audit_right = !undo; // Physical rightward motion, independently of undo semantics.
        audit_step = audit_panel = 0;
        for (unsigned panel = 0; panel < 3; ++panel) {
            for (unsigned x = 0; x < 160; ++x) {
                for (unsigned y = 0; y < 128; ++y) {
                    audit_strip[panel * 464 + (audit_right ? 232 : 0) + x][y] = audit_images[panel + unsigned(!undo)][x][y];
                    audit_strip[panel * 464 + (audit_right ? 0 : 232) + x][y] = audit_images[panel + unsigned(undo)][x][y];
                }
            }
        }
        scroll_trace.clear();
        renderer.transition(undo, 500);
        for (unsigned panel = 0; panel < 3; ++panel)
            renderer.show(panel, audit_pane(panel + unsigned(undo)));
        const uint32_t start = UINT32_MAX - 200000;
        for (unsigned i = 0; i < 1600; ++i) renderer.draw(start);
        audit_motion = true;
        unsigned batches = 0;
        for (unsigned elapsed = 1; elapsed <= 2000; ++elapsed) {
            audit_now = start + elapsed * 1000;
            unsigned before = audit_step;
            for (unsigned work = 0; work < 8; ++work) {
                renderer.draw(audit_now);
            }
            unsigned steps = audit_step - before;
            assert(steps == 0 || steps == 2);
            batches += steps == 2;
            if (renderer.ready()) {
                break;
            }
        }
        assert(audit_step == 232 && audit_panel == 0 && batches == 116);
        audit_motion = false;
        replay_delayed_offsets();
        search_scroll_schedules();
        drain(renderer);
    }

#ifdef CNTRL_DIAGNOSTICS
    ScrollTimingProbe probe;
    uint32_t now = UINT32_MAX - 500000;
    auto load = [&]() {
        unsigned before = writes;
        for (unsigned i = 0; i < 486; ++i) {
            probe.poll(now);
            now += 400;
        }
        now -= 400;
        assert(writes == before + 486 && ring == 0);
        for (const auto &panel : ram) {
            for (unsigned y = 0; y < 128; ++y) {
                for (uint16_t pixel : panel[y]) {
                    assert(pixel == (y % 16 >= 8 ? 0x7bef : 0));
                }
            }
        }
    };
    auto no_work = [&](uint32_t when) {
        unsigned before_writes = writes, before_scrolls = scroll_updates;
        probe.poll(when);
        assert(writes == before_writes && scroll_updates == before_scrolls);
    };
    probe.arm();
    load();
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
        now += 5000000;
        no_work(now);
        probe.start();
        no_work(now);
        for (unsigned trial = 0; trial < 6; ++trial) {
            assert(refresh_settings.back() == (trial >= 2));
            no_work(now + 399999);
            now += 400000;
            for (unsigned batch = 0; batch < 116; ++batch) {
                const unsigned old_ring = ring;
                const unsigned before_scrolls = scroll_updates;
                for (unsigned step = 0; step < 2; ++step) {
                    const unsigned before_writes = writes;
                    probe.poll(now);
                    assert(writes == before_writes + 3);
                    const unsigned column = (old_ring + (trial % 2 ? 161 - step : 160 + step)) % 162;
                    for (unsigned panel = 0; panel < 3; ++panel) {
                        for (unsigned y = 0; y < 128; ++y) {
                            assert(ram[panel][y][column] == (y % 16 < 8 ? 0xffff : 0));
                        }
                    }
                    assert(scroll_updates == before_scrolls + step);
                    now += 1000; // More than the modeled wire time, including SSA.
                }
                assert(ring == (old_ring + (trial % 2 ? 160 : 2)) % 162);
                if (batch == 20) {
                    now += 50000; // Delayed post-command poll must still start a full wait.
                }
                no_work(now); // Anchor after the completed transfers.
                if (batch < 115) {
                    const unsigned wait = trial < 2 ? 20000 : 10000;
                    no_work(now + wait - 1);
                    now += wait;
                }
            }
            for (const auto &panel : screens) {
                for (unsigned y = 0; y < 128; ++y) {
                    for (uint16_t pixel : panel[y]) {
                        assert(pixel == (y % 16 < 8 ? 0xffff : 0));
                    }
                }
            }
            no_work(now + 499999);
            now += 500000;
            no_work(now);
            load();
        }
        assert(!refresh_settings.back()); // Completed sequences restore stock timing.
    }
    assert(refresh_settings.size() == 13);
    for (unsigned i = 0; i < 12; ++i) {
        assert(refresh_settings[i] == (i % 6 >= 2));
    }
    set_display_fast_refresh(true);
    probe.stop();
    assert(!refresh_settings.back());
    no_work(now + 10000000);
    assert(!probe.active());
    probe.arm();
    load();
    assert(!refresh_settings.back());
    probe.stop();
#endif

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
    cntrl::MemoryBuffer data;
    assert(data.resize(48)); std::memset(data.data, 0, data.size);
    data.data[0] = 9; data.data[4] = 2; data.data[6] = 2; data.data[10] = 2;
    data.data[16] = '1'; data.data[22] = 3;
    data.data[24] = 2; data.data[28] = 5; data.data[30] = 2;
    data.data[32] = 0xc3; data.data[33] = 0xa9; data.data[42] = 3;
    data.data[46] = 128; data.data[47] = 64;
    assert(font_assets::valid_pixels(entry, data.data));
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

static void test_page_settings() {
    cntrl::PageSettings settings{{1000, 1000, 24, 1, 4, 2, 100, 3000, 400,
        40, 5, 12, 40, 2, 8, 0, 0x4000, 0xf800, 0, 3, 1, 0xff90}};
    Pages custom;
    settings.words[cntrl::PageSettings::title_font] = 1;
    custom.configure(settings);
    assert(custom.view(0, 0, {}).lines[0].graphics.font == 1);
    settings.words[cntrl::PageSettings::title_font] = 0;
    settings.words[cntrl::PageSettings::clock_outputs] = 1;
    settings.words[cntrl::PageSettings::slot1] = 90;
    settings.words[cntrl::PageSettings::slot2] = 144;
    Pages pages; pages.configure(settings);
    constexpr uint8_t cycle[] = {'C', 'L', 'A', 'Y', 0, 10,
        1, 1, 2, 1, 3, 1, 4, 1, 5, 1, 6, 1, 7, 1, 8, 1, 9, 1, 0, 1};
    pages.layout(cntrl::AppLayout{cycle});
    PerformanceStatus status; status.cycles = 10; status.patches = 59;
    status.ram_used[0] = 49 * 1024; status.ram_free[0] = 463 * 1024;
    status.ram_used[1] = 25 * 1024; status.ram_free[1] = 487 * 1024;
    status.disk_valid = true; status.disk_used = 1181116006ull;
    status.disk_free = 29ull * 1024 * 1024 * 1024 - status.disk_used;
    assert(pages.page() == Pages::Page::status);
    pages.browse(true);
    auto view = pages.view(0, 0, status);
    assert(!std::strcmp(view.lines[0].text, "Tap Tempo"));
    const auto heading = view.lines[0].graphics;
    assert(view.heading_band && heading.foreground == 0xffff && heading.background == 0);
    assert(view.graphics.background == 0xff90 && view.circles[0].annulus == 5);
    DisplayRenderer renderer;
    renderer.show(2, view); drain(renderer);
    assert(screens[2][10][0] == 0 && screens[2][23][0] == 0 && screens[2][24][0] == 0xff90);
    cntrl::Pane weight;
    weight.graphics.pad = 2;
    weight.lines[0].graphics = weight.graphics;
    weight.lines[0].x = 10; weight.lines[0].y = 30;
    std::strcpy(weight.lines[0].text, "H");
    renderer.show(2, weight); drain(renderer);
    assert(screens[2][37][10] == 0xffff);
    weight.lines[0].heavier = true;
    renderer.show(2, weight); drain(renderer);
    assert(screens[2][37][10] == 0 && screens[2][38][10] == 0xffff);
    assert(screens[2][30][9] == 0xffff && screens[2][30][15] == 0xffff);
    weight.lines[0].taller = true;
    renderer.show(2, weight); drain(renderer);
    assert(screens[2][38][10] == 0 && screens[2][39][10] == 0xffff);
    assert(screens[2][30][9] == 0xffff && screens[2][30][15] == 0xffff);
    assert(!std::strcmp(view.lines[1].text, "---.-") && view.circles[0].color == 0x4000);
    pages.time(50, 50000, true); assert(pages.due(50));
    view = pages.view(50, 50000, status);
    assert(view.circles[0].color == 0xf800 && !std::strcmp(view.lines[1].text, "---.-"));
    pages.time(60, 60000, false); assert(pages.due(60));
    view = pages.view(60, 60000, status); assert(view.circles[0].color == 0x4000);
    pages.tap(100); view = pages.view(100, 100000, status);
    assert(view.circles[0].color == 0x4000 && !std::strcmp(view.lines[1].text, "---.-"));
    pages.tap(600); view = pages.view(600, 600000, status);
    assert(!std::strcmp(view.lines[1].text, "120.0") && view.circles[0].color == 0xf800);
    assert(!pages.due(799) && pages.due(800));
    view = pages.view(900, 900000, status); assert(view.circles[0].color == 0x4000);
    pages.time(950, 950000, true); assert(pages.due(950));
    view = pages.view(950, 950000, status); assert(view.circles[0].color == 0xf800);
    pages.time(1000, 1000000, true); assert(!pages.due(1000));
    pages.time(1050, 1050000, false); assert(pages.due(1050));
    view = pages.view(1050, 1050000, status); assert(view.circles[0].color == 0x4000);
    assert(pages.due(1100));
    pages.browse(false); pages.time(1200, 1200000, false);
    view = pages.view(1200, 1200000, status);
    assert(!std::strcmp(view.lines[3].text, "Tap: 120.0"));
    assert(!std::strcmp(view.lines[2].text, "Clock: ---.-"));
    assert(!std::strcmp(view.lines[1].text, "Cycles: 10  Patches: 59"));
    assert(!std::strcmp(view.lines[4].text, "RAM: 49/512k 25/512k"));
    assert(!std::strcmp(view.lines[5].text, "Disk:1.1/29G Up:0:00:01"));
    assert(!std::strcmp(view.lines[6].text, "Starts: --") && !view.lines[7].text[0]);
    assert(view.lines[0].graphics.line_height == settings[cntrl::PageSettings::title_height] * 3 / 2 + 4);
    assert(view.lines[0].graphics.font == heading.font && view.lines[0].graphics.size == heading.size);
    for (unsigned i = 1; i <= 6; ++i) {
        assert(view.lines[i].graphics.font == view.lines[1].graphics.font);
        assert(view.lines[i].graphics.size == view.lines[1].graphics.size);
        assert(view.lines[i].taller && !std::strstr(view.lines[i].text, "Setup:"));
        assert(view.lines[i].graphics.line_height >= 20 && view.lines[i].graphics.line_height <= 21);
    }
    assert(view.lines[1].y < 40);
    assert(view.lines[2].y > view.lines[1].y && view.lines[4].y > view.lines[2].y);
    assert(view.lines[3].x >= 80 && view.lines[2].y == view.lines[3].y);
    renderer.show(2, view); drain(renderer);
    const auto &counts = view.lines[1];
    const unsigned advance = 6 * std::max(1u, unsigned(ShowGraphics::native_font(counts.graphics.font, counts.graphics.size)->cap_height) / 8);
    assert(screens[2][counts.y][counts.x] == 0x0011);
    assert(screens[2][counts.y][counts.x + 8 * advance] == 0x0400);
    assert(screens[2][counts.y][counts.x + 12 * advance] == 0x0011);
    assert(screens[2][counts.y][counts.x + 21 * advance] == 0x0400);
    const auto &uptime = view.lines[5];
    assert(screens[2][uptime.y][uptime.x + 16 * advance] == 0x0400);
    assert(!pages.due(2199) && pages.due(2200));
    for (unsigned i = 0; i <= 24; ++i) pages.clock(16, 2000000 + i * 500000 / 24);
    view = pages.view(2500, 2500000, status);
    assert(!std::strcmp(view.lines[2].text, "Clock: 120.0") && !view.lines[2].stippled);
    pages.time(3501, 3501000, false); view = pages.view(3501, 3501000, status);
    assert(!std::strcmp(view.lines[2].text, "Clock: 120.0") && view.lines[2].stippled);
    pages.clock(17, 3600000); view = pages.view(3600, 3600000, status);
    assert(!view.lines[2].stippled && !std::strcmp(view.lines[1].text, "Cycles: 10  Patches: 59"));
    pages.time(1205000, 1205000000, false);
    view = pages.view(1205000, 1205000000, status);
    assert(!std::strcmp(view.lines[5].text, "Disk:1.1/29G Up:0:20:05"));
    status.disk_valid = false;
    view = pages.view(1205000, 1205000000, status);
    assert(!std::strcmp(view.lines[5].text, "Disk:--/--G Up:0:20:05"));
    assert(!std::strcmp(view.lines[6].text, "Starts: --"));
    status.starts = 42; status.starts_status = StartupCounter::Status::ready;
    view = pages.view(1205000, 1205000000, status);
    assert(!std::strcmp(view.lines[6].text, "Starts: 42"));
    assert(view.lines[6].graphics.size == view.lines[1].graphics.size);
    assert(view.lines[6].graphics.foreground == 0x0011 && view.lines[6].alternate_color == 0x0400);
    status.starts_status = StartupCounter::Status::error;
    view = pages.view(1205000, 1205000000, status);
    assert(!std::strcmp(view.lines[6].text, "Starts: error"));
    pages.time(0xfffffff0u, 0, false);
    pages.time(0x000003e8u, 1000000, false);
    view = pages.view(1000, 1000000, status);
    assert(!std::strcmp(view.lines[5].text, "Disk:--/--G Up:1193:02:48"));
    pages.browse(false); assert(pages.page() == Pages::Page::firmware);
    view = pages.view(3600, 3600000, status);
    assert(!std::strcmp(view.lines[0].text, "Firmware"));
    assert(!std::strcmp(view.lines[1].text, "Use stock"));
    assert(!std::strcmp(view.lines[2].text, "firmware"));
    assert(view.lines[0].graphics.line_height == heading.line_height);
    assert(view.lines[0].graphics.font == heading.font && view.lines[0].graphics.size == heading.size);
    assert(view.lines[1].y > 64 && view.lines[2].y > view.lines[1].y);
    assert(!pages.due(5000));
    pages.tap(3900);
    assert(pages.page() == Pages::Page::confirm_firmware);
    view = pages.view(3900, 3900000, status);
    assert(!std::strcmp(view.lines[0].text, "F for stock firmware."));
    assert(!std::strcmp(view.lines[1].text, "C to cancel"));
    assert(view.lines[0].y < 64 && view.lines[1].y >= 64);
    assert(!pages.browse(true));
    assert(pages.browse(false)); // Only F confirms.
    pages.tap(4000);
    assert(pages.page() == Pages::Page::firmware);
    view = pages.view(4000, 4000000, status);
    assert(!std::strcmp(view.lines[0].text, "Firmware"));
    pages.browse(false); assert(pages.page() == Pages::Page::cable);
    cntrl::CableTest::Status cables;
    view = pages.view(4000, 4000000, status, cables);
    assert(!std::strcmp(view.lines[0].text, "MIDI Cable Test"));
    assert(view.heading_band && view.graphics.background == 0xff90);
    assert(view.lines[0].graphics.font == heading.font && view.lines[0].graphics.size == heading.size);
    assert(view.toggle.visible && !view.toggle.enabled && !view.toggle.disabled);
    assert(!std::strcmp(view.lines[1].text, "Out") && !std::strcmp(view.lines[2].text, "In"));
    assert(!std::strcmp(view.lines[3].text, "DIN") && !std::strcmp(view.lines[4].text, "TRS"));
    DisplayRenderer cable_renderer;
    cable_renderer.show(2, view); drain(cable_renderer);
    for (unsigned y : {68u, 94u}) for (unsigned x : {78u, 130u}) assert(screens[2][y][x] == 0x8410);
    using Source = cntrl::CableTest::Source;
    cables = {true, {true, true}, {Source::trs, Source::din}};
    view = pages.view(4001, 4001000, status, cables);
    assert(view.toggle.visible && view.toggle.enabled && !view.toggle.disabled);
    cable_renderer.show(2, view); drain(cable_renderer);
    assert(screens[2][68][78] == 0x001f && screens[2][94][78] == 0x07e0);
    assert(screens[2][68][130] == 0x07e0 && screens[2][94][130] == 0x001f);
    assert(screens[2][62][72] == 0xff90); // Circular, not square.
    assert(screens[2][117][147] == 0xffff && screens[2][117][135] == 0x07e0);
    cables.enabled = false;
    view = pages.view(4002, 4002000, status, cables);
    for (unsigned i = 1; i <= 4; ++i) assert(view.lines[i].graphics.foreground == 0x8410);
    cable_renderer.show(2, view); drain(cable_renderer);
    for (unsigned y : {68u, 94u}) for (unsigned x : {78u, 130u}) assert(screens[2][y][x] == 0x8410);
    assert(screens[2][117][135] == 0xffff && screens[2][117][147] == 0);
    pages.browse(false); assert(pages.page() == Pages::Page::panic);
    view = pages.view(4002, 4002000, status);
    assert(!std::strcmp(view.lines[0].text, "Panic") && !std::strcmp(view.lines[3].text, "Ready"));
    assert(view.heading_band && view.graphics.background == 0xff90 && view.lines[0].graphics.size == heading.size);
    view = pages.view(4003, 4003000, status, {}, cntrl::Panic::Status::incomplete);
    assert(!std::strcmp(view.lines[3].text, "Incomplete"));
    view = pages.view(4003, 4003000, status, {}, cntrl::Panic::Status::sending);
    assert(!std::strcmp(view.lines[3].text, "Sending"));
    pages.browse(false); assert(pages.page() == Pages::Page::ports);
    pages.next_port_mode();
    view = pages.view(4004, 4004000, status);
    assert(!std::strcmp(view.lines[0].text, "TRS Ports"));
    assert(!std::strcmp(view.lines[1].text, "Jack 1: Off"));
    assert(!std::strcmp(view.lines[2].text, "MIDI Type A"));
    pages.browse(false); assert(pages.page() == Pages::Page::expression);
    pages.expression_status("Jack 2  CC 11 Ch 1", "50%  Calibrated", "C: Calibrate");
    view = pages.view(4010, 4010000, status);
    assert(!std::strcmp(view.lines[0].text, "Expression") && !std::strcmp(view.lines[2].text, "50%  Calibrated"));
    pages.browse(false); assert(pages.page() == Pages::Page::clock_transport);
    view = pages.view(4005, 4005000, status);
    assert(!std::strcmp(view.lines[0].text, "MIDI Clock"));
    assert(pages.generator.bpm() == 120); // Two taps changed the initial 90 BPM.
    pages.clock_button(0, 4005000, 1);
    assert(pages.generator.running() && pages.clock_owner() == 1);
    pages.clock_button(4, 4006000, 1); // External mode stops the internal stream.
    assert(!pages.generator.running());
    pages.clock_button(0, 4007000, 1);
    assert(!pages.generator.running());
    pages.browse(false); assert(pages.page() == Pages::Page::clock_tempo);
    pages.clock_button(2, 4008000, 1);
    assert(pages.generator.bpm() == 144);
    pages.clock_button(0, 4009000, 1);
    assert(pages.generator.bpm() == 143);
    pages.browse(false); assert(pages.page() == Pages::Page::keyboard);
    view = pages.view(4005, 4005000, status);
    assert(!std::strcmp(view.lines[0].text, "Keyboard"));
    assert(!std::strcmp(view.lines[1].text, "No shortcuts"));
    pages.browse(false); assert(pages.page() == Pages::Page::tap);
    pages.browse(true); assert(pages.page() == Pages::Page::keyboard);
    pages.browse(true); assert(pages.page() == Pages::Page::clock_tempo);
    pages.browse(true); assert(pages.page() == Pages::Page::clock_transport);
    pages.browse(true); assert(pages.page() == Pages::Page::expression);
    pages.browse(true); assert(pages.page() == Pages::Page::ports);
    pages.browse(true); assert(pages.page() == Pages::Page::panic);
    pages.browse(true); assert(pages.page() == Pages::Page::cable);
    pages.browse(true); assert(pages.page() == Pages::Page::firmware);
    pages.browse(true);
    assert(pages.page() == Pages::Page::status);
    view = pages.view(3601, 3601000, status);
    assert(view.lines[1].x >= 2 && view.lines[1].x < 80);
    assert(!std::strcmp(view.lines[1].text, "Cycles: 10  Patches: 59"));
    pages.browse(true); assert(pages.page() == Pages::Page::tap);
    view = pages.view(3601, 3601000, status);
    assert(!std::strcmp(view.lines[1].text, "120.0"));
}

static void test_page_window_allocation() {
    constexpr uint8_t cycle[] = {'C', 'L', 'A', 'Y', 0, 5,
        10, 0, 1, 1, 1, 2, 1, 3, 0, 0};
    Pages pages;
    pages.layout(cntrl::AppLayout{cycle});
    assert(pages.page() == Pages::Page::none && pages.windows() == 0);
    pages.tap(100);
    assert(pages.page() == Pages::Page::none);
    for (unsigned count = 1; count <= 3; ++count) {
        pages.browse(false);
        assert(pages.page() == Pages::Page::status && pages.windows() == count);
    }
    pages.browse(false);
    assert(pages.page() == Pages::Page::tap && pages.windows() == 0);
    pages.browse(false);
    assert(pages.page() == Pages::Page::none && pages.windows() == 0);
    pages.browse(true);
    assert(pages.page() == Pages::Page::tap);
    pages.layout(cntrl::AppLayout{});
    pages.browse(false);
    assert(pages.page() == Pages::Page::none && pages.windows() == 0);

    cntrl::Pane content;
    std::snprintf(content.lines[0].text, 64, "Status");
    for (unsigned i = 1; i < 8; ++i) std::snprintf(content.lines[i].text, 64, "row %u", i);
    content.circles[0].diameter = 40;
    for (unsigned count = 2; count <= 3; ++count) {
        unsigned row = 1;
        for (unsigned window = 0; window < count; ++window) {
            const auto view = Pages::window_view(content, window, count);
            assert(!std::strcmp(view.lines[0].text, "Status"));
            assert(view.circles[0].diameter == (window == 0 ? 40 : 0));
            for (unsigned i = 1; i < cntrl::max_lines && view.lines[i].text[0]; ++i)
                assert(!std::strcmp(view.lines[i].text, content.lines[row++].text));
        }
        assert(row == 8);
    }
}

void test_due_scan_gets_display_handoff() {
    DisplayRenderer renderer;
    auto *first = acquire_display_band(); assert(first);
    *first = {}; first->columns = 1; first->count = 128;
    queue_display_band(*first); start_display_dma();
    auto *second = acquire_display_band(); assert(second && second != first);
    *second = *first; second->column = 1;
    queue_display_band(*second);
    scan_due = true;
    renderer.draw(); assert(display_spi_busy());
    renderer.draw(); // DMA completes inside the renderer, after the loop's scan attempt.
    assert(!display_spi_busy() && display_dma_pending());
    assert(!acquire_display_band()); // The queued band still owns its buffer.
    scan_due = false;
    start_display_dma(); assert(display_spi_busy());
    poll_display_dma(); poll_display_dma();
    assert(!display_dma_pending());
}

static void shared_font_memory() {
    font_assets::Entry entry;
    entry.bytes[0] = 24; entry.bytes[1] = 2; entry.bytes[13] = 128; entry.bytes[14] = 128;
    constexpr unsigned glyphs = 41, pixels = 20 * 128, index = glyphs * 16;
    entry.bytes[7] = glyphs;
    cntrl::put32(entry.bytes + 15, index);
    cntrl::put32(entry.bytes + 19, glyphs * pixels);
    cntrl::MemoryBuffer data;
    assert(data.resize(entry.length()) && data.size > 96 * 1024);
    std::memset(data.data, 0, data.size);
    for (unsigned i = 0; i < glyphs; ++i) {
        auto *g = data.data + i * 16;
        cntrl::put32(g, i); g[4] = 160; g[6] = 128;
        cntrl::put32(g + 12, i * pixels);
    }
    font_assets::cache.begin();
    assert(font_assets::cache.add(456, entry, std::move(data)));
    font_assets::cache.finish(true);
    assert(cntrl::memory_used == entry.length());
    const auto *original = font_assets::cache.get(456, 24)->data.data;
    font_assets::Entry small;
    small.bytes[0] = 13; small.bytes[1] = 2; small.bytes[7] = 1;
    small.bytes[13] = 1; small.bytes[15] = 16;
    cntrl::MemoryBuffer setup, pending, candidate;
    assert(pending.resize(16)); std::memset(pending.data, 0, pending.size);
    font_assets::cache.begin();
    assert(font_assets::cache.add(789, small, std::move(pending)));
    assert(setup.resize(cntrl::memory_budget - cntrl::memory_used));
    assert(!candidate.resize(1));
    font_assets::cache.finish(false);
    assert(cntrl::memory_used == cntrl::memory_budget - 16);
    assert(font_assets::cache.get(456, 24)->data.data == original);
    setup.clear();
    font_assets::cache.begin(); assert(font_assets::cache.reuse(456, 24));
    font_assets::cache.finish(true);
    assert(font_assets::cache.get(456, 24)->data.data == original);
    assert(cntrl::memory_used == entry.length());
    font_assets::cache.begin(); font_assets::cache.finish(true);
    assert(cntrl::memory_used == 0);
}

int main() {
    cntrl::memory_budget = 192 * 1024;
    shared_font_memory();
    test_due_scan_gets_display_handoff();
    test_page_settings();
    test_page_window_allocation();
    writes = pixels_sent = 0;
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
    bands.heading_band = true;
    bands.three_bands = true;
    renderer.show(0, bands); drain(renderer);
    // Fixed thirds cover rows 0..41, 42..84 and 85..127.
    assert(screens[0][0][80] == 1);
    assert(screens[0][41][0] == 1 && screens[0][41][159] == 1);
    assert(screens[0][42][150] == 2 && screens[0][42][0] == 0xffff);
    assert(screens[0][84][150] == 2 && screens[0][85][150] == 4);
    assert(screens[0][17][4] == 0 && screens[0][59][4] == 0 && screens[0][102][4] == 0);
    bands.lines[0].graphics.line_height = 24;
    bands.lines[1].text[0] = 0;
    renderer.show(0, bands); drain(renderer);
    assert(screens[0][9][4] == 0 && screens[0][102][4] == 0);
    bands.lines[0].graphics.line_height = 8;
    std::strcpy(bands.lines[1].text, "A");
    bands.lines[0].stippled = true;
    renderer.show(0, bands); drain(renderer);
    assert(screens[0][41][0] == 1 && screens[0][41][159] == 1);
    bands.lines[0].stippled = false;
    bands.heading_band = false;
    bands.three_bands = false;
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
    view.graphics.background = 0xffff; view.graphics.pad = 2;
    for (unsigned i = 0; i < 2; ++i) {
        auto &field = view.lines[i];
        std::strcpy(field.text, i ? "Tap: 120.0" : "Clock: 120.0");
        field.graphics = view.graphics;
        field.graphics.foreground = 0;
        field.graphics.size = field.graphics.line_height = 8;
        field.x = i ? 82 : 2; field.y = 60;
        field.stippled = !i;
    }
    renderer.show(2, view); drain(renderer);
    assert(screens[2][60][2] == 0 && screens[2][60][3] == 0xffff);
    assert(screens[2][60][82] == 0 && screens[2][60][83] == 0);
    renderer.show(2, empty); drain(renderer);

    view = empty;
    view.graphics.background = 0x07e0;
    renderer.show(1, view); drain(renderer);
    blank(1, 0x07e0);
    assert(ShowGraphics::supports_font(0, 8) && ShowGraphics::supports_font(1, 13));
    assert(ShowGraphics::supports_font(2, 96) && !ShowGraphics::supports_font(3, 8));
    assert(ShowGraphics::supports_font(5, 28)); // Fixed cable-test UI title.
    assert(!ShowGraphics::supports_font(5, 32));
    assert(ShowGraphics::supports_font(8, 24)); // Built-in monospace UI; show text still uses SD subsets.
    assert(!ShowGraphics::supports_font(10, 24));
    assert(ShowGraphics::supports_font(0, 13) && !ShowGraphics::supports_font(1, 15));

    // Send moves the left card into the centre and the centre card right.
    for (unsigned i = 0; i < 3; ++i) {
        cntrl::Pane pane; pane.graphics.background = uint16_t(0x1111 * (i + 2));
        renderer.show(i, pane);
    }
    drain(renderer);
    renderer.transition(false, 2000);
    check_motion = true; checked_progress = 0;
    for (unsigned i = 0; i < 3; ++i) {
        // Destination styling differs from the travelling card on inner panes.
        cntrl::Pane pane; pane.graphics.background = uint16_t(0x1111 * (i ? i + 4 : 1));
        renderer.show(i, pane);
    }
    for (unsigned now = 0; now < 1000; ++now)
        for (unsigned work = 0; work < 8; ++work) renderer.draw(now * 1000);
    assert(!renderer.ready() && checked_progress < display_pitch);
    for (unsigned now = 1000; now < 3000; ++now)
        for (unsigned work = 0; work < 8; ++work) renderer.draw(now * 1000);
    assert(renderer.ready());
    check_motion = false; assert(checked_progress == display_pitch);
    for (unsigned i = 0; i < 3; ++i) {
        blank(i, uint16_t(0x1111 * (i ? i + 4 : 1)));
        cntrl::Pane pane; pane.graphics.background = uint16_t(0x1111 * (i + 1));
        renderer.show(i, pane);
    }
    drain(renderer);
    assert(ring == (display_ram_columns - display_pitch % display_ram_columns) % display_ram_columns);
    renderer.transition(true, 500);
    check_motion = check_undo = true; checked_progress = 0;
    for (unsigned i = 0; i < 3; ++i) {
        cntrl::Pane pane; pane.graphics.background = uint16_t(0x1111 * (i + 2));
        renderer.show(i, pane);
    }
    for (unsigned now = 3000; now < 5000; ++now)
        for (unsigned work = 0; work < 8; ++work) renderer.draw(now * 1000);
    assert(renderer.ready() && ring == 0);
    check_motion = false; assert(checked_progress == display_pitch);
    for (unsigned i = 0; i < 3; ++i) blank(i, uint16_t(0x1111 * (i + 2)));
    // An unspecified duration uses the renderer default, even after an explicit one.
    renderer.transition(false, std::nullopt);
    for (unsigned now = 5000; now < 5100; ++now)
        for (unsigned work = 0; work < 8; ++work) renderer.draw(now * 1000);
    assert(!renderer.ready());
    for (unsigned now = 5100; now < 5300; ++now)
        for (unsigned work = 0; work < 8; ++work) renderer.draw(now * 1000);
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
    for (unsigned now = 7000; now < 9000; ++now)
        for (unsigned work = 0; work < 8; ++work) renderer.draw(now * 1000);
    assert(renderer.ready() && scroll_updates > before_scroll);
    blank(1, 0x001f); blank(2, 0xf800);
    assert(transition_progress(1999, 2000) < display_pitch);
    assert(transition_progress(2000, 2000) == display_pitch);
    assert(transition_progress(500, 500) == display_pitch);
    for (unsigned backward = 0; backward < 2; ++backward) {
        for (unsigned i = 0; i < 3; ++i) {
            cntrl::Pane pane; pane.graphics.background = uint16_t(0x1111 * (i + 1));
            renderer.show(i, pane);
        }
        drain(renderer);
        const unsigned origin = panel_rings[0];
        renderer.transition(backward, 500, 0);
        cntrl::Pane next; next.graphics.background = 0x4444;
        renderer.show(0, next);
        for (unsigned now = 10000; now < 11000; ++now) {
            for (unsigned work = 0; work < 8; ++work) renderer.draw(now * 1000);
            blank(1, 0x2222); blank(2, 0x3333);
            unsigned progress = backward ? (panel_rings[0] + 162 - origin) % 162
                : (origin + 162 - panel_rings[0]) % 162;
            for (unsigned y = 0; y < 128; ++y) for (unsigned x = 0; x < 160; ++x) {
                int source = int(x) + (backward ? int(progress) : -int(progress));
                bool incoming = source < 0 || source >= 160;
                if (incoming) source += backward ? -160 : 160;
                assert(screens[0][y][x] == rounded_background(unsigned(source), y, incoming ? 0x4444 : 0x1111));
            }
        }
        assert(renderer.ready()); blank(0, 0x4444);
    }
    for (bool backward : {false, true}) {
        for (unsigned i = 0; i < 3; ++i) {
            cntrl::Pane pane; pane.graphics.background = uint16_t(0x1111 * (i + 1));
            renderer.show(i, pane);
        }
        drain(renderer);
        const unsigned origin = panel_rings[0], before = writes, before_pixels = pixels_sent;
        renderer.transition(backward, 500, 0, true, true);
        cntrl::Pane next; next.graphics.background = 0x4444;
        renderer.show(0, next);
        bool intermediate = false;
        unsigned previous_progress = 0;
        for (unsigned poll = 0; poll < 12000 && !renderer.ready(); ++poll) {
            const unsigned old_writes = writes;
            renderer.draw(20000000 + poll * (backward ? 8000 : 100));
            assert(writes - old_writes <= 8);
            blank(1, 0x2222); blank(2, 0x3333);
            assert(panel_rings[0] == origin);
            if (writes != old_writes && (writes - before) % 160 == 0) {
                unsigned progress = 0;
                for (unsigned y = 0; y < 128; ++y) progress += screens[0][y][80] == 0x4444;
                assert(progress == previous_progress + 32);
                previous_progress = progress;
                intermediate |= progress > 0 && progress < 128;
                for (unsigned y = 0; y < 128; ++y) for (unsigned x = 0; x < 160; ++x) {
                    int source = int(y) + (backward ? -int(progress) : int(progress));
                    bool incoming = source < 0 || source >= 128;
                    source = (source + 128) % 128;
                    assert(screens[0][y][x] == rounded_background(x, unsigned(source), incoming ? 0x4444 : 0x1111));
                }
            }
        }
        assert(intermediate && previous_progress == 128 && renderer.ready());
        assert(pixels_sent - before_pixels < 4 * 160 * 128 / 2);
        blank(0, 0x4444);
    }
    // Configured sends can move both performance panes vertically or leftward.
    for (bool vertical : {false, true}) for (bool backward : {false, true}) {
        for (unsigned i = 0; i < 3; ++i) {
            cntrl::Pane pane; pane.graphics.background = uint16_t(0x1111 * (i + 1));
            renderer.show(i, pane);
        }
        drain(renderer);
        renderer.transition(backward, 500, -1, true, vertical, false);
        for (unsigned i = 0; i < 2; ++i) {
            cntrl::Pane pane; pane.graphics.background = uint16_t(0x4444 * (i + 1));
            renderer.show(i, pane);
        }
        for (unsigned poll = 0; poll < 20000 && !renderer.ready(); ++poll) {
            renderer.draw(40000000 + poll * 1000);
            blank(2, 0x3333);
        }
        assert(renderer.ready()); blank(0, 0x4444); blank(1, 0x8888); blank(2, 0x3333);
    }
    for (unsigned i = 0; i < 3; ++i) {
        cntrl::Pane pane; pane.graphics.background = 0xffff;
        renderer.show(i, pane);
    }
    drain(renderer);
    renderer.target_brightness(64); drain(renderer);
    blank(0, 0x8410); blank(1); blank(2);
    renderer.target_brightness(16); drain(renderer);
    blank(0, 0x2104); blank(1); blank(2);
    renderer.target_brightness(128); drain(renderer);
    blank(0); blank(1); blank(2); // Original pixels survive repeated dimming.
    for (unsigned backward = 0; backward < 2; ++backward) {
        for (unsigned i = 0; i < 3; ++i) {
            cntrl::Pane p; p.graphics.background = uint16_t(0x1111 * (i + 1));
            renderer.show(i, p);
        }
        drain(renderer);
        unsigned origin = panel_rings[2];
        renderer.transition(backward, 500, 2);
        cntrl::Pane incoming; incoming.graphics.background = 0x4444;
        renderer.show(2, incoming);
        for (unsigned now = 12000; now < 13000; ++now) {
            for (unsigned work = 0; work < 8; ++work) renderer.draw(now * 1000);
            blank(0, 0x1111); blank(1, 0x2222);
            unsigned progress = backward ? (panel_rings[2] + 162 - origin) % 162 : (origin + 162 - panel_rings[2]) % 162;
            for (unsigned x = 0; x < 160; ++x) {
                int source = int(x) + (backward ? int(progress) : -int(progress));
                bool entering = source < 0 || source >= 160;
                if (entering) source += backward ? -160 : 160;
                for (unsigned y = 0; y < 128; ++y)
                    assert(screens[2][y][x] == rounded_background(unsigned(source), y, entering ? 0x4444 : 0x3333));
            }
        }
        assert(renderer.ready()); blank(2, 0x4444);
    }
    renderer.transition(true, 500, -1, true);
    cntrl::Pane restored; restored.graphics.background = 0x7777; renderer.show(0, restored);
    restored.graphics.background = 0x8888; renderer.show(1, restored);
    for (unsigned now = 13000; now < 14000; ++now) {
        for (unsigned work = 0; work < 8; ++work) renderer.draw(now * 1000);
        blank(2, 0x4444);
        for (unsigned x = 0; x < 160; ++x)
            assert(screens[1][64][x] == 0 || screens[1][64][x] == 0x2222 || screens[1][64][x] == 0x8888);
    }
    assert(renderer.ready()); blank(0, 0x7777); blank(1, 0x8888);
    cntrl::Pane flasher; flasher.graphics.background = 0xffff;
    flasher.circles[0] = {80, 90, 40, 15, 0xf800, 0};
    renderer.show(2, flasher); drain(renderer);
    assert(screens[2][90][80] == 0xf800);
    assert(screens[2][90][105] == 0 && screens[2][90][120] == 0xffff);
    flasher.circles[0].color = 0x4000; renderer.show(2, flasher); drain(renderer);
    assert(screens[2][90][80] == 0x4000 && screens[2][90][105] == 0);
    audit_animation_batches();
}
