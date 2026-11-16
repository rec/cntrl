#pragma once
#include "raster.h"
#include "messages.h"
#include <cntrl/text_messages.h>
#include <cntrl/checked_array.h>
#include <optional>
#include <vector>
#include <cstdio>

namespace cntrl::ui {
// One glyph or a bounded region per poll. The target's checked allocator decides
// where frame storage lives; DMA transfer storage remains separate.
class Renderer {
public:
#if UINTPTR_MAX == UINT32_MAX
    static_assert(sizeof(Pane) == 1800, "Update display.estimate_frame_ram for changed transition snapshots");
#endif
    Renderer(Display &display, const Fonts &fonts, uint16_t *work, unsigned work_capacity)
        : display_(display), fonts_(fonts), raster_(fonts, work), screens_(display.count()) {
        unsigned offset = 0, maximum = 0, transfer = 0;
        for (unsigned i = 0; i < display.count(); ++i) {
            const auto v = display.viewport(i); const unsigned area = v.width * v.height;
            screens_[i] = {v, offset, Pane{}, true, false, 0, {}}; offset += area; maximum = std::max(maximum, area);
            transfer = std::max(transfer, unsigned(v.height * 8 * 2));
        }
        transfer_.resize(transfer);
        area_ = offset;
        usable_ = maximum <= work_capacity && !screens_.empty();
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(renderer_prepare)
#endif
    bool prepare(unsigned count, bool animations) {
        finish(false);
        if (count > 2 || !usable_) return false;
        for (unsigned i = 0; i < count; ++i) if (!frames_[i].size) {
            ++memory_frame_operations;
            const bool ready = pending_frames_[i].resize(area_ * sizeof(uint16_t));
            --memory_frame_operations;
            if (!ready) { finish(false); return false; }
        }
        if (count == 1 && animations && incoming_.empty() && !pending_incoming_.resize(unsigned(screens_.size()))) {
            finish(false); return false;
        }
        pending_count_ = count; pending_animations_ = animations; prepared_ = true;
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(renderer_finish)
#endif
    void finish(bool commit) {
        if (commit && prepared_) {
            invalidate();
            for (unsigned i = 0; i < 2; ++i) {
                if (pending_frames_[i].size) frames_[i].swap(pending_frames_[i]);
                if (i >= pending_count_) frames_[i].clear();
            }
            if (!pending_incoming_.empty()) incoming_.swap(pending_incoming_);
            if (pending_count_ != 1 || !pending_animations_) incoming_.release();
            frame_count_ = pending_count_; animations_ = pending_animations_;
            cache_ = reinterpret_cast<uint16_t *>(frames_[0].data);
            previous_ = frame_count_ == 2 ? reinterpret_cast<uint16_t *>(frames_[1].data) : cache_;
        }
        for (auto &frame : pending_frames_) frame.clear();
        pending_incoming_.release(); prepared_ = false;
    }
    void target_brightness(uint8_t value,unsigned screen = 0) {
        requested_brightness_ = std::min(value,uint8_t(128));
        if (screen < screens_.size() && screen != brightness_screen_) {
            screens_[brightness_screen_].dirty = true; screens_[brightness_screen_].valid = false;
            brightness_screen_ = screen; dim_column_ = 0;
            screens_[screen].dirty = true; screens_[screen].valid = false;
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(renderer_message)
#endif
    void message(const TextMessages &message, uint32_t now) {
        if (!message.active(now)) {
            if (message_) { message_.reset(); invalidate(); }
            return;
        }
        if (message_ && message_revision_ == message.revision()) return;
        Pane pane;
        pane.graphics.size = 8; pane.graphics.line_height = 10;
        const auto viewport = display_.viewport(0);
        const int width = std::max(0, int(viewport.width) - 2 * pane.graphics.pad);
        const unsigned rows = std::min(max_lines, viewport.height > 8 ? (viewport.height - 8) / 10 : 0);
        const char *text = message.text();
        for (unsigned row = 0; row < rows; ++row) {
            auto &line = pane.lines[row]; line.graphics = pane.graphics;
            if (!row) {
                line.graphics.font = localized_face;
                utf8::copy(line.text, sizeof(line.text), i18n::text(message.kind() == TextMessages::Kind::cue ? i18n::Message::cue : i18n::Message::warning));
            }
            else {
                unsigned used = 0;
                while (*text && used + 1 < sizeof(line.text)) {
                    line.text[used] = *text; line.text[used + 1] = 0;
                    if (fonts_.bounds(17, 8, 0, line.text).width > width) { line.text[used] = 0; break; }
                    ++used; ++text;
                }
                if (row + 1 == rows && *text) {
                    used = std::min(used, unsigned(sizeof(line.text) - 4));
                    do {
                        std::memcpy(line.text + used, "...", 4);
                        if (fonts_.bounds(17, 8, 0, line.text).width <= width || !used) break;
                        --used;
                    } while (true);
                }
            }
            line.y = int16_t(4 + row * 10 - fonts_.bounds(line.graphics.font, 8, 0, line.text).top);
        }
        message_revision_ = message.revision();
        message_ = pane; invalidate();
    }
    bool ready() const {
        if (!usable_ || !display_.ready() || preparing_ || animating_ || drawing_ >= 0) return false;
        for (const auto &screen : screens_) if (screen.dirty) return false;
        return true;
    }
    void invalidate() {
        drawing_ = -1; preparing_ = animating_ = false;
        incoming_screen_ = -1;
        for (auto &screen : screens_) { screen.dirty = true; screen.valid = false; screen.ring = 0; }
        reset_scroll_ = true;
    }
    void invalidate_language() { message_.reset(); message_revision_ = 0; invalidate(); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(renderer_show)
#endif
    void show(unsigned pane, const Pane &content) {
        if (pane >= screens_.size()) return;
        auto &screen = screens_[pane];
        if (!std::memcmp(&screen.requested, &content, sizeof(content))) return;
        damage(screen, content);
        for (const auto &line : content.lines) if (line.text[0] && line.graphics.font == localized_face) {
            screen.valid = false; break;
        }
        screen.requested = content; screen.dirty = true;
    }
    void error(const char *message) {
        if (screens_.empty()) return;
        const unsigned screen = unsigned(screens_.size() / 2);
        Pane pane; pane.graphics = screens_[screen].requested.graphics;
        pane.graphics.font = localized_face; pane.graphics.size = 8; pane.graphics.asset = 0;
        pane.lines[0].graphics = pane.graphics;
        std::snprintf(pane.lines[0].text, sizeof(pane.lines[0].text), "%s", message); show(screen, pane);
    }
    void transition(bool undo, std::optional<unsigned> duration, int pane = -1, unsigned reserved = 0, bool vertical = false, bool panorama = true, unsigned pane_count = 1, bool fixed_heading = false) {
        if (message_) return;
        if (!frame_count_ || !animations_ || !usable_ || preparing_ || animating_ || !display_.ready()) return;
        for (const auto &screen : screens_) if (!screen.valid) return;
        if (frame_count_ == 2) for (const auto &screen : screens_) std::copy(cache_ + screen.offset, cache_ + screen.offset + screen.viewport.width * screen.viewport.height, previous_ + screen.offset);
        drawing_ = -1; undo_ = undo; selected_ = pane; reserved_ = std::min(reserved, unsigned(screens_.size())); vertical_ = vertical; panorama_ = panorama;
        selected_count_ = pane_count; fixed_heading_ = fixed_heading;
        duration_ = duration.value_or(vertical ? 200 : 75); preparing_ = true;
        for (auto &screen : screens_) screen.dirty = true;
    }
    void draw(uint32_t now = 0) {
        display_.poll();
        if (!usable_ || display_.priority_due()) return;
        if (display_.failed()) { invalidate(); error(i18n::text(i18n::Message::display_transfer_failed)); }
        if (!display_.ready()) return;
        if (reset_scroll_) { display_.reset_scroll(); reset_scroll_ = false; }
        if (preparing_ && frame_count_ == 1) {
            for (unsigned i = 0; i < screens_.size(); ++i) { incoming_[i] = screens_[i].requested; screens_[i].dirty = false; }
            start_animation(now); return;
        }
        if (animating_) {
            if (frame_count_ == 1 && !prepare_incoming()) return;
            animate(now); return;
        }
        if (drawing_ < 0) {
            for (unsigned i = 0; i < screens_.size(); ++i) {
                const unsigned screen = (next_screen_ + i) % screens_.size();
                if (!screens_[screen].dirty) continue;
                screens_[screen].dirty = false;
                next_screen_ = (screen + 1) % screens_.size();
                begin_paint(screen, screen == 0 && message_ ? *message_ : screens_[screen].requested); break;
            }
        }
        if (drawing_ < 0) {
            if (preparing_) start_animation(now);
            else dim();
            return;
        }
        auto &screen = screens_[unsigned(drawing_)];
        if (rendering_) { paint_step(); return; }
        const auto v = screen.viewport; bool changed = !screen.valid || !frame_count_;
        if (frame_count_ && screen.valid) for (int y = painting_region_.top; y < painting_region_.bottom; ++y) if (raster_.pixels()[y * v.width + column_] != cache_[screen.offset + y * v.width + column_]) changed = true;
        if (changed && !preparing_) {
            for (int y = painting_region_.top; y < painting_region_.bottom; ++y) encode(unsigned(y - painting_region_.top), dim_pixel(unsigned(drawing_), raster_.pixels()[y * v.width + column_]));
            if (!submit_column(unsigned(drawing_), column_, unsigned(painting_region_.bottom - painting_region_.top), unsigned(painting_region_.top))) return;
        }
        if (frame_count_) for (int y = painting_region_.top; y < painting_region_.bottom; ++y) cache_[screen.offset + y * v.width + column_] = raster_.pixels()[y * v.width + column_];
        if (++column_ == unsigned(painting_region_.right)) { screen.valid = true; drawing_ = -1; }
    }
private:
    struct Screen { Viewport viewport; unsigned offset; Pane requested; bool dirty = true, valid = false; unsigned ring = 0; Region damage; };
    Display &display_;
    const Fonts &fonts_;
    Raster raster_;
    uint16_t *cache_ = nullptr, *previous_ = nullptr;
    MemoryBuffer frames_[2], pending_frames_[2];
    CheckedArray<Pane> incoming_, pending_incoming_;
    unsigned area_ = 0, frame_count_ = 0, pending_count_ = 0;
    bool animations_ = true, pending_animations_ = true, prepared_ = false;
    int incoming_screen_ = -1;
    std::vector<Screen> screens_;
    std::vector<uint8_t> transfer_;
    Pane painting_;
    Region painting_region_;
    std::optional<Pane> message_;
    uint32_t message_revision_ = 0;
    bool usable_ = false, preparing_ = false, animating_ = false, rendering_ = false, undo_ = false, vertical_ = false, panorama_ = true, fixed_heading_ = false;
    bool reset_scroll_ = true, ring_scroll_ = false, ring_step_ = false, reveal_ = false, settling_ = false, settle_anchor_ = false;
    unsigned reveal_screen_ = 0;
    uint32_t settled_ = 0;
    unsigned reserved_ = 0;
    unsigned selected_count_ = 1, brightness_screen_ = 0, next_screen_ = 0;
    int drawing_ = -1, selected_ = -1;
    unsigned column_ = 0, field_ = 0, letter_ = 0, progress_ = 0, duration_ = 0, animation_screen_ = 0, animation_column_ = 0, dim_column_ = 0;
    uint32_t started_ = 0;
    uint8_t brightness_ = 128, requested_brightness_ = 128;
    void damage(Screen &screen, const Pane &content) {
        const auto &before = screen.requested;
        bool full = before.three_bands != content.three_bands || before.heading_band != content.heading_band || before.focused != content.focused
            || std::memcmp(&before.graphics, &content.graphics, sizeof(content.graphics));
        // Compare values, not TextLine/Rectangle padding, when deciding damage.
        for (unsigned i = 0; i < max_lines && !full; ++i) {
            const auto &a = before.lines[i], &b = content.lines[i];
            full = std::strcmp(a.text, b.text) || std::memcmp(&a.graphics, &b.graphics, sizeof(a.graphics))
                || a.x != b.x || a.y != b.y || a.centered != b.centered || a.wrap != b.wrap || a.stippled != b.stippled
                || a.color_mask != b.color_mask || a.alternate_color != b.alternate_color;
        }
        if (full) {
            screen.damage = {0, 0, int(screen.viewport.width), int(screen.viewport.height)};
            return;
        }
        for (unsigned i = 0; i < 4; ++i) {
            const auto &a = before.rectangles[i], &b = content.rectangles[i];
            if (a.x != b.x || a.y != b.y || a.width != b.width || a.height != b.height || a.color != b.color || a.stippled != b.stippled) {
                for (const auto *r : {&before.rectangles[i], &content.rectangles[i]}) screen.damage.include(r->x, r->y, r->width, r->height);
            }
            if (std::memcmp(&before.circles[i], &content.circles[i], sizeof(Circle))) {
                for (const auto *c : {&before.circles[i], &content.circles[i]}) if (c->diameter) {
                    const unsigned span = c->diameter + 2 * c->annulus;
                    screen.damage.include(int(c->x) - int(span) / 2, int(c->y) - int(span) / 2, span, span);
                }
            }
        }
        const auto &a = before.toggle, &b = content.toggle;
        if (a.x != b.x || a.y != b.y || a.visible != b.visible || a.enabled != b.enabled || a.disabled != b.disabled) {
            const unsigned height = std::max(4u, screen.viewport.height * 7 / 64);
            for (const auto *t : {&before.toggle, &content.toggle}) if (t->visible) screen.damage.include(t->x, t->y, height * 13 / 7, height);
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(renderer_begin_paint)
#endif
    void begin_paint(unsigned screen, const Pane &content) {
        drawing_ = int(screen); painting_ = content;
        auto &s = screens_[screen]; const auto v = s.viewport;
        fit_localized(painting_, v);
        painting_region_ = !s.valid || preparing_ || animating_ ? Region{0, 0, int(v.width), int(v.height)} : s.damage;
        s.damage = {};
        painting_region_ = {std::max(0, painting_region_.left), std::max(0, painting_region_.top), std::min(int(v.width), painting_region_.right), std::min(int(v.height), painting_region_.bottom)};
        if (painting_region_.empty()) { drawing_ = -1; return; }
        column_ = unsigned(painting_region_.left); field_ = letter_ = 0;
        raster_.configure(v, painting_.graphics, painting_region_); backgrounds();
        for (const auto &circle : painting_.circles) raster_.circle(circle);
        for (const auto &r : painting_.rectangles) raster_.rectangle(r.x, r.y, r.width, r.height, r.color, r.stippled);
        raster_.toggle(painting_.toggle); rendering_ = true; incoming_screen_ = -1;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(renderer_fit_localized)
#endif
    void fit_localized(Pane &pane, Viewport viewport) const {
        unsigned count = 0;
        for (const auto &line : pane.lines) if (line.text[0]) ++count;
        const unsigned pad = pane.graphics.pad;
        const unsigned available_height = viewport.height > 2 * pad ? viewport.height - 2 * pad : 0;
        for (unsigned index = 0; index < max_lines; ++index) {
            auto &line = pane.lines[index];
            if (!line.text[0] || line.graphics.font != localized_face) continue;
            const unsigned maximum = std::min(96u, unsigned(line.graphics.size));
            const unsigned height = pane.three_bands ? viewport.height / 3
                : line.y < 0 ? available_height / std::max(1u, count) : line.graphics.line_height;
            const int left = line.centered ? int(pad) : std::max(int(pad), int(line.x));
            const int width = std::max(0, int(viewport.width) - int(pad) - left);
            const auto original = fonts_.bounds(localized_face, line.graphics.size, 0, line.text);
            auto choose = [&]() {
                for (unsigned size = maximum; size >= 8; --size) {
                    if (!fonts_.supports(localized_face, size)) continue;
                    const auto bounds = fonts_.bounds(localized_face, size, 0, line.text);
                    unsigned rows = 1;
                    const unsigned line_height = std::max(size, unsigned(std::max(0, bounds.height)));
                    if (line.wrap) {
                        TextLine candidate = line; candidate.graphics.size = uint8_t(size);
                        rows = Raster::localized_position(fonts_, candidate, UINT_MAX, width).row + 1;
                    }
                    if ((!line.wrap && bounds.width > width) || rows * line_height > height) continue;
                    line.graphics.size = uint8_t(size);
                    if (line.y < 0) {
                        line.graphics.line_height = uint16_t(line_height);
                        line.graphics.top = uint16_t(std::max(0, -bounds.top));
                    } else if (size != maximum) {
                        line.y = int16_t(int(line.y) + original.top + (int(height) - bounds.height) / 2 - bounds.top);
                    }
                    return true;
                }
                return false;
            };
            if (choose()) continue;
            if (line.graphics.asset) {
                const char *shorter = i18n::short_text(static_cast<i18n::Message>(line.graphics.asset - 1));
                if (shorter && *shorter) { utf8::copy(line.text, sizeof(line.text), shorter); if (choose()) continue; }
            }
            char prefix[sizeof(line.text)]; utf8::copy(prefix, sizeof(prefix), line.text);
            while (*prefix) {
                utf8::erase_last(prefix);
                if (std::strlen(prefix) + 3 >= sizeof(line.text)) continue;
                std::snprintf(line.text, sizeof(line.text), "%s...", prefix);
                if (choose()) break;
            }
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(renderer_paint_step)
#endif
    void paint_step() {
        const auto &line = painting_.lines[field_];
        const int y = text_y(field_);
        if ((letter_ || raster_.intersects(line, y)) && raster_.letter(line, letter_, y)) ++letter_;
        else { letter_ = 0; if (++field_ == max_lines) {
            backgrounds(true);
            if (painting_.focused) {
                const auto viewport = screens_[unsigned(drawing_)].viewport;
                raster_.rectangle(0,0,viewport.width,1,painting_.graphics.foreground);
                raster_.rectangle(0,int(viewport.height) - 1,viewport.width,1,painting_.graphics.foreground);
                raster_.rectangle(0,0,1,viewport.height,painting_.graphics.foreground);
                raster_.rectangle(int(viewport.width) - 1,0,1,viewport.height,painting_.graphics.foreground);
            }
            raster_.corners(); rendering_ = false;
        } }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(renderer_start_animation)
#endif
    void start_animation(uint32_t now) {
        preparing_ = false; animating_ = true; started_ = now; progress_ = 0;
        animation_screen_ = animation_column_ = 0; incoming_screen_ = -1;
        // Native scrolling moves full-height columns; fixed headings need regional redraws.
        ring_scroll_ = !vertical_ && !fixed_heading_; ring_step_ = false; reveal_ = settling_ = settle_anchor_ = false;
        for (unsigned i = 0; i < screens_.size(); ++i)
            if (moving(i) && (display_.scroll_columns(i) <= screens_[i].viewport.width
                || screens_[i].viewport.width != screens_[0].viewport.width
                || screens_[i].viewport.gap != screens_[0].viewport.gap)) ring_scroll_ = false;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(renderer_prepare_incoming)
#endif
    bool prepare_incoming() {
        unsigned screen = animation_screen_;
        while (screen < screens_.size() && !moving(screen)) ++screen;
        if (screen == screens_.size()) return true;
        if (!vertical_ && panoramic()) {
            const unsigned first = selected_ >= 0 ? unsigned(selected_) : 0;
            const unsigned end = selected_ >= 0 ? std::min(unsigned(screens_.size()), first + selected_count_)
                : unsigned(screens_.size()) - reserved_;
            screen = undo_ ? end - 1 : first;
        }
        if (incoming_screen_ == int(screen)) return true;
        if (drawing_ < 0) begin_paint(screen, incoming_[screen]);
        if (rendering_) { paint_step(); return false; }
        incoming_screen_ = int(screen); drawing_ = -1; return true;
    }
    uint16_t incoming_pixel(unsigned screen, unsigned x, unsigned y) const {
        const auto &s = screens_[screen];
        return frame_count_ == 1 ? raster_.pixels()[y * s.viewport.width + x]
            : cache_[s.offset + y * s.viewport.width + x];
    }
    void encode(unsigned index, uint16_t pixel) { transfer_[2 * index] = uint8_t(pixel >> 8); transfer_[2 * index + 1] = uint8_t(pixel); }
    uint16_t dim_pixel(unsigned screen, uint16_t pixel) const {
        if (screen != brightness_screen_ || brightness_ == 128) return pixel;
        return uint16_t((((pixel >> 11) * brightness_ + 64) / 128) << 11 |
            (((((pixel >> 5) & 63) * brightness_ + 64) / 128) << 5) | (((pixel & 31) * brightness_ + 64) / 128));
    }
    void dim() {
        auto &screen = screens_[brightness_screen_]; if (!screen.valid) return;
        if (!frame_count_) {
            if (brightness_ != requested_brightness_) {
                brightness_ = requested_brightness_; screen.dirty = true; screen.valid = false;
            }
            return;
        }
        if (!dim_column_) { if (brightness_ == requested_brightness_) return; brightness_ = requested_brightness_; }
        for (unsigned y = 0; y < screen.viewport.height; ++y) encode(y, dim_pixel(brightness_screen_, cache_[screen.offset + y * screen.viewport.width + dim_column_]));
        if (submit_column(brightness_screen_, dim_column_, screen.viewport.height)) dim_column_ = (dim_column_ + 1) % screen.viewport.width;
    }
    int text_y(unsigned field) const {
        const auto &line = painting_.lines[field];
        if (line.y >= 0) return line.y + line.graphics.top;
        const unsigned height = screens_[unsigned(drawing_)].viewport.height;
        if (painting_.three_bands) {
            const unsigned start = field * height / 3, end = (field + 1) * height / 3, used = raster_.rows(line) * line.graphics.line_height;
            return int(start + (used < end - start ? (end - start - used) / 2 : 0) + line.graphics.top);
        }
        unsigned count = 0, used = 0, preceding = 0, index = 0;
        for (unsigned i = 0; i < max_lines; ++i) if (painting_.lines[i].text[0]) {
            const auto &item = painting_.lines[i]; unsigned rows = raster_.rows(item);
            if (i < field) { preceding += rows * item.graphics.line_height; index += rows; }
            count += rows; used += rows * item.graphics.line_height;
        }
        const unsigned pad = painting_.graphics.pad, available = height > 2 * pad ? height - 2 * pad : 0;
        return int(pad + preceding + (used < available && count ? (available - used) * (2 * index + 1) / (2 * count) : 0) + line.graphics.top);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(renderer_backgrounds)
#endif
    void backgrounds(bool preserve = false) {
        const auto v = screens_[unsigned(drawing_)].viewport;
        unsigned start = painting_.heading_band ? 0 : painting_.graphics.pad;
        for (unsigned i = 0; i < max_lines; ++i) {
            const auto &line = painting_.lines[i]; if (!line.text[0]) continue;
            if (preserve && line.y >= 0) {
                if (line.stippled) {
                    auto b = fonts_.bounds(line.graphics.font, line.graphics.size, line.graphics.asset, line.text);
                    const int x = line.centered ? (int(v.width) - b.width) / 2 : line.x + b.left;
                    raster_.rectangle(x, text_y(i) + b.top, b.width, b.height, line.graphics.background, true);
                }
                continue;
            }
            unsigned end = v.height > painting_.graphics.pad ? v.height - painting_.graphics.pad : 0;
            if (painting_.three_bands) { start = i * v.height / 3; end = (i + 1) * v.height / 3; }
            else for (unsigned j = i + 1; j < max_lines; ++j) if (painting_.lines[j].text[0]) {
                const int bottom = text_y(i) - line.graphics.top + raster_.rows(line) * line.graphics.line_height;
                end = unsigned(std::max(0, (bottom + text_y(j) - painting_.lines[j].graphics.top) / 2)); break;
            }
            const bool heading = painting_.heading_band && i == 0;
            if (heading && !painting_.three_bands) end = unsigned(std::max(0, text_y(i) - line.graphics.top + line.graphics.line_height + 2));
            const unsigned inset = heading ? 0 : painting_.graphics.pad;
            end = std::min(end, v.height > inset ? v.height - inset : 0);
            if ((!preserve || line.stippled) && end > start && v.width > 2 * inset)
                raster_.rectangle(inset, start, v.width - 2 * inset, end - start, line.graphics.background, preserve);
            start = end;
        }
    }
    bool moving(unsigned screen) const { return selected_ >= 0 ? screen >= unsigned(selected_) && screen - unsigned(selected_) < selected_count_ : screen + reserved_ < screens_.size(); }
    bool panoramic() const { return panorama_ && (selected_ < 0 || selected_count_ > 1); }
    unsigned animation_top(unsigned screen) const { return fixed_heading_ ? screens_[screen].viewport.height / 3 : 0; }
    unsigned distance() const {
        unsigned result = 1;
        for (unsigned i = 0; i < screens_.size(); ++i) if (moving(i)) result = std::max(result, vertical_ ? screens_[i].viewport.height - animation_top(i) : screens_[i].viewport.width + (panoramic() ? screens_[i].viewport.gap : 0));
        return result;
    }
    uint16_t sample(unsigned screen, unsigned x, unsigned y) const {
        const auto &s = screens_[screen]; const auto v = s.viewport;
        const unsigned top = animation_top(screen), height = v.height - top;
        const unsigned shift = progress_ * (vertical_ ? height : v.width + (panoramic() ? v.gap : 0)) / distance();
        if (vertical_) {
            int source = int(y - top) + (undo_ ? -int(shift) : int(shift)); const bool incoming = source < 0 || source >= int(height);
            source = int(top) + (source + int(height)) % int(height);
            return incoming ? incoming_pixel(screen, x, unsigned(source)) : previous_[s.offset + unsigned(source) * v.width + x];
        }
        int source = int(x) + (undo_ ? int(shift) : -int(shift));
        if (!panoramic()) {
            const bool incoming = source < 0 || source >= int(v.width); source = (source + int(v.width)) % int(v.width);
            return incoming ? incoming_pixel(screen, unsigned(source), y) : previous_[s.offset + y * v.width + unsigned(source)];
        }
        int pane = int(screen);
        int first = selected_ >= 0 ? selected_ : 0;
        int end = selected_ >= 0 ? std::min(int(screens_.size()),selected_ + int(selected_count_)) : int(screens_.size());
        while (source < 0 && pane > first) { --pane; source += int(screens_[unsigned(pane)].viewport.width + screens_[unsigned(pane)].viewport.gap); }
        while (pane + 1 < end && source >= int(screens_[unsigned(pane)].viewport.width + screens_[unsigned(pane)].viewport.gap)) { source -= int(screens_[unsigned(pane)].viewport.width + screens_[unsigned(pane)].viewport.gap); ++pane; }
        const auto &p = screens_[unsigned(pane)];
        if (source < 0 || (pane + 1 == end && source >= int(p.viewport.width + p.viewport.gap))) {
            source += source < 0 ? int(p.viewport.width + p.viewport.gap) : -int(p.viewport.width + p.viewport.gap);
            if (source < 0 || source >= int(p.viewport.width)) return 0;
            return incoming_pixel(unsigned(pane), unsigned(source), std::min(y, p.viewport.height - 1));
        }
        if (source >= int(p.viewport.width) || y >= p.viewport.height) return 0;
        if (reserved_ && undo_ && unsigned(pane) >= screens_.size() - reserved_ && screens_.size() > reserved_) { const auto &left = screens_[screens_.size() - reserved_ - 1]; return incoming_pixel(unsigned(screens_.size()) - reserved_ - 1, std::min(unsigned(source), left.viewport.width - 1), std::min(y, left.viewport.height - 1)); }
        return previous_[p.offset + y * p.viewport.width + unsigned(source)];
    }
    bool submit_column(unsigned screen, unsigned column, unsigned height, unsigned top = 0) {
        const unsigned columns = display_.scroll_columns(screen);
        if (columns && !top) return display_.ring_column(screen, (screens_[screen].ring + column) % columns, transfer_.data(), height);
        return display_.submit(screen, columns ? (screens_[screen].ring + column) % columns : column, top, 1, height, transfer_.data());
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(renderer_animate_ring)
#endif
    void animate_ring(uint32_t now) {
        if (settle_anchor_) {
            settled_ = now; settle_anchor_ = false; settling_ = true; return;
        }
        if (settling_) {
            if (uint32_t(now - settled_) < display_.scroll_settle_us()) return;
            settling_ = false;
        }
        if (reveal_) {
            if (!reveal_screen_ && selected_ < 0 && !reserved_) {
                bool same = true;
                for (const auto &s : screens_) if (s.ring != screens_[0].ring) same = false;
                if (same && display_.scroll_all(screens_[0].ring)) { reveal_ = false; settle_anchor_ = true; return; }
            }
            while (reveal_screen_ < screens_.size() && !moving(reveal_screen_)) ++reveal_screen_;
            if (reveal_screen_ < screens_.size()) { display_.scroll(reveal_screen_, screens_[reveal_screen_].ring); ++reveal_screen_; return; }
            reveal_ = false; settle_anchor_ = true; return;
        }
        if (!ring_step_) {
            if (progress_ == distance()) {
                if (frame_count_ == 2) for (unsigned i = 0; i < screens_.size(); ++i) if (moving(i)) {
                    const auto &s = screens_[i];
                    for (unsigned y = 0; y < s.viewport.height; ++y) for (unsigned x = 0; x < s.viewport.width; ++x)
                        raster_.pixels()[y * s.viewport.width + x] = sample(i, x, y);
                    std::copy(raster_.pixels(), raster_.pixels() + s.viewport.width * s.viewport.height, cache_ + s.offset);
                }
                animating_ = false;
                for (unsigned i = 0; i < screens_.size(); ++i) {
                    screens_[i].dirty = true;
                    if (frame_count_ == 1 && moving(i)) screens_[i].valid = false;
                }
                return;
            }
            unsigned quantum = 1;
            for (unsigned i = 0; i < screens_.size(); ++i) if (moving(i)) quantum = std::max(quantum, display_.scroll_quantum(i));
            const unsigned deadline = std::min(distance(), (progress_ / quantum + 1) * quantum);
            if (uint64_t(uint32_t(now - started_)) < uint64_t(deadline) * duration_ * 1000 / distance()) return;
            ++progress_; ring_step_ = true;
        }
        while (animation_screen_ < screens_.size() && !moving(animation_screen_)) ++animation_screen_;
        if (animation_screen_ == screens_.size()) { animating_ = false; return; }
        auto &screen = screens_[animation_screen_];
        const auto v = screen.viewport; const unsigned columns = display_.scroll_columns(animation_screen_);
        const unsigned ring = (screen.ring + (undo_ ? 1 : columns - 1)) % columns;
        const unsigned x = undo_ ? v.width - 1 : 0;
        for (unsigned y = 0; y < v.height; ++y) encode(y, dim_pixel(animation_screen_, sample(animation_screen_, x, y)));
        if (!display_.ring_column(animation_screen_, (ring + x) % columns, transfer_.data(), v.height)) return;
        screen.ring = ring;
        do { ++animation_screen_; } while (animation_screen_ < screens_.size() && !moving(animation_screen_));
        if (animation_screen_ == screens_.size()) {
            animation_screen_ = 0; ring_step_ = false; unsigned quantum = 1;
            for (unsigned i = 0; i < screens_.size(); ++i) if (moving(i)) quantum = std::max(quantum, display_.scroll_quantum(i));
            if (!(progress_ % quantum) || progress_ == distance()) { reveal_ = true; reveal_screen_ = 0; }
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(renderer_animate)
#endif
    void animate(uint32_t now) {
        if (ring_scroll_) { animate_ring(now); return; }
        if (!animation_screen_ && !animation_column_) {
            const unsigned next = std::min(distance(), progress_ + std::max(1u, distance() / 4));
            if (uint64_t(uint32_t(now - started_)) < uint64_t(next) * duration_ * 1000 / distance()) return;
            progress_ = next;
        }
        while (animation_screen_ < screens_.size() && !moving(animation_screen_)) ++animation_screen_;
        if (animation_screen_ == screens_.size()) { animating_ = false; return; }
        auto &screen = screens_[animation_screen_]; const auto v = screen.viewport;
        const unsigned top = animation_top(animation_screen_), height = v.height - top;
        const unsigned width = display_.scroll_columns(animation_screen_) ? 1u : std::min(8u, v.width - animation_column_);
        for (unsigned x = 0; x < width; ++x) for (unsigned y = top; y < v.height; ++y) encode(x * height + y - top, dim_pixel(animation_screen_, sample(animation_screen_, animation_column_ + x, y)));
        if (!(width == 1 ? submit_column(animation_screen_, animation_column_, height, top) : display_.submit(animation_screen_, animation_column_, top, width, height, transfer_.data()))) return;
        animation_column_ += width;
        if (animation_column_ == v.width) { animation_column_ = 0; do { ++animation_screen_; } while (animation_screen_ < screens_.size() && !moving(animation_screen_)); }
        if (animation_screen_ == screens_.size()) {
            animation_screen_ = 0;
            if (progress_ == distance()) { animating_ = false; for (auto &s : screens_) { s.valid = false; s.dirty = true; } }
        }
    }
};
} // namespace cntrl::ui
