#pragma once
#include "ui/messages.h"
#include <cntrl/usb_config.h>
#include "controller.h"
#include "patch_edit.h"
#include "cycle_edit.h"
#include "setup_names.h"
#include "revisions.h"
#include <cstdlib>

namespace cntrl {
struct Replies {
    virtual ~Replies() = default;
    virtual bool reply_ready() const { return true; }
    virtual void reply(const char *text) = 0;
    virtual void data(const uint8_t *bytes, unsigned length) = 0;
};

class Documents {
public:
    Documents(SetupStorage &storage, Hardware &hardware, Controller &controller, Replies &replies, GlobalSettings *globals = nullptr)
        : revisions_(storage), storage_(storage), hardware_(hardware), controller_(controller), replies_(replies), globals_(globals) {}
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_boot)
#endif
    void boot() {
        empty();
        if (globals_ && !globals_->ready()) { std::strcpy(boot_error_, "global-settings-invalid"); return; }
        if (!storage_.available()) { std::strcpy(boot_error_, "storage-unavailable"); return; }
        int stored = read_selection();
        if (stored < 0) { std::strcpy(boot_error_, "selection-record-invalid"); return; }
        if (stored && selected_[0]) {
            if (!select(selected_)) std::strcpy(boot_error_, "selected-setup-invalid");
        } else if (!stored) {
            char first[64];
            if (next_valid("", first)) {
                if (!select(first)) std::strcpy(boot_error_, "selection-write");
            } else if (present_ && !generation_) std::strcpy(boot_error_, "invalid-setups");
        }
    }
    const Document &document() const { return document_; }
    const char *selected() const { return selected_; }
    bool busy() const { return browse_phase_ != BrowsePhase::idle || history_action_ != HistoryAction::none || receiving_ || writing_ || erasing_ || download_ || listing_; }
    bool dirty() const { return dirty_; }
    void bind_globals(GlobalSettings &globals) { globals_ = &globals; document_.bind_globals(&globals.values()); }
    const GlobalSettings::Values *global_preferences() const {
        return globals_ && globals_->ready() ? &globals_->values() : nullptr;
    }
    const char *startup_set() const { return globals_ ? globals_->values().startup_set : ""; }
    const char *name_and_contact() const { return globals_ ? globals_->values().name_and_contact : ""; }
    bool name_and_contact(const char *text) {
        if (!globals_ || busy() || dirty_) return false;
        auto values = globals_->values();
        if (std::strlen(text) >= sizeof(values.name_and_contact)) return false;
        std::strcpy(values.name_and_contact, text);
        return global_replace(values, false);
    }
    bool startup_set(const char *name) {
        if (!globals_ || busy() || dirty_) return false;
        auto values = globals_->values();
        if (std::strlen(name) >= sizeof(values.startup_set)) return false;
        std::snprintf(values.startup_set, sizeof(values.startup_set), "%s", name);
        return global_replace(values, false);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_global_change)
#endif
    bool global_change(unsigned field, uint32_t value, bool clear, bool activity_busy) {
        GlobalSettings *target = globals_;
        GlobalSettings::Values candidate;
        if (!target || !target->candidate(field, value, clear, candidate)) return global_error("global-value");
        return global_replace(candidate, activity_busy);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_global_replace)
#endif
    bool global_replace(const GlobalSettings::Values &values, bool activity_busy) {
        GlobalSettings *target = globals_;
        if (!target || !target->ready()) return global_error("global-unavailable");
        if (!GlobalSettings::valid(values)) return global_error("global-value");
        uint64_t changed = target->values().known ^ values.known;
        for (unsigned i = 0; i < GlobalSettings::count; ++i)
            if (target->values().values[i] != values.values[i]) changed |= uint64_t(1) << i;
        const bool language_changed = std::strcmp(target->values().language, values.language) != 0;
        const bool startup_changed = std::strcmp(target->values().startup_set, values.startup_set) != 0;
        const bool contact_changed = std::strcmp(target->values().name_and_contact, values.name_and_contact) != 0;
        if (!changed && !language_changed && !startup_changed && !contact_changed) { last_error_[0] = 0; return true; }
        if (startup_changed && (busy() || dirty_ || activity_busy)) return global_error("global-busy");
        if (contact_changed && busy()) return global_error("global-busy");
        if (changed && (busy() || dirty_ || ((changed & ~uint64_t(3)) && activity_busy))) return global_error("global-busy");
        if (changed & ~uint64_t(7)) {
            if (!validate_globals(values)) return false;
        }
        Document prepared;
        bool reload = document_.valid() && bool(changed & ~document_.explicit_settings() & ~uint64_t(7));
        const GlobalSettings::Values previous = target->values();
        if (reload) {
            // Prepared pages retain Document copies. All copies must point at
            // stable store memory, never the caller's proposed Values object.
            target->values_ = values;
            if (!prepared.load(active_.data, document_.length(), hardware_, &target->values()) || !prepare_resources(prepared)) {
                target->values_ = previous;
                return global_error("global-resources");
            }
        }
        if (language_changed && hardware_.prepare_language(values.language[0] ? values.language : "en") != LanguagePreparation::ready) {
            if (reload) { target->values_ = previous; finish_resources(false); }
            return global_error("global-language");
        }
        if (!target->commit(values)) {
            if (reload) target->values_ = previous;
            if (reload) finish_resources(false);
            if (language_changed) hardware_.finish_language(false);
            return global_error("global-write");
        }
        if (language_changed) hardware_.finish_language(true);
        if (document_.valid()) {
            const uint64_t affected = changed & ~document_.explicit_settings();
            if (reload) {
                prepared.bind_globals(&target->values()); document_ = prepared;
                finish_resources(true); controller_.activate(document_);
            } else {
                document_.bind_globals(&target->values());
                if (affected & 1) { hardware_.set_brightness(document_.brightness()); controller_.runtime_brightness(document_.brightness()); }
                if (affected & 2) hardware_.set_splash(document_.splash(), document_.screensaver_timeout());
                if (affected & 4) hardware_.set_midi_interface(document_.is_midi_interface());
            }
        }
        last_error_[0] = 0; return true;
    }
    const char *last_error() const { return last_error_; }
    bool next_valid(const char *after, char *name) {
        char cursor[64]; std::snprintf(cursor, sizeof(cursor), "%s", after);
        while (storage_.next(cursor, name)) {
            std::snprintf(cursor, sizeof(cursor), "%s", name);
            if (load(name)) { std::snprintf(name, 64, "%s", record_name_); return true; }
        }
        return false;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_select)
#endif
    bool select(const char *name) {
        if (busy() || dirty_ || !setup_name(name) || !load(name)) return false;
        Document candidate;
        if (!candidate.load(staging_.data, length_, hardware_, global_values())) return false;
        last_error_[0] = 0; browse_ = Browse(); browse_.select = true;
        quiet_ = true; browse_phase_ = BrowsePhase::activate;
        browse_step();
        return !last_error_[0];
    }
    bool browse(const char *after, bool backward, bool select_result = false) {
        if (busy() || dirty_ || !storage_.available()) return false;
        browse_ = Browse(); std::snprintf(browse_.reference, 64, "%s", after);
        browse_.backward = backward; browse_.select = select_result;
        last_error_[0] = 0; quiet_ = !select_result;
        storage_.catalogue_begin(); browse_phase_ = BrowsePhase::scan; return true;
    }
    const char *browse_result() const { return browse_.result; }
    bool rotate(bool backward) { return browse(selected_, backward, true); }
    void redraw() { if (document_.valid()) { if (!hardware_.refresh_pages()) controller_.refresh(); } else empty(); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_edit_begin)
#endif
    bool edit_begin(const char *name = nullptr) {
        if (busy() || dirty_ || hardware_.configuration_locked()) return false;
        if (name && name_compare(name, selected_)) {
            if (!setup_name(name) || !storage_.available()) return false;
            browse_ = Browse();
            std::strcpy(browse_.candidate, name);
            browse_.loading = browse_.edit = true;
            last_error_[0] = 0;
            quiet_ = true;
            storage_.choose(name);
            browse_phase_ = BrowsePhase::header;
            return true;
        }
        if (!document_.valid() || !allocate(staging_, document_.length())) return false;
        std::memcpy(staging_.data, active_.data, document_.length());
        std::strcpy(edit_name_, selected_);
        dirty_ = true;
        return true;
    }
    const char *edit_name() const { return edit_name_; }
    bool edit_reloads() const { return !name_compare(edit_name_, selected_); }
    bool edit_document(Document &draft) const {
        return dirty_ && draft.load(staging_.data, staging_.size, hardware_, global_values());
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_edit_add_cycle)
#endif
    bool edit_add_cycle(const char *name, unsigned instrument, unsigned tag) {
        Document current, candidate;
        if (!dirty_ || busy() || hardware_.configuration_locked()
            || !current.load(staging_.data, staging_.size, hardware_, global_values())
            || !append_cycle(current, name, instrument, tag, scratch_)
            || !candidate.load(scratch_.data, scratch_.size, hardware_, global_values())) return false;
        if (candidate.page_ram2() > hardware_.memory_capacity()
            || hardware_.memory_runtime() > hardware_.memory_capacity() - candidate.page_ram2()) return false;
        staging_.swap(scratch_);
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_edit_cycle_name_used)
#endif
    bool edit_cycle_name_used(const char *name) const {
        Document current;
        if (dirty_ && current.load(staging_.data, staging_.size, hardware_, global_values()))
            for (unsigned i = 0; i < current.patch_cycles(); ++i)
                if (!name_compare(name, reinterpret_cast<const char *>(current.patch_cycle(i)))) return true;
        return false;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_edit_value)
#endif
    int64_t edit_value(unsigned patch, unsigned field) const {
        Document draft;
        return edit_document(draft) ? edit_value(draft, patch, field) : 0;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_edit_value_for_document)
#endif
    static int64_t edit_value(const Document &d, unsigned patch, unsigned field) {
        if (field <= 4 && patch >= d.patches()) return 0;
        const uint8_t *p = patch < d.patches() ? d.patch(patch) : nullptr;
        if (p && u16(p) == 65535 && field <= 4) return -1;
        switch (field) {
        case 0: return p[4] + 1;
        case 1: return u32(d.bank(patch)) == 0xffffffffu ? int64_t(-1) : int64_t(u32(d.bank(patch)));
        case 2: return d.instrument(u16(p))[64] + 1;
        case 3: return p[7] == 255 ? -1 : p[7];
        case 4: return p[8] == 255 ? -65 : int(p[8]) - 64;
        case 5: return d.brightness();
        case 7: return d.is_midi_interface();
        default: return int(d.screensaver_timeout() / 1000);
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_edit_adjust)
#endif
    bool edit_adjust(unsigned patch, unsigned field, int delta, bool original = false) {
        Document d;
        if (!dirty_ || !d.load(staging_.data, staging_.size, hardware_, global_values()) || (field <= 4 && patch >= d.patches()) || !delta || field > 7) return false;
        uint8_t *p = patch < d.patches() ? staging_.data + (d.patch(patch) - staging_.data) : nullptr;
        if (p && u16(p) == 65535 && field <= 4) return false;
        bool reference = false;
        int node = patch < d.patches() ? patch_edit_node(d, d.browser_root(patch), &reference) : -1;
        if ((field <= 4 && node < 0) || (original && !reference)) return false;
        uint8_t selection[11];
        if (field <= 4) std::memcpy(selection, d.node(unsigned(node)) + 4, sizeof(selection));
        int64_t old = edit_value(d, patch, field), value = old + delta;
        if (field == 0 && value >= 1 && value <= 128) selection[2] = uint8_t(value - 1);
        else if (field == 1) {
            uint32_t bank = 0xffffffffu; const uint8_t *mapping = nullptr;
            for (unsigned i = 0; i < d.mappings(); ++i) {
                const uint8_t *m = d.mapping(i); uint32_t candidate = u32(m + 2);
                if (u16(m) != u16(selection)) continue;
                if (delta > 0 && (old < 0 || candidate > unsigned(old)) && (!mapping || candidate < bank)) { bank = candidate; mapping = m; }
                if (delta < 0 && old >= 0 && candidate < unsigned(old) && (!mapping || candidate > bank)) { bank = candidate; mapping = m; }
            }
            if ((delta > 0 && !mapping) || (delta < 0 && old < 0)) return false;
            selection[3] = mapping ? mapping[6] : 255;
            selection[4] = mapping ? mapping[7] : 255;
            put32(selection + 7, bank);
        } else if (field == 3 && value >= -1 && value <= 127) selection[5] = value < 0 ? 255 : uint8_t(value);
        else if (field == 4 && value >= -65 && value <= 63) selection[6] = value == -65 ? 255 : uint8_t(value + 64);
        else if (!(field == 2 && value >= 1 && value <= 16)
            && !(field == 5 && value >= 0 && value <= 255)
            && !(field == 6 && value >= 0 && value <= 2147483)
            && !(field == 7 && value >= 0 && value <= 1)) return false;
        if (field <= 4 && field != 2 && ((reference && !original) || d.cycle_catalogue().contains_node(unsigned(node)))) {
            if (!detach_patch(d, patch, scratch_) || !d.load(scratch_.data, scratch_.size, hardware_, global_values())) {
                hardware_.error(ui::i18n::text(ui::i18n::Message::save_failed));
                return false;
            }
            node = patch_edit_node(d, d.browser_root(patch));
            if (node < 0) return false;
        } else {
            if (!allocate(scratch_, staging_.size)) return false;
            std::memcpy(scratch_.data, staging_.data, staging_.size);
        }
        auto *bytes = scratch_.data;
        auto address = [&](const uint8_t *p) { return bytes + (p - d.data()); };
        if (field == 2) address(d.instrument(u16(selection)))[64] = uint8_t(value - 1);
        else if (field <= 4) {
            std::memcpy(address(d.node(unsigned(node))) + 4, selection, sizeof(selection));
            for (unsigned i = 0; i < d.patches(); ++i) {
                if (patch_edit_node(d, d.browser_root(i)) != node) continue;
                std::memcpy(address(d.patch(i)) + 4, selection + 2, 5);
                std::memcpy(address(d.bank(i)), selection + 7, 4);
            }
        } else if (field == 5) bytes[d.core_offset() + 18] = uint8_t(value);
        else if (field == 6) put32(bytes + d.core_offset() + 24, unsigned(value) * 1000);
        else bytes[d.core_offset() + 19] = (bytes[d.core_offset() + 19] & ~uint8_t(2)) | (uint8_t(value) << 1);
        if (field >= 5 && d.global_mask_offset()) {
            uint64_t mask = d.explicit_settings() | (uint64_t(1) << (field - 5));
            for (unsigned i = 0; i < 8; ++i) bytes[d.global_mask_offset() + i] = uint8_t(mask >> (i * 8));
        }
        binary::seal_setup(bytes, scratch_.size);
        Document candidate;
        if (!candidate.load(bytes, scratch_.size, hardware_, global_values())) return false;
        staging_.swap(scratch_);
        return true;
    }
    bool edit_has_original(unsigned patch) const {
        Document d;
        if (!dirty_ || !d.load(staging_.data, staging_.size, hardware_, global_values()) || patch >= d.patches()) return false;
        bool reference = false;
        return patch_edit_node(d, d.browser_root(patch), &reference) >= 0 && reference;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_save_trs_mode)
#endif
    bool save_trs_mode(unsigned jack, midi::TrsMode mode, uint32_t now) {
        if (jack >= 4 || dirty_ || !edit_begin()) return false;
        staging_.data[document_.metadata() + trs_modes_offset + jack] = uint8_t(mode);
        if (mode != midi::TrsMode::aux) staging_.data[document_.metadata() + scrolling_offset + 5] &= ~(4u << jack);
        if (document_.global_mask_offset()) {
            uint64_t mask = document_.explicit_settings() | (uint64_t(1) << (19 + jack));
            if (mode != midi::TrsMode::aux) mask |= uint64_t(1) << (23 + jack);
            for (unsigned i = 0; i < 8; ++i) staging_.data[document_.global_mask_offset() + i] = uint8_t(mask >> (i * 8));
        }
        binary::seal_setup(staging_.data, document_.length());
        Document candidate;
        if (candidate.load(staging_.data, document_.length(), hardware_, global_values()) && edit_save(now)) return true;
        edit_cancel(); return false;
    }
    void edit_cancel() { dirty_ = false; staging_.clear(); scratch_.clear(); }
    bool edit_save(uint32_t now) {
        if (!dirty_ || busy() || hardware_.configuration_locked()) return false;
        if (!allocate(scratch_, staging_.size)) return false;
        std::strcpy(record_name_, edit_name_);
        expected_ = staging_.size; expected_crc_ = checksum(staging_.data, expected_); quiet_ = true;
        if (!start_write(now)) return false;
        return true;
    }
    bool accepts_input() const { return browse_phase_ == BrowsePhase::idle && history_action_ == HistoryAction::none && !writing_ && !erasing_; }
    void keepalive(uint32_t now) { last_byte_ = now; }
    void cancel_transfer() { receiving_ = listing_ = overflow_ = false; download_ = used_ = 0; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(document_data)
#endif
    void receive_data(const uint8_t *bytes, unsigned length, uint32_t now) {
        if (!receiving_ || !length || length > expected_ - received_) { error("unexpected-binary-data"); return; }
        last_byte_ = now; std::memcpy(staging_.data + received_, bytes, length); received_ += length;
        if (received_ == expected_) { receiving_ = false; received(now); }
    }
    void receive(uint8_t byte, uint32_t now) {
        if (browse_phase_ != BrowsePhase::idle || history_action_ != HistoryAction::none || writing_ || erasing_) return;
        last_byte_ = now;
        if (receiving_) {
            receive_data(&byte, 1, now);
            return;
        }
        if (byte == '\n') {
            line_[used_] = 0;
            if (!overflow_) command(now); else error("command-too-long");
            used_ = 0; overflow_ = false;
        } else if (byte != '\r') {
            if (used_ < sizeof(line_) - 1) line_[used_++] = char(byte); else overflow_ = true;
        }
    }
    void tick(uint32_t now) {
        if (epoch_) { unsigned seconds = uint32_t(now - clock_) / 1000; epoch_ += seconds; clock_ += seconds * 1000; }
        if ((receiving_ || used_ || overflow_ || listing_ || download_) && uint32_t(now - last_byte_) > 10000) {
            receiving_ = false; listing_ = false; download_ = 0; used_ = 0; overflow_ = false; error("transfer-timeout");
        }
        if (browse_phase_ != BrowsePhase::idle) { browse_tick(now); return; }
        if (history_action_ != HistoryAction::none) { history_tick(now); return; }
        if (erasing_) { erase_step(now); return; }
        if (!writing_) {
            if (!dirty_ && !receiving_ && !download_ && !listing_) {
                staging_.clear(); scratch_.clear();
            }
            return;
        }
        unsigned n = expected_ - written_; if (n > 512) n = 512;
        if (n && !storage_.write(staging_.data + written_, n)) { storage_.finish(); writing_ = false; error("storage-write"); return; }
        written_ += n; if (written_ != expected_) return;
        if (!name_compare(record_name_,selected_)) {
            if (!hardware_.configuration_handoff()) return;
        } else if (!hardware_.output_idle() || hardware_.configuration_locked()) return;
        bool done = storage_.write(reinterpret_cast<const uint8_t *>("DONE"), 4);
        done = storage_.finish() && done; writing_ = false;
        char name[64]; std::snprintf(name, sizeof(name), "%s", record_name_);
        uint32_t generation = generation_ + 1;
        // Read-back reuses staging. Keep editable bytes if verification fails.
        if (dirty_) std::memcpy(scratch_.data, staging_.data, expected_);
        if (!done || !load(name) || generation_ != generation || length_ != expected_ || checksum(staging_.data, length_) != expected_crc_) {
            if (dirty_) staging_.swap(scratch_);
            error("storage-verify"); return;
        }
        if (!name_compare(name, selected_)) {
            Document candidate;
            if (!candidate.load(staging_.data, length_, hardware_, global_values()) || !prepare_resources(candidate)) { error("setup-resources"); return; }
            finish_resources(true); activate(candidate);
        }
        if (!quiet_) replies_.reply("CNTRL OK");
        quiet_ = false; dirty_ = false;
    }
    struct HistoryComparison { unsigned patches = 0, routes = 0; bool settings = false, formatting = false; };
    const Revision &history_revision() const { return history_revision_; }
    const HistoryComparison &history_comparison() const { return comparison_; }
    bool history_next(const char *name, uint64_t before) { return begin_history(HistoryAction::list, name, before); }
    bool history_save(const char *name, const char *label) { return begin_history(HistoryAction::save, name, 0, label); }
    bool history_restore(const char *name, uint64_t number, uint32_t) { return begin_history(HistoryAction::restore, name, number); }
    bool history_compare(const char *name, uint64_t number) { return begin_history(HistoryAction::compare, name, number); }
    bool history_delete(const char *name, uint64_t number) { return begin_history(HistoryAction::erase, name, number); }
private:
    enum class BrowsePhase { idle, scan, header, body, trailer, activate };
    struct Browse {
        char reference[64] = {}, candidate[64] = {}, canonical[64] = {}, result[64] = {};
        char first[64] = {}, last[64] = {}, before[64] = {}, after[64] = {};
        unsigned slot = 0, best_slot = 0, length = 0, position = 0, best_length = 0, count = 0;
        uint32_t generation = 0, best_generation = 0, crc = 0xffffffff, expected_crc = 0, progress = 0;
        bool valid = false, backward = false, select = false, loading = false, final = false, edit = false;
    } browse_;
    BrowsePhase browse_phase_ = BrowsePhase::idle;
    void browse_fail() { browse_phase_ = BrowsePhase::idle; error("catalogue-failed"); }
    void browse_next_slot() {
        if (browse_.final) { browse_fail(); return; }
        if (++browse_.slot < 2) { browse_phase_ = BrowsePhase::header; return; }
        if (browse_.loading) {
            if (!browse_.valid || !browse_.best_length) { browse_fail(); return; }
            browse_.slot = browse_.best_slot; browse_.final = true; browse_phase_ = BrowsePhase::header; return;
        }
        if (browse_.valid && browse_.best_length) {
            const char *name = browse_.canonical;
            auto copy = [&](char *destination) { std::snprintf(destination, 64, "%s", name); };
            if (!browse_.first[0] || name_compare(name, browse_.first) < 0) copy(browse_.first);
            if (!browse_.last[0] || name_compare(name, browse_.last) > 0) copy(browse_.last);
            if (name_compare(name, browse_.reference) < 0 && (!browse_.before[0] || name_compare(name, browse_.before) > 0)) copy(browse_.before);
            if (name_compare(name, browse_.reference) > 0 && (!browse_.after[0] || name_compare(name, browse_.after) < 0)) copy(browse_.after);
            ++browse_.count;
        }
        browse_phase_ = BrowsePhase::scan;
    }
    void browse_tick(uint32_t now) {
        browse_step();
        if (!quiet_ && browse_phase_ != BrowsePhase::idle && browse_.select && uint32_t(now - browse_.progress) >= 500) {
            replies_.reply("CNTRL WAIT"); browse_.progress = now;
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_browse_step)
#endif
    void browse_step() {
        switch (browse_phase_) {
        case BrowsePhase::idle: return;
        case BrowsePhase::scan:
            if (storage_.catalogue_step(browse_.candidate)) {
                if (!storage_.available()) { browse_fail(); return; }
                const char *result = browse_.backward ? (browse_.before[0] ? browse_.before : browse_.last)
                    : (browse_.after[0] ? browse_.after : browse_.first);
                std::snprintf(browse_.result, 64, "%s", result);
                if (!browse_.select) { browse_phase_ = BrowsePhase::idle; quiet_ = false; return; }
                if (browse_.count < 2) { browse_phase_ = BrowsePhase::idle; error("rotation-failed"); return; }
                std::snprintf(browse_.candidate, 64, "%s", result); browse_.loading = true;
            } else if (!browse_.candidate[0]) return;
            storage_.choose(browse_.candidate);
            browse_.slot = 0; browse_.valid = false; browse_phase_ = BrowsePhase::header; return;
        case BrowsePhase::header:
            if (!storage_.read(browse_.slot, 0, header_, 96) || !header_valid(header_)
                || name_compare(browse_.candidate, reinterpret_cast<const char *>(header_ + 20))) { browse_next_slot(); return; }
            browse_.length = u32(header_ + 8); browse_.generation = u32(header_ + 4);
            if (storage_.size(browse_.slot) != browse_.length + 100
                || (browse_.final && (browse_.length != browse_.best_length || browse_.generation != browse_.best_generation))) { browse_next_slot(); return; }
            if (!allocate(scratch_, browse_.length)) { browse_fail(); return; }
            browse_.expected_crc = u32(header_ + 12); browse_.crc = 0xffffffff; browse_.position = 0;
            browse_phase_ = browse_.length ? BrowsePhase::body : BrowsePhase::trailer; return;
        case BrowsePhase::body: {
            unsigned n = browse_.length - browse_.position; if (n > 512) n = 512;
            uint8_t *p = scratch_.data + browse_.position;
            if (!storage_.read(browse_.slot, 96 + browse_.position, p, n)) { browse_next_slot(); return; }
            for (unsigned i = 0; i < n; ++i) {
                browse_.crc ^= p[i];
                for (unsigned b = 0; b < 8; ++b) browse_.crc = (browse_.crc >> 1) ^ (0xedb88320u & (0u - (browse_.crc & 1)));
            }
            browse_.position += n;
            if (browse_.position == browse_.length) browse_phase_ = BrowsePhase::trailer;
            return;
        }
        case BrowsePhase::trailer: {
            uint8_t trailer[4]; Document candidate;
            if (!storage_.read(browse_.slot, 96 + browse_.length, trailer, 4) || std::memcmp(trailer, "DONE", 4)
                || ~browse_.crc != browse_.expected_crc || (browse_.length && !candidate.load(scratch_.data, browse_.length, hardware_, global_values()))) {
                browse_next_slot(); return;
            }
            if (browse_.final) {
                staging_.swap(scratch_);
                if (browse_.edit) {
                    std::strcpy(edit_name_, reinterpret_cast<const char *>(header_ + 20));
                    dirty_ = true; quiet_ = false;
                    browse_phase_ = BrowsePhase::idle;
                    return;
                }
                length_ = browse_.length; generation_ = browse_.generation; slot_ = 1 - browse_.slot;
                std::snprintf(record_name_, 64, "%s", header_ + 20);
                browse_phase_ = BrowsePhase::activate; return;
            }
            if (!browse_.valid || int32_t(browse_.generation - browse_.best_generation) > 0) {
                browse_.valid = true; browse_.best_generation = browse_.generation; browse_.best_length = browse_.length; browse_.best_slot = browse_.slot;
                std::snprintf(browse_.canonical, 64, "%s", header_ + 20);
            }
            browse_next_slot(); return;
        }
        case BrowsePhase::activate: {
            Document candidate;
            if (!candidate.load(staging_.data, length_, hardware_, global_values())) { browse_fail(); return; }
            if (document_.valid() && !hardware_.configuration_handoff()) return;
            if (!prepare_resources(candidate)) { browse_fail(); return; }
            if (std::strcmp(selected_, record_name_) && !persist_selection(record_name_)) {
                finish_resources(false); browse_fail(); return;
            }
            finish_resources(true); std::snprintf(selected_, 64, "%s", record_name_);
            activate(candidate); browse_phase_ = BrowsePhase::idle;
            if (!quiet_) replies_.reply("CNTRL OK");
            quiet_ = false; return;
        }
        }
    }
    enum class HistoryAction { none, list, save, restore, compare, download, erase, archive };
    enum class HistoryPhase { current, selected, revision, append, erase };
    HistoryAction history_action_ = HistoryAction::none;
    HistoryPhase history_phase_ = HistoryPhase::current;
    Revisions revisions_;
    Revision history_revision_;
    HistoryComparison comparison_;
    // History reads/current bytes and deletion names have exclusive lifetimes.
    MemoryBuffer scratch_;
    char history_name_[64] = {}, history_label_[64] = {};
    uint64_t history_number_ = 0;
    unsigned history_slot_ = 0, current_length_ = 0, current_slot_ = 0;
    uint32_t current_generation_ = 0, history_progress_ = 0;
    bool history_reply_ = false;
    SetupStorage &storage_; Hardware &hardware_; Controller &controller_; Replies &replies_;
    MemoryBuffer active_, staging_;
    uint8_t header_[96] = {};
    Document document_;
    GlobalSettings *globals_ = nullptr;
    char selected_[64] = {}, record_name_[64] = {}, edit_name_[64] = {}, cursor_[64] = {}, line_[256] = {}, last_error_[64] = {}, boot_error_[64] = {};
    unsigned used_ = 0, received_ = 0, expected_ = 0, written_ = 0, slot_ = 0, length_ = 0, download_ = 0;
    unsigned delete_length_ = 0, delete_offset_ = 0;
    uint32_t expected_crc_ = 0, generation_ = 0, modified_ = 0, last_byte_ = 0, epoch_ = 0, clock_ = 0;
    uint32_t selection_generation_ = 0;
    int selection_slot_ = -1;
    bool receiving_ = false, writing_ = false, overflow_ = false, dirty_ = false, quiet_ = false, listing_ = false, deleting_ = false, erasing_ = false, present_ = false;
    inline static const uint8_t zero_[64] = {};
    bool allocation_failed_ = false;
    const GlobalSettings::Values *global_values() const { return globals_ ? &globals_->values() : nullptr; }
    bool global_error(const char *message) { std::snprintf(last_error_, sizeof(last_error_), "%s", message); return false; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_validate_globals)
#endif
    bool validate_globals(const GlobalSettings::Values &values) {
        char cursor[64] = {}, name[64]; MemoryBuffer bytes;
        while (storage_.next(cursor, name)) {
            std::snprintf(cursor, sizeof(cursor), "%s", name); storage_.choose(name);
            int best = -1; uint32_t generation = 0;
            for (unsigned slot = 0; slot < 2; ++slot) {
                uint8_t header[96], trailer[4];
                if (!storage_.read(slot, 0, header, sizeof(header)) || !header_valid(header)
                    || name_compare(name, reinterpret_cast<const char *>(header + 20))) continue;
                unsigned length = u32(header + 8);
                if (storage_.size(slot) != length + 100 || !storage_.read(slot, 96 + length, trailer, 4)
                    || std::memcmp(trailer, "DONE", 4)) continue;
                if (!bytes.resize(length)) { storage_.choose(selected_); return global_error("global-memory"); }
                if (length && (!storage_.read(slot, 96, bytes.data, length) || checksum(bytes.data, length) != u32(header + 12))) continue;
                Document raw;
                if (length && !raw.load(bytes.data, length, hardware_, nullptr)) continue;
                if (best < 0 || int32_t(u32(header + 4) - generation) > 0) { best = int(slot); generation = u32(header + 4); }
            }
            if (best < 0) { storage_.choose(selected_); return global_error("global-invalid-setup"); }
            uint8_t header[96];
            if (!storage_.read(unsigned(best), 0, header, sizeof(header))) { storage_.choose(selected_); return global_error("global-read"); }
            unsigned length = u32(header + 8);
            if (!bytes.resize(length) || (length && !storage_.read(unsigned(best), 96, bytes.data, length))) { storage_.choose(selected_); return global_error("global-read"); }
            Document candidate;
            if (length && !candidate.load(bytes.data, length, hardware_, &values)) { storage_.choose(selected_); return global_error("global-invalid-setup"); }
        }
        storage_.choose(selected_); return true;
    }
    bool allocate(MemoryBuffer &buffer, unsigned length) {
        allocation_failed_ = !buffer.resize(length);
        return !allocation_failed_;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_begin_history)
#endif
    bool begin_history(HistoryAction action, const char *name, uint64_t number, const char *label = "") {
        if (busy() || (dirty_ && action != HistoryAction::archive) || !setup_name(name) || std::strlen(label) > 63) return false;
        if (action != HistoryAction::archive) quiet_ = true;
        history_progress_ = last_byte_;
        history_action_ = action; history_number_ = number; history_reply_ = false;
        last_error_[0] = 0; history_revision_ = Revision(); comparison_ = HistoryComparison();
        std::snprintf(history_name_, sizeof(history_name_), "%s", name);
        std::snprintf(history_label_, sizeof(history_label_), "%s", label);
        if (action == HistoryAction::list) {
            history_phase_ = HistoryPhase::revision; revisions_.next(name, number, staging_);
        } else if (action == HistoryAction::download) {
            history_phase_ = HistoryPhase::revision; revisions_.read(name, number, staging_);
        } else if (action == HistoryAction::erase) {
            history_phase_ = HistoryPhase::erase; revisions_.erase(name, number);
        } else {
            current_length_ = current_generation_ = history_slot_ = 0; current_slot_ = 1;
            history_phase_ = HistoryPhase::current; revisions_.current(name, 0, scratch_);
        }
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_history_complete)
#endif
    void history_complete(bool good) {
        HistoryAction action = history_action_; history_action_ = HistoryAction::none;
        if (!good) { error("history-failed"); return; }
        quiet_ = false;
        if (!history_reply_) return;
        if (action == HistoryAction::list) {
            if (!history_revision_.number) { replies_.reply("CNTRL END"); return; }
            char response[256], label[127], number[21]; hex_text(history_revision_.label, label);
            revision_text(history_revision_.number, number);
            std::snprintf(response, sizeof(response), "CNTRL REVISION %s %lu %u %s",
                number, (unsigned long)history_revision_.modified,
                unsigned(history_revision_.length), history_revision_.label[0] ? label : "-");
            replies_.reply(response);
        } else if (action == HistoryAction::download) {
            length_ = history_revision_.length; download_ = 1; char response[80];
            std::snprintf(response, sizeof(response), "CNTRL FILE %u %08lx", length_, (unsigned long)checksum(staging_.data, length_));
            replies_.reply(response);
        } else replies_.reply("CNTRL OK");
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_archive_ready)
#endif
    void archive_ready(uint32_t now) {
        if (history_action_ == HistoryAction::save) {
            if (!current_length_) { history_complete(false); return; }
        } else {
            generation_ = current_generation_; slot_ = 1 - current_slot_;
            std::snprintf(record_name_, sizeof(record_name_), "%s", history_name_);
            if (!current_length_ || (current_length_ == expected_ && !std::memcmp(staging_.data, scratch_.data, expected_))) {
                history_action_ = HistoryAction::none; begin_write(now); return;
            }
        }
        history_phase_ = HistoryPhase::append;
        revisions_.append(history_name_, history_label_, epoch_, scratch_, current_length_);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_history_tick)
#endif
    void history_tick(uint32_t now) {
        revisions_.tick();
        if (revisions_.memory_failed()) { history_complete(false); return; }
        if (revisions_.busy()) {
            if (!quiet_ && uint32_t(now - history_progress_) >= 500) {
                replies_.reply("CNTRL WAIT"); history_progress_ = now;
            }
            return;
        }
        if (history_phase_ == HistoryPhase::current) {
            const Revision &info = revisions_.info(); Document candidate;
            if (revisions_.success() && candidate.load(scratch_.data, info.length, hardware_, global_values())
                && (!current_length_ || int32_t(uint32_t(info.number) - current_generation_) > 0)) {
                current_generation_ = uint32_t(info.number); current_length_ = info.length; current_slot_ = history_slot_;
            }
            if (!history_slot_++) { revisions_.current(history_name_, 1, scratch_); return; }
            if (current_length_) {
                history_phase_ = HistoryPhase::selected;
                revisions_.current(history_name_, current_slot_, scratch_); return;
            }
            current_ready(now);
        } else if (history_phase_ == HistoryPhase::selected) {
            Document candidate; const auto &info = revisions_.info();
            if (!revisions_.success() || info.length != current_length_ || info.number != current_generation_
                || !candidate.load(scratch_.data, info.length, hardware_, global_values())) { history_complete(false); return; }
            current_ready(now);
        } else if (history_phase_ == HistoryPhase::revision) {
            if (!revisions_.success()) {
                if (history_action_ == HistoryAction::list) { history_revision_ = Revision(); history_complete(true); }
                else history_complete(false);
                return;
            }
            history_revision_ = revisions_.info();
            if (history_action_ == HistoryAction::restore || history_action_ == HistoryAction::compare) {
                Document original;
                if (!original.load(staging_.data, history_revision_.length, hardware_, global_values())
                    || !revisions_.remap(staging_, original)) { history_complete(false); return; }
            }
            if (history_action_ == HistoryAction::restore) {
                Document candidate;
                if (!candidate.load(staging_.data, history_revision_.length, hardware_, global_values()) || !candidate.prepare_fonts(hardware_)) { history_complete(false); return; }
                hardware_.finish_fonts(false);
                expected_ = history_revision_.length; expected_crc_ = checksum(staging_.data, expected_);
                archive_ready(now);
            } else if (history_action_ == HistoryAction::compare) {
                if (!compare_history()) { history_complete(false); return; }
                history_complete(true);
            } else history_complete(true);
        } else if (history_phase_ == HistoryPhase::append) {
            if (!revisions_.success()) { history_complete(false); return; }
            if (history_action_ == HistoryAction::save) { history_revision_ = revisions_.info(); history_complete(true); }
            else { history_action_ = HistoryAction::none; begin_write(now); }
        } else history_complete(revisions_.success());
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_compare_history)
#endif
    bool compare_history() {
        Document old, current;
        if (!old.load(staging_.data, history_revision_.length, hardware_) || !current.load(scratch_.data, current_length_, hardware_)) return false;
        unsigned &patches = comparison_.patches, &routes = comparison_.routes;
        bool &settings = comparison_.settings, &formatting = comparison_.formatting;
        patches = routes = 0;
        for (unsigned i = 0; i < old.patches() || i < current.patches(); ++i)
            if (i >= old.patches() || i >= current.patches() || std::memcmp(old.patch(i), current.patch(i), 76)) ++patches;
        unsigned changed_nodes = 0;
        for (unsigned i = 0; i < old.nodes() || i < current.nodes(); ++i) {
            if (i >= old.nodes() || i >= current.nodes()) { ++changed_nodes; continue; }
            const uint8_t *a = old.node(i), *b = current.node(i);
            if (u16(a + 2) != u16(b + 2) || std::memcmp(a, b, 4 + u16(a + 2))) ++changed_nodes;
        }
        if (changed_nodes > patches) patches = changed_nodes;
        for (unsigned i = 0; i < old.switches() || i < current.switches(); ++i)
            if (i >= old.switches() || i >= current.switches()
                || std::memcmp(old.switch_assignment(i), current.switch_assignment(i), 70)) ++patches;
        for (unsigned i = 0; i < old.named_patches() || i < current.named_patches(); ++i)
            if (i >= old.named_patches() || i >= current.named_patches()
                || std::memcmp(old.named_patch(i), current.named_patch(i), 66)) ++patches;
        for (unsigned i = 0; i < old.instruments() || i < current.instruments(); ++i)
            if (i >= old.instruments() || i >= current.instruments() || std::memcmp(old.instrument(i), current.instrument(i), 68)) ++routes;
        settings = old.page_section_size() != current.page_section_size()
            || std::memcmp(old.bank(old.patches()), current.bank(current.patches()), old.page_section_size())
            || std::memcmp(old.trs_modes(), current.trs_modes(), 4) || old.navigation_hold_ms() != current.navigation_hold_ms()
            || old.navigation_repeat_ms() != current.navigation_repeat_ms() || old.brightness() != current.brightness()
            || old.splash() != current.splash() || old.is_midi_interface() != current.is_midi_interface()
            || old.usb_device_midi() != current.usb_device_midi() || old.usb_host_midi() != current.usb_host_midi()
            || old.animation_color_cycle() != current.animation_color_cycle()
            || old.frame_buffers() != current.frame_buffers() || old.use_animations() != current.use_animations()
            || old.fast_debounce() != current.fast_debounce()
            || old.forward_device_to_host() != current.forward_device_to_host()
            || old.page_ram2() != current.page_ram2()
            || old.screensaver_timeout() != current.screensaver_timeout()
            || old.scroll_ms() != current.scroll_ms()
            || std::memcmp(old.data() + old.metadata() + scrolling_offset, current.data() + current.metadata() + scrolling_offset, 5);
        for (unsigned i = 0; i < 4; ++i) settings = settings || old.auxiliary_combined(i) != current.auxiliary_combined(i);
        auto names_end = [](const Document &d) {
            unsigned cursor = unsigned(d.bank(d.patches()) - d.data()) + d.page_section_size() + 4;
            for (unsigned i = 0; i < d.patches() * 3; ++i) { unsigned n = u16(d.data() + cursor); cursor += 2 + (n == 65535 ? 0 : n); }
            return cursor;
        };
        unsigned a = u32(old.data() + old.display_metadata() + 12), b = u32(current.data() + current.display_metadata() + 12);
        formatting = a != b || std::memcmp(old.data() + old.core_offset() + old.core_length() - a, current.data() + current.core_offset() + current.core_length() - b, a);
        unsigned old_names = names_end(old) - old.metadata(), current_names = names_end(current) - current.metadata();
        formatting = formatting || old_names != current_names
            || std::memcmp(old.data() + old.metadata(), current.data() + current.metadata(), old_names);
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_current_ready)
#endif
    void current_ready(uint32_t now) {
        if (history_action_ == HistoryAction::restore || history_action_ == HistoryAction::compare) {
            history_phase_ = HistoryPhase::revision; revisions_.read(history_name_, history_number_, staging_);
        } else archive_ready(now);
    }
    void error(const char *message) {
        if (allocation_failed_) { message = "document-memory"; allocation_failed_ = false; }
        std::snprintf(last_error_, sizeof(last_error_), "%s", message);
        char response[128]; std::snprintf(response, sizeof(response), "CNTRL ERROR %s", message);
        if (quiet_) { hardware_.error(message); quiet_ = false; } else replies_.reply(response);
    }
    void empty() {
        document_ = Document(); controller_.deactivate();
        if (!hardware_.relays.reset()) hardware_.error(ui::i18n::text(ui::i18n::Message::relay_reset_failed));
        hardware_.configure_pages(document_);
        active_.clear();
        constexpr uint8_t off[4] = {}; hardware_.configure_trs_ports(off);
        hardware_.begin_fonts(0); hardware_.finish_fonts(true);
        hardware_.set_movies(nullptr, 0, nullptr, 0);
        hardware_.set_brightness(192); hardware_.set_splash(false, 0);
        hardware_.set_midi_interface(false);
        hardware_.set_animation_color_cycle(false);
        Pane view; view.lines[1].centered = true;
        view.lines[1].graphics.line_height = view.lines[1].graphics.size = 16;
        std::snprintf(view.lines[1].text, sizeof(view.lines[1].text), "%s", ui::i18n::text(ui::i18n::Message::no_setups));
        view.lines[1].graphics.font = 253;
        for (unsigned i = 0; i < 3; ++i) hardware_.show(i, view);
    }
    void activate(const Document &candidate) {
        boot_error_[0] = 0;
        active_.swap(staging_);
        document_ = candidate; controller_.activate(document_);
        staging_.clear();
    }
    bool prepare_resources(const Document &document) {
        if (!document.prepare_fonts(hardware_)) return false;
        if (hardware_.prepare_pages(document)) return true;
        finish_resources(false); return false;
    }
    void finish_resources(bool commit) {
        hardware_.finish_pages(commit);
        hardware_.finish_fonts(commit);
    }
    bool header_valid(const uint8_t *h) const {
        return !std::memcmp(h, "CNST", 4) && checksum(h, 92) == u32(h + 92)
            && u32(h + 8) <= memory_budget && text_valid(h + 20, 64)
            && setup_name(reinterpret_cast<const char *>(h + 20)) && !u32(h + 84) && !u32(h + 88);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_read_slot)
#endif
    bool read_slot(unsigned slot) {
        uint8_t trailer[4];
        if (!storage_.read(slot, 0, header_, 96) || !header_valid(header_)) return false;
        unsigned length = u32(header_ + 8);
        if (!allocate(staging_, length)) return false;
        if (storage_.size(slot) != length + 100 || !storage_.read(slot, 96 + length, trailer, 4)
            || std::memcmp(trailer, "DONE", 4) || (length && !storage_.read(slot, 96, staging_.data, length))
            || checksum(staging_.data, length) != u32(header_ + 12)) return false;
        Document candidate; return !length || candidate.load(staging_.data, length, hardware_, global_values());
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_load)
#endif
    bool load(const char *name) {
        allocation_failed_ = false;
        storage_.choose(name); int best = -1; uint32_t gen = 0;
        present_ = storage_.size(0) || storage_.size(1);
        for (unsigned i = 0; i < 2; ++i) {
            if (read_slot(i) && !name_compare(name, reinterpret_cast<const char *>(header_ + 20))
                && (best < 0 || int32_t(u32(header_ + 4) - gen) > 0)) { best = int(i); gen = u32(header_ + 4); }
            if (allocation_failed_) return false;
        }
        generation_ = gen; slot_ = best == 0 ? 1 : 0; length_ = 0;
        if (best < 0 || !read_slot(unsigned(best))) return false;
        length_ = u32(header_ + 8); modified_ = u32(header_ + 16);
        std::snprintf(record_name_, sizeof(record_name_), "%s", header_ + 20);
        return length_ != 0;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_read_selection)
#endif
    int read_selection() {
        storage_.choose(""); uint8_t bytes[84]; bool found = false;
        bool present = storage_.size(0) || storage_.size(1);
        for (unsigned i = 0; i < 2; ++i) {
            if (storage_.size(i) != sizeof(bytes) || !storage_.read(i, 0, bytes, sizeof(bytes))
                || std::memcmp(bytes, "CSEL", 4) || std::memcmp(bytes + 80, "DONE", 4)
                || u32(bytes + 76) != checksum(bytes, 76) || u32(bytes + 72)
                || (bytes[8] ? !text_valid(bytes + 8, 64) || !setup_name(reinterpret_cast<const char *>(bytes + 8)) : std::memcmp(bytes + 8, zero_, 64))) continue;
            if (!found || int32_t(u32(bytes + 4) - selection_generation_) > 0) {
                found = true; selection_slot_ = int(i); selection_generation_ = u32(bytes + 4);
                std::snprintf(selected_, sizeof(selected_), "%.63s", bytes + 8);
            }
        }
        return found ? 1 : present ? -1 : 0;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_persist_selection)
#endif
    bool persist_selection(const char *name) {
        uint8_t bytes[84] = {}, verify[84];
        std::memcpy(bytes, "CSEL", 4); put32(bytes + 4, selection_generation_ + 1);
        std::snprintf(reinterpret_cast<char *>(bytes + 8), 64, "%s", name);
        put32(bytes + 76, checksum(bytes, 76)); std::memcpy(bytes + 80, "DONE", 4);
        storage_.choose(""); unsigned slot = selection_slot_ == 0 ? 1 : 0;
        if (!storage_.begin(slot)) return false;
        bool good = storage_.write(bytes, sizeof(bytes)); good = storage_.finish() && good;
        if (!good || storage_.size(slot) != sizeof(bytes) || !storage_.read(slot, 0, verify, sizeof(verify)) || std::memcmp(bytes, verify, sizeof(bytes))) return false;
        selection_slot_ = int(slot); ++selection_generation_; return true;
    }
    bool start_write(uint32_t now) {
        if (!expected_) return begin_write(now);
        return begin_history(HistoryAction::archive, record_name_, 0);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_begin_write)
#endif
    bool begin_write(uint32_t now) {
        Document prepared;
        if (expected_ && !prepared.load(staging_.data, expected_, hardware_, global_values())) return false;
        storage_.prepare_document(prepared);
        if (dirty_ && !allocate(scratch_, expected_)) { error("document-memory"); return false; }
        last_error_[0] = 0;
        std::memset(header_, 0, sizeof(header_)); std::memcpy(header_, "CNST", 4);
        put32(header_ + 4, generation_ + 1); put32(header_ + 8, expected_); put32(header_ + 12, expected_crc_);
        put32(header_ + 16, epoch_ ? epoch_ + uint32_t(now - clock_) / 1000 : 0);
        std::snprintf(reinterpret_cast<char *>(header_ + 20), 64, "%s", record_name_);
        put32(header_ + 92, checksum(header_, 92)); storage_.choose(record_name_);
        if (!storage_.begin(slot_)) { error("storage-unavailable"); return false; }
        if (!storage_.write(header_, 96)) { storage_.finish(); error("storage-write"); return false; }
        written_ = 0; writing_ = true; return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_erase_batch)
#endif
    void erase_batch() {
        if (!expected_ || staging_.data[expected_ - 1]) { error("invalid-delete-list"); return; }
        if (!allocate(scratch_, expected_)) { error("document-memory"); return; }
        unsigned names_length = expected_; std::memcpy(scratch_.data, staging_.data, names_length);
        for (unsigned i = 0; i < names_length;) {
            const char *n = reinterpret_cast<const char *>(scratch_.data + i);
            if (!setup_name(n) || !load(n)) { error("missing-setup"); return; }
            for (unsigned j = 0; j < i; j += unsigned(std::strlen(reinterpret_cast<const char *>(scratch_.data + j))) + 1)
                if (!name_compare(n, reinterpret_cast<const char *>(scratch_.data + j))) { error("duplicate-setup"); return; }
            i += unsigned(std::strlen(n)) + 1;
        }
        auto deleting = [&](const char *name) {
            for (unsigned i = 0; i < names_length; i += unsigned(std::strlen(reinterpret_cast<const char *>(scratch_.data + i))) + 1)
                if (!name_compare(name, reinterpret_cast<const char *>(scratch_.data + i))) return true;
            return false;
        };
        if (deleting(selected_)) {
            char cursor[64] = {}, name[64] = {}; bool found = false;
            while (next_valid(cursor, name)) {
                if (!deleting(name)) { found = true; break; }
                std::snprintf(cursor, sizeof(cursor), "%s", name);
            }
            if (found) { if (!select(name)) { error("selection-write"); return; } }
            else {
                if (!persist_selection("")) { error("selection-write"); return; }
                selected_[0] = 0; empty();
            }
        }
        delete_length_ = names_length; delete_offset_ = 0; erasing_ = true;
    }
    void erase_step(uint32_t now) {
        erasing_ = false;
        const char *name = reinterpret_cast<const char *>(scratch_.data + delete_offset_);
        if (!load(name)) { error("missing-setup"); return; }
        expected_ = 0; expected_crc_ = checksum(staging_.data, 0);
        if (!start_write(now)) return;
        bool ok = storage_.write(reinterpret_cast<const uint8_t *>("DONE"), 4);
        ok = storage_.finish() && ok; writing_ = false;
        unsigned tombstone = slot_;
        if (!ok || !read_slot(tombstone) || u32(header_ + 8)) { error("delete-verify"); return; }
        delete_offset_ += unsigned(std::strlen(name)) + 1;
        if (delete_offset_ < delete_length_) erasing_ = true;
        else replies_.reply("CNTRL OK");
    }
    void received(uint32_t now) {
        if (checksum(staging_.data, expected_) != expected_crc_) { error("checksum"); return; }
        if (deleting_) { erase_batch(); return; }
        Document candidate;
        if (!candidate.load(staging_.data, expected_, hardware_, global_values())) { error("invalid-document"); return; }
        if (!candidate.prepare_fonts(hardware_)) { error("font-load"); return; }
        hardware_.finish_fonts(false);
        start_write(now);
    }
    void listing() {
        char name[64], hex[127], response[224];
        length_ = 0;
        while (storage_.next(cursor_, name)) {
            std::snprintf(cursor_, sizeof(cursor_), "%s", name);
            if (load(name)) break;
            if (present_ && !generation_) {
                hex_text(name, hex); std::snprintf(response, sizeof(response), "CNTRL INVALID %s", hex); replies_.reply(response); return;
            }
        }
        if (!length_) { listing_ = false; replies_.reply("CNTRL END"); return; }
        hex_text(record_name_, hex);
        std::snprintf(response, sizeof(response), "CNTRL ENTRY %s %lu %u %08lx", hex,
            (unsigned long)modified_, length_, (unsigned long)checksum(staging_.data, length_));
        replies_.reply(response);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(documents_command)
#endif
    void command(uint32_t now) {
        char hex[127] = {}, name[64] = {}; unsigned length = 0, overwrite = 0; unsigned long crc = 0, epoch = 0; int end = 0;
        if (!std::strcmp(line_, "CNTRL FORMAT")) {
            char response[64]; std::snprintf(response, sizeof(response), "CNTRL FORMAT %u %u %u %u", binary::development_format,
                unsigned(binary::Kind::setup), unsigned(binary::Kind::bundle), unsigned(binary::Kind::songs));
            replies_.reply(response); return;
        }
        if (!std::strcmp(line_, "CNTRL STATUS")) {
            if (boot_error_[0]) { error(boot_error_); return; }
            char response[112]; std::snprintf(response, sizeof(response), "CNTRL READY %u %u %08lx%s", document_version, document_.length(),
                document_.valid() ? (unsigned long)checksum(active_.data, document_.length()) : 0ul, globals_ && globals_->ready() ? " device_globals=1 language=1 name_and_contact=1" : "");
            replies_.reply(response); return;
        }
        if (!std::strcmp(line_, "CNTRL SETUP CURRENT")) {
            if (!std::strcmp(boot_error_, "selection-record-invalid")) { error(boot_error_); return; }
            char response[160]; hex_text(selected_, hex); std::snprintf(response, sizeof(response), "CNTRL CURRENT %s", hex);
            replies_.reply(response); return;
        }
        if (std::sscanf(line_, "CNTRL SETUP SET %126s%n", hex, &end) == 1 && end && !line_[end] && unhex_name(hex, name)) {
            if (dirty_) { error("unsaved-edits"); return; }
            if (!select(name)) error("selection-failed");
            else if (busy()) quiet_ = false;
            else replies_.reply("CNTRL OK");
            return;
        }
        if (!std::strcmp(line_, "CNTRL SETUP FORWARD") || !std::strcmp(line_, "CNTRL SETUP BACKWARD")) {
            if (dirty_) { error("unsaved-edits"); return; }
            if (!rotate(!std::strcmp(line_, "CNTRL SETUP BACKWARD"))) error("rotation-failed");
            return;
        }
        if (!storage_.available()) { error("storage-unavailable"); return; }
        if (!std::strcmp(line_, "CNTRL SETUP LIST")) { if (dirty_) { error("unsaved-edits"); return; } download_ = 0; cursor_[0] = 0; listing_ = true; listing(); return; }
        if (!std::strcmp(line_, "CNTRL NEXT")) {
            if (listing_) { listing(); return; }
            if (download_) {
                unsigned n = length_ - (download_ - 1); if (n > usb_block_size) n = usb_block_size;
                replies_.data(staging_.data + download_ - 1, n);
                download_ += n; if (download_ - 1 == length_) download_ = 0;
                return;
            }
            error("no-transfer"); return;
        }
        if (hardware_.configuration_locked()) { error("configuration-locked"); return; }
        if (dirty_) { error("unsaved-edits"); return; }
        listing_ = false; download_ = 0;
        uint64_t revision = 0; char number[21] = {}, operation[16] = {}, label_hex[127] = {}, label[64] = {};
        if (std::sscanf(line_, "CNTRL HISTORY %15s %126s %20s%n", operation, hex, number, &end) == 3
            && unhex_name(hex, name) && parse_revision(number, revision)) {
            bool accepted = false;
            if (!std::strcmp(operation, "SAVE")) {
                int finish = 0;
                if (!std::strcmp(line_ + end, " -")) label[0] = 0;
                else if (std::sscanf(line_ + end, " %126s%n", label_hex, &finish) != 1 || line_[end + finish]
                    || !unhex_name(label_hex, label)) { error("invalid-label"); return; }
                accepted = history_save(name, label);
            } else {
                if (line_[end]) { error("invalid-command"); return; }
                if (!std::strcmp(operation, "LIST")) accepted = history_next(name, revision);
                else if (!std::strcmp(operation, "GET")) accepted = begin_history(HistoryAction::download, name, revision);
                else if (!std::strcmp(operation, "RESTORE")) { accepted = history_restore(name, revision, now); quiet_ = false; }
                else if (!std::strcmp(operation, "DELETE")) accepted = history_delete(name, revision);
            }
            if (!accepted) error("history-busy"); else { history_reply_ = true; quiet_ = false; }
            return;
        }
        if (std::sscanf(line_, "CNTRL TIME %lu%n", &epoch, &end) == 1 && end && !line_[end] && epoch && epoch <= 0xfffffffful) { epoch_ = uint32_t(epoch); clock_ = now; replies_.reply("CNTRL OK"); return; }
        if (!std::strcmp(line_, "CNTRL SETUP FINISH")) {
            char first[64]; if (!selected_[0] && next_valid("", first) && !select(first)) { error("selection-failed"); return; }
            replies_.reply("CNTRL OK"); return;
        }
        if (std::sscanf(line_, "CNTRL SETUP GET %126s%n", hex, &end) == 1 && end && !line_[end] && unhex_name(hex, name)) {
            if (!load(name)) { error("missing-setup"); return; }
            char response[80]; std::snprintf(response, sizeof(response), "CNTRL FILE %u %08lx", length_, (unsigned long)checksum(staging_.data, length_));
            download_ = 1; replies_.reply(response); return;
        }
        if (std::sscanf(line_, "CNTRL SETUP PUT %126s %u %lx %u%n", hex, &length, &crc, &overwrite, &end) == 4
            && end && !line_[end] && unhex_name(hex, name) && overwrite <= 1 && length >= 116 && length <= memory_budget && crc <= 0xfffffffful) {
            bool exists = load(name);
            if (allocation_failed_) { error("document-memory"); return; }
            if ((exists && (!overwrite || std::strcmp(record_name_, name))) || (present_ && !generation_ && !overwrite)) { error("setup-exists"); return; }
            if (!allocate(staging_, length)) { error("document-memory"); return; }
            std::snprintf(record_name_, sizeof(record_name_), "%s", name);
            expected_ = length; expected_crc_ = uint32_t(crc); received_ = 0; deleting_ = false;
            receiving_ = true; replies_.reply("CNTRL SEND"); return;
        }
        if (std::sscanf(line_, "CNTRL SETUP DELETE %u %lx%n", &length, &crc, &end) == 2
            && end && !line_[end] && length && length <= memory_budget && crc <= 0xfffffffful) {
            if (!allocate(staging_, length)) { error("document-memory"); return; }
            expected_ = length; expected_crc_ = uint32_t(crc); received_ = 0; deleting_ = true;
            receiving_ = true; replies_.reply("CNTRL SEND"); return;
        }
        error("invalid-command");
    }
};
}
