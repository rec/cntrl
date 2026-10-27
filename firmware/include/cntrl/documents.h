#pragma once
#include "controller.h"
#include "setup_names.h"
#include "revisions.h"
#include <cstdlib>

namespace cntrl {
struct Replies {
    virtual ~Replies() = default;
    virtual void reply(const char *text) = 0;
};

class Documents {
public:
    Documents(SetupStorage &storage, Hardware &hardware, Controller &controller, Replies &replies)
        : storage_(storage), hardware_(hardware), controller_(controller), replies_(replies) {}
    void boot() {
        empty(true);
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
    bool busy() const { return receiving_ || writing_ || erasing_ || download_ || listing_; }
    bool dirty() const { return dirty_; }
    const char *last_error() const { return last_error_; }
    bool next_valid(const char *after, char *name) {
        char cursor[64]; std::snprintf(cursor, sizeof(cursor), "%s", after);
        while (storage_.next(cursor, name)) {
            std::snprintf(cursor, sizeof(cursor), "%s", name);
            if (load(name)) { std::snprintf(name, 64, "%s", record_name_); return true; }
        }
        return false;
    }
    bool select(const char *name) {
        if (busy() || dirty_ || !hardware_.output_idle() || !setup_name(name) || !load(name)) return false;
        Document candidate;
        if (!candidate.load(staging_, length_, hardware_) || !candidate.prepare_fonts(hardware_)) return false;
        char canonical[64]; std::snprintf(canonical, sizeof(canonical), "%s", record_name_);
        if (std::strcmp(selected_, canonical) && !persist_selection(canonical)) { hardware_.finish_fonts(false); return false; }
        hardware_.finish_fonts(true);
        std::snprintf(selected_, sizeof(selected_), "%s", canonical);
        activate(); return true;
    }
    bool rotate(bool backward) {
        if (busy() || dirty_) return false;
        char cursor[64] = {}, name[64], first[64] = {}, last[64] = {}, before[64] = {}, after[64] = {};
        unsigned count = 0;
        while (next_valid(cursor, name)) {
            if (!count++) std::snprintf(first, sizeof(first), "%s", name);
            if (name_compare(name, selected_) < 0) std::snprintf(before, sizeof(before), "%s", name);
            if (!after[0] && name_compare(name, selected_) > 0) std::snprintf(after, sizeof(after), "%s", name);
            std::snprintf(last, sizeof(last), "%s", name);
            std::snprintf(cursor, sizeof(cursor), "%s", name);
        }
        return count > 1 && select(backward ? (before[0] ? before : last) : (after[0] ? after : first));
    }
    void redraw() { if (document_.valid()) controller_.refresh(); else empty(); }
    bool edit_begin() {
        if (busy() || !document_.valid()) return false;
        std::memcpy(staging_, active_, document_.length()); dirty_ = true; return true;
    }
    int64_t edit_value(unsigned patch, unsigned field) const {
        Document d; if (!dirty_ || !d.load(staging_, document_.length(), hardware_) || patch >= d.patches()) return 0;
        const uint8_t *p = d.patch(patch);
        switch (field) {
        case 0: return p[4] + 1;
        case 1: return u32(d.bank(patch)) == 0xffffffffu ? int64_t(-1) : int64_t(u32(d.bank(patch)));
        case 2: return d.instrument(u16(p))[64] + 1;
        case 3: return p[7] == 255 ? -1 : p[7];
        case 4: return p[8] == 255 ? -65 : int(p[8]) - 64;
        case 5: return d.brightness();
        case 7: return d.usb_midi_interface();
        default: return int(d.screensaver_timeout() / 1000);
        }
    }
    bool edit_adjust(unsigned patch, unsigned field, int delta) {
        Document d;
        if (!dirty_ || !d.load(staging_, document_.length(), hardware_) || patch >= d.patches() || field > 7) return false;
        uint8_t *p = staging_ + (d.patch(patch) - staging_);
        int64_t old = edit_value(patch, field), value = old + delta;
        if (field == 0 && value >= 1 && value <= 128) p[4] = uint8_t(value - 1);
        else if (field == 1) {
            uint32_t bank = 0xffffffffu; const uint8_t *mapping = nullptr;
            for (unsigned i = 0; i < d.mappings(); ++i) {
                const uint8_t *m = d.mapping(i); uint32_t candidate = u32(m + 2);
                if (u16(m) != u16(p)) continue;
                if (delta > 0 && (old < 0 || candidate > unsigned(old)) && (!mapping || candidate < bank)) { bank = candidate; mapping = m; }
                if (delta < 0 && old >= 0 && candidate < unsigned(old) && (!mapping || candidate > bank)) { bank = candidate; mapping = m; }
            }
            if ((delta > 0 && !mapping) || (delta < 0 && old < 0)) return false;
            p[5] = mapping ? mapping[6] : 255; p[6] = mapping ? mapping[7] : 255;
            put32(staging_ + (d.bank(patch) - staging_), bank);
        } else if (field == 2 && value >= 1 && value <= 16) staging_[d.instrument(u16(p)) - staging_ + 64] = uint8_t(value - 1);
        else if (field == 3 && value >= -1 && value <= 127) p[7] = value < 0 ? 255 : uint8_t(value);
        else if (field == 4 && value >= -65 && value <= 63) p[8] = value == -65 ? 255 : uint8_t(value + 64);
        else if (field == 5 && value >= 0 && value <= 255) staging_[18] = uint8_t(value);
        else if (field == 6 && value >= 0 && value <= 2147483) put32(staging_ + 24, unsigned(value) * 1000);
        else if (field == 7 && value >= 0 && value <= 1) staging_[19] = (staging_[19] & ~uint8_t(2)) | (uint8_t(value) << 1);
        else return false;
        put32(staging_ + 20, checksum(staging_ + 28, document_.length() - 28)); return true;
    }
    void edit_cancel() { dirty_ = false; }
    bool edit_save(uint32_t now) {
        if (!dirty_ || busy()) return false;
        // Reuse transfer scratch to preserve edits while verifying BOTH slots.
        // A checksum-correct header alone does not establish a committed slot.
        char name[64]; std::snprintf(name, sizeof(name), "%s", selected_);
        std::memcpy(delete_names_, staging_, document_.length());
        if (!load(name)) return false;
        std::memcpy(staging_, delete_names_, document_.length());
        std::snprintf(record_name_, sizeof(record_name_), "%s", name);
        expected_ = document_.length(); expected_crc_ = checksum(staging_, expected_); quiet_ = true;
        if (!start_write(now)) return false;
        dirty_ = false; return true;
    }
    void receive(uint8_t byte, uint32_t now) {
        if (writing_ || erasing_) return;
        last_byte_ = now;
        if (receiving_) {
            staging_[received_++] = byte;
            if (received_ == expected_) { receiving_ = false; received(now); }
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
        if (erasing_) { erase_step(now); return; }
        if (!writing_) return;
        unsigned n = expected_ - written_; if (n > 512) n = 512;
        if (n && !storage_.write(staging_ + written_, n)) { storage_.finish(); writing_ = false; error("storage-write"); return; }
        written_ += n; if (written_ != expected_) return;
        if (!hardware_.output_idle()) return;
        bool done = storage_.write(reinterpret_cast<const uint8_t *>("DONE"), 4);
        done = storage_.finish() && done; writing_ = false;
        char name[64]; std::snprintf(name, sizeof(name), "%s", record_name_);
        uint32_t generation = generation_ + 1;
        if (!done || !load(name) || generation_ != generation || length_ != expected_ || checksum(staging_, length_) != expected_crc_) { error("storage-verify"); return; }
        if (!name_compare(name, selected_)) {
            Document candidate;
            if (!candidate.load(staging_, length_, hardware_) || !candidate.prepare_fonts(hardware_)) { error("font-load"); return; }
            hardware_.finish_fonts(true); activate();
        }
        if (!quiet_) replies_.reply("CNTRL OK");
        quiet_ = false;
    }
    bool history_next(const char *name, uint64_t before, Revision &info) {
        return !busy() && !dirty_ && Revisions(storage_).next(name, before, info, staging_);
    }
    bool history_save(const char *name, const char *label) {
        if (busy() || dirty_ || !load(name)) return false;
        return Revisions(storage_).append(name, label, epoch_, staging_, length_);
    }
    bool history_restore(const char *name, uint64_t number, uint32_t now) {
        if (busy() || dirty_ || !hardware_.output_idle()) return false;
        load(name); Revision info;
        if (!Revisions(storage_).read(name, number, info, staging_)) return false;
        Document candidate;
        if (!candidate.load(staging_, info.length, hardware_) || !candidate.prepare_fonts(hardware_)) return false;
        hardware_.finish_fonts(false);
        expected_ = info.length; expected_crc_ = checksum(staging_, expected_);
        std::snprintf(record_name_, sizeof(record_name_), "%s", name);
        quiet_ = true; return start_write(now);
    }
    bool history_compare(const char *name, uint64_t number, unsigned &patches, unsigned &routes, bool &settings, bool &formatting) {
        if (busy() || dirty_ || !load(name)) return false;
        unsigned current_length = length_; std::memcpy(delete_names_, staging_, length_);
        Revision info; if (!Revisions(storage_).read(name, number, info, staging_)) return false;
        Document old, current;
        if (!old.load(staging_, info.length, hardware_) || !current.load(delete_names_, current_length, hardware_)) return false;
        patches = routes = 0;
        for (unsigned i = 0; i < old.patches() || i < current.patches(); ++i)
            if (i >= old.patches() || i >= current.patches() || std::memcmp(old.patch(i), current.patch(i), 76)) ++patches;
        for (unsigned i = 0; i < old.instruments() || i < current.instruments(); ++i)
            if (i >= old.instruments() || i >= current.instruments() || std::memcmp(old.instrument(i), current.instrument(i), 68)) ++routes;
        settings = old.chord_ms() != current.chord_ms() || old.brightness() != current.brightness()
            || old.splash() != current.splash() || old.usb_midi_interface() != current.usb_midi_interface()
            || old.screensaver_timeout() != current.screensaver_timeout() || old.transition_ms() != current.transition_ms();
        auto names_end = [](const Document &d) {
            unsigned cursor = unsigned(d.bank(d.patches()) - d.data()) + 4;
            for (unsigned i = 0; i < d.patches() * 3; ++i) { unsigned n = u16(d.data() + cursor); cursor += 2 + (n == 65535 ? 0 : n); }
            return cursor;
        };
        unsigned a = u32(old.data() + names_end(old) + 12), b = u32(current.data() + names_end(current) + 12);
        formatting = a != b || std::memcmp(old.data() + old.length() - a, current.data() + current.length() - b, a);
        unsigned old_names = names_end(old) - old.metadata(), current_names = names_end(current) - current.metadata();
        formatting = formatting || old_names != current_names
            || std::memcmp(old.data() + old.metadata(), current.data() + current.metadata(), old_names);
        return true;
    }
    bool history_delete(const char *name, uint64_t number) {
        if (busy() || dirty_) return false;
        if (number) return storage_.remove_revision(name, number);
        uint64_t before = UINT64_MAX;
        while (auto n = storage_.revision_before(name, before)) {
            if (!storage_.remove_revision(name, n)) return false;
            before = n;
        }
        return true;
    }
private:
    SetupStorage &storage_; Hardware &hardware_; Controller &controller_; Replies &replies_;
    uint8_t active_[max_document] = {}, staging_[max_document] = {}, header_[96] = {};
    // Deletion names survive staging-buffer reuse while loading candidate slots.
    uint8_t delete_names_[max_document] = {};
    Document document_;
    char selected_[64] = {}, record_name_[64] = {}, cursor_[64] = {}, line_[256] = {}, last_error_[64] = {}, boot_error_[64] = {};
    unsigned used_ = 0, received_ = 0, expected_ = 0, written_ = 0, slot_ = 0, length_ = 0, download_ = 0;
    unsigned delete_length_ = 0, delete_offset_ = 0;
    uint32_t expected_crc_ = 0, generation_ = 0, modified_ = 0, last_byte_ = 0, epoch_ = 0, clock_ = 0;
    uint32_t selection_generation_ = 0;
    int selection_slot_ = -1;
    bool receiving_ = false, writing_ = false, overflow_ = false, dirty_ = false, quiet_ = false, listing_ = false, deleting_ = false, erasing_ = false, present_ = false;
    inline static const uint8_t zero_[64] = {};
    void error(const char *message) {
        std::snprintf(last_error_, sizeof(last_error_), "%s", message);
        char response[128]; std::snprintf(response, sizeof(response), "CNTRL ERROR %s", message);
        if (quiet_) { hardware_.error(message); quiet_ = false; } else replies_.reply(response);
    }
    void empty(bool startup = false) {
        document_ = Document(); controller_.deactivate();
        hardware_.begin_fonts(); hardware_.finish_fonts(true);
        hardware_.set_brightness(192); hardware_.set_splash(startup, 0);
        hardware_.set_usb_midi_interface(false);
        Pane view; view.lines[1].centered = true;
        view.lines[1].graphics.size = view.lines[1].graphics.line_height = 16;
        std::snprintf(view.lines[1].text, sizeof(view.lines[1].text), "No setups");
        for (unsigned i = 0; i < 3; ++i) hardware_.show(i, view);
    }
    void activate() {
        boot_error_[0] = 0;
        std::memcpy(active_, staging_, length_);
        document_.load(active_, length_, hardware_); controller_.activate(document_);
    }
    bool header_valid(const uint8_t *h) const {
        return !std::memcmp(h, "CNST", 4) && checksum(h, 92) == u32(h + 92)
            && u32(h + 8) <= max_document && text_valid(h + 20, 64)
            && setup_name(reinterpret_cast<const char *>(h + 20)) && !u32(h + 84) && !u32(h + 88);
    }
    bool read_slot(unsigned slot) {
        uint8_t trailer[4];
        if (!storage_.read(slot, 0, header_, 96) || !header_valid(header_)) return false;
        unsigned length = u32(header_ + 8);
        if (storage_.size(slot) != length + 100 || !storage_.read(slot, 96 + length, trailer, 4)
            || std::memcmp(trailer, "DONE", 4) || (length && !storage_.read(slot, 96, staging_, length))
            || checksum(staging_, length) != u32(header_ + 12)) return false;
        Document candidate; return !length || candidate.load(staging_, length, hardware_);
    }
    bool load(const char *name) {
        storage_.choose(name); int best = -1; uint32_t gen = 0;
        present_ = storage_.size(0) || storage_.size(1);
        for (unsigned i = 0; i < 2; ++i)
            if (read_slot(i) && !name_compare(name, reinterpret_cast<const char *>(header_ + 20))
                && (best < 0 || int32_t(u32(header_ + 4) - gen) > 0)) { best = int(i); gen = u32(header_ + 4); }
        generation_ = gen; slot_ = best == 0 ? 1 : 0; length_ = 0;
        if (best < 0 || !read_slot(unsigned(best))) return false;
        length_ = u32(header_ + 8); modified_ = u32(header_ + 16);
        std::snprintf(record_name_, sizeof(record_name_), "%s", header_ + 20);
        return length_ != 0;
    }
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
        if (expected_) {
            std::memcpy(delete_names_, staging_, expected_);
            char name[64]; std::snprintf(name, sizeof(name), "%s", record_name_);
            bool exists = load(name);
            bool changed = exists && (length_ != expected_ || std::memcmp(staging_, delete_names_, expected_));
            bool archived = !changed || Revisions(storage_).append(name, "", epoch_, staging_, length_);
            std::memcpy(staging_, delete_names_, expected_);
            std::snprintf(record_name_, sizeof(record_name_), "%s", name);
            if (!archived) { error("history-write"); return false; }
        }

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
    void erase_batch() {
        if (!expected_ || staging_[expected_ - 1]) { error("invalid-delete-list"); return; }
        unsigned names_length = expected_; std::memcpy(delete_names_, staging_, names_length);
        for (unsigned i = 0; i < names_length;) {
            const char *n = reinterpret_cast<const char *>(delete_names_ + i);
            if (!setup_name(n) || !load(n)) { error("missing-setup"); return; }
            for (unsigned j = 0; j < i; j += unsigned(std::strlen(reinterpret_cast<const char *>(delete_names_ + j))) + 1)
                if (!name_compare(n, reinterpret_cast<const char *>(delete_names_ + j))) { error("duplicate-setup"); return; }
            i += unsigned(std::strlen(n)) + 1;
        }
        auto deleting = [&](const char *name) {
            for (unsigned i = 0; i < names_length; i += unsigned(std::strlen(reinterpret_cast<const char *>(delete_names_ + i))) + 1)
                if (!name_compare(name, reinterpret_cast<const char *>(delete_names_ + i))) return true;
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
        const char *name = reinterpret_cast<const char *>(delete_names_ + delete_offset_);
        load(name); expected_ = 0; expected_crc_ = checksum(staging_, 0);
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
        if (checksum(staging_, expected_) != expected_crc_) { error("checksum"); return; }
        if (deleting_) { erase_batch(); return; }
        Document candidate;
        if (!candidate.load(staging_, expected_, hardware_)) { error("invalid-document"); return; }
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
            (unsigned long)modified_, length_, (unsigned long)checksum(staging_, length_));
        replies_.reply(response);
    }
    void command(uint32_t now) {
        char hex[127] = {}, name[64] = {}; unsigned length = 0, overwrite = 0; unsigned long crc = 0, epoch = 0; int end = 0;
        if (!std::strcmp(line_, "CNTRL STATUS")) {
            if (boot_error_[0]) { error(boot_error_); return; }
            char response[80]; std::snprintf(response, sizeof(response), "CNTRL READY %u %u %08lx", document_version, document_.length(),
                document_.valid() ? (unsigned long)checksum(active_, document_.length()) : 0ul);
            replies_.reply(response); return;
        }
        if (!std::strcmp(line_, "CNTRL SETUP CURRENT")) {
            if (!std::strcmp(boot_error_, "selection-record-invalid")) { error(boot_error_); return; }
            char response[160]; hex_text(selected_, hex); std::snprintf(response, sizeof(response), "CNTRL CURRENT %s", hex);
            replies_.reply(response); return;
        }
        if (!storage_.available()) { error("storage-unavailable"); return; }
        if (!std::strcmp(line_, "CNTRL SETUP LIST")) { if (dirty_) { error("unsaved-edits"); return; } download_ = 0; cursor_[0] = 0; listing_ = true; listing(); return; }
        if (!std::strcmp(line_, "CNTRL NEXT")) {
            if (listing_) { listing(); return; }
            if (download_) {
                unsigned n = length_ - (download_ - 1); if (n > 128) n = 128;
                char response[280] = "CNTRL DATA ";
                for (unsigned i = 0; i < n; ++i) std::snprintf(response + 11 + i * 2, 3, "%02x", staging_[download_ - 1 + i]);
                download_ += n; if (download_ - 1 == length_) download_ = 0;
                replies_.reply(response); return;
            }
            error("no-transfer"); return;
        }
        if (dirty_) { error("unsaved-edits"); return; }
        listing_ = false; download_ = 0;
        unsigned long long revision = 0; char operation[16] = {}, label_hex[127] = {}, label[64] = {};
        if (std::sscanf(line_, "CNTRL HISTORY %15s %126s %llu%n", operation, hex, &revision, &end) == 3
            && unhex_name(hex, name)) {
            if (!std::strcmp(operation, "SAVE")) {
                int finish = 0;
                if (!std::strcmp(line_ + end, " -")) label[0] = 0;
                else if (std::sscanf(line_ + end, " %126s%n", label_hex, &finish) != 1 || line_[end + finish]
                    || !unhex_name(label_hex, label)) { error("invalid-label"); return; }
                if (history_save(name, label)) replies_.reply("CNTRL OK"); else error("history-write"); return;
            }
            if (line_[end]) { error("invalid-command"); return; }
            if (!std::strcmp(operation, "LIST")) {
                Revision info; char response[256];
                if (!history_next(name, revision, info)) { replies_.reply("CNTRL END"); return; }
                hex_text(info.label, label_hex);
                std::snprintf(response, sizeof(response), "CNTRL REVISION %llu %lu %u %s", (unsigned long long)info.number,
                    (unsigned long)info.modified, unsigned(info.length), info.label[0] ? label_hex : "-");
                replies_.reply(response); return;
            }
            if (!std::strcmp(operation, "GET")) {
                Revision info;
                if (!Revisions(storage_).read(name, revision, info, staging_)) { error("missing-revision"); return; }
                length_ = info.length; download_ = 1; char response[80];
                std::snprintf(response, sizeof(response), "CNTRL FILE %u %08lx", length_, (unsigned long)checksum(staging_, length_));
                replies_.reply(response); return;
            }
            if (!std::strcmp(operation, "RESTORE")) {
                if (history_restore(name, revision, now)) quiet_ = false; else error("restore-failed"); return;
            }
            if (!std::strcmp(operation, "DELETE")) {
                if (history_delete(name, revision)) replies_.reply("CNTRL OK"); else error("history-delete"); return;
            }
        }
        if (std::sscanf(line_, "CNTRL TIME %lu%n", &epoch, &end) == 1 && end && !line_[end] && epoch && epoch <= 0xfffffffful) { epoch_ = uint32_t(epoch); clock_ = now; replies_.reply("CNTRL OK"); return; }
        if (std::sscanf(line_, "CNTRL SETUP SET %126s%n", hex, &end) == 1 && end && !line_[end] && unhex_name(hex, name)) {
            if (select(name)) replies_.reply("CNTRL OK"); else error("selection-failed"); return;
        }
        if (!std::strcmp(line_, "CNTRL SETUP FORWARD") || !std::strcmp(line_, "CNTRL SETUP BACKWARD")) {
            if (rotate(!std::strcmp(line_, "CNTRL SETUP BACKWARD"))) replies_.reply("CNTRL OK"); else error("rotation-failed"); return;
        }
        if (!std::strcmp(line_, "CNTRL SETUP FINISH")) {
            char first[64]; if (!selected_[0] && next_valid("", first) && !select(first)) { error("selection-failed"); return; }
            replies_.reply("CNTRL OK"); return;
        }
        if (std::sscanf(line_, "CNTRL SETUP GET %126s%n", hex, &end) == 1 && end && !line_[end] && unhex_name(hex, name)) {
            if (!load(name)) { error("missing-setup"); return; }
            char response[80]; std::snprintf(response, sizeof(response), "CNTRL FILE %u %08lx", length_, (unsigned long)checksum(staging_, length_));
            download_ = 1; replies_.reply(response); return;
        }
        if (std::sscanf(line_, "CNTRL SETUP PUT %126s %u %lx %u%n", hex, &length, &crc, &overwrite, &end) == 4
            && end && !line_[end] && unhex_name(hex, name) && overwrite <= 1 && length >= 116 && length <= max_document && crc <= 0xfffffffful) {
            bool exists = load(name);
            if ((exists && (!overwrite || std::strcmp(record_name_, name))) || (present_ && !generation_ && !overwrite)) { error("setup-exists"); return; }
            std::snprintf(record_name_, sizeof(record_name_), "%s", name);
            expected_ = length; expected_crc_ = uint32_t(crc); received_ = 0; deleting_ = false;
            receiving_ = true; replies_.reply("CNTRL SEND"); return;
        }
        if (std::sscanf(line_, "CNTRL SETUP DELETE %u %lx%n", &length, &crc, &end) == 2
            && end && !line_[end] && length && length <= max_document && crc <= 0xfffffffful) {
            expected_ = length; expected_crc_ = uint32_t(crc); received_ = 0; deleting_ = true;
            receiving_ = true; replies_.reply("CNTRL SEND"); return;
        }
        error("invalid-command");
    }
};
}
