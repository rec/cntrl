#pragma once
#include "documents.h"
#include <algorithm>

namespace cntrl {
#pragma pack(push, 1)
struct SongPatch {
    char output[32] = {};
    uint8_t channel = 0, program = 0, msb = 255, lsb = 255, volume = 255, pan = 255;
    uint8_t function = 0, exit_buttons = 0;
};
struct SongSelection {
    char document[64] = {}, instrument[32] = {};
    SongPatch patch;
    uint32_t bank = 0xffffffff, occurrence = 0;
};
struct Song : SongSelection {
    uint32_t id = 0;
    char name[64] = {};
    uint32_t count = 1;
};
struct SongContinuation {
    uint32_t generation = 0, crc = 0, set = 0, entry = 0, song = 0, patch = 0, next = 0, source_crc = 0;
};
struct SongEntry { uint32_t id = 0, song = 0; };
struct Setlist { uint32_t id = 0; char name[64] = {}; uint32_t count = 0; };
struct SongCounts { uint32_t next_id = 1, songs = 0, sets = 0; };
#pragma pack(pop)
inline bool song_patch_valid(const SongPatch &p) {
    if (p.function) return p.function == 1 && button_pair_valid(p.exit_buttons) && !p.output[0]
        && !p.channel && !p.program && p.msb == 255 && p.lsb == 255 && p.volume == 255 && p.pan == 255;
    return !p.exit_buttons && text_valid(reinterpret_cast<const uint8_t *>(p.output), 32)
        && p.channel < 16 && p.program < 128
        && (p.msb < 128 || p.msb == 255) && (p.lsb < 128 || p.lsb == 255)
        && (p.volume < 128 || p.volume == 255) && (p.pan < 128 || p.pan == 255);
}
inline SongPatch song_patch(const Document &d, unsigned index) {
    SongPatch result; const uint8_t *p = d.patch(index);
    if (u16(p) == 65535) { result.function = p[9]; result.exit_buttons = p[10]; return result; }
    const uint8_t *i = d.instrument(u16(p));
    std::memcpy(result.output, i + 32, 32); result.channel = i[64]; result.program = p[4];
    result.msb = p[5]; result.lsb = p[6]; result.volume = p[7]; result.pan = p[8]; return result;
}
struct SongResolver {
    virtual ~SongResolver() = default;
    virtual bool resolve(const SongSelection &song, SongPatch &patch) = 0;
    virtual bool revision(const SongSelection &, uint32_t &crc) = 0;
};

// Three committed generations permit an atomic edit or undo. Slot 3 is an
// uncommitted streaming import, never selected at boot. No collection is in RAM.
class Songs {
public:
    Songs(Storage &storage, Hardware &hardware, SongResolver &resolver)
        : storage_(storage), hardware_(hardware), resolver_(resolver) {}
    const SongCounts &counts() const { return counts_; }
    uint32_t generation() const { return generation_; }
    const char *last_error() const { return error_; }
    bool busy() const { return phase_ != Phase::idle; }
    bool success() const { return good_; }
    // One bounded storage/hash/validation step. Completion publishes atomically.
    void tick() { step(); }
    bool boot() {
        bool found = false; uint32_t best = 0;
        for (unsigned i = 0; i < 3; ++i) {
            uint32_t generation, crc; unsigned length; SongCounts counts;
            if (record(i, generation, length, crc, counts)
                && (!found || int32_t(generation - best) > 0)) {
                found = true; best = generation; active_ = i; length_ = length; crc_ = crc; counts_ = counts;
            }
        }
        if (!found && (storage_.size(0) || storage_.size(1) || storage_.size(2))) { ready_ = true; invalid_ = true; return false; }
        generation_ = best; ready_ = true;
        if (!found) {
            if (!commit(active_, 12, counts_, 0, 0, nullptr, 0, nullptr, nullptr)) return false;
            while (busy()) tick(); // Initialization precedes the live main loop.
            return success();
        }
        return true;
    }
    bool song_at(unsigned index, Song &result) const {
        unsigned offset;
        return song_offset(index, offset) && payload(active_, offset, &result, sizeof(result));
    }
    bool patch_at(uint32_t id, unsigned index, SongSelection &result) const {
        Song item; unsigned position;
        if (!find_song(id, item, position) || index >= item.count) return false;
        if (!index) { result = item; return true; }
        unsigned offset; return song_offset(position, offset)
            && payload(active_, offset + sizeof(Song) + (index - 1) * sizeof(SongSelection), &result, sizeof(result));
    }
    bool insert_patch(uint32_t id, unsigned after, const SongSelection &selection) {
        if (counts_.next_id == 0xffffffff || length_ > 0xffffffffu - 20 - sizeof(SongSelection)
            || !prepare(Edit::insert_patch, id)) return false;
        job_.at = after; job_.selection = selection; return true;
    }
    bool replace_patch(uint32_t id, unsigned at, const SongSelection &selection) {
        if (!prepare(Edit::replace_patch, id)) return false;
        job_.at = at; job_.selection = selection; return true;
    }
    bool remove_patch(uint32_t id, unsigned at) {
        if (!prepare(Edit::remove_patch, id)) return false;
        job_.at = at; return true;
    }
    bool move_patch(uint32_t id, unsigned at, bool earlier) {
        if ((earlier && !at) || (!earlier && at == 0xffffffffu) || !prepare(Edit::move_patch, id)) return false;
        job_.at = at; job_.to = earlier ? at - 1 : at + 1; return true;
    }
    const Song *song(uint32_t id) const {
        if (!id) return nullptr;
        if (song_cache_.id == id) return &song_cache_;
        for (unsigned i = 0; i < counts_.songs; ++i) {
            if (!song_at(i, song_cache_)) return nullptr;
            if (song_cache_.id == id) return &song_cache_;
        }
        return nullptr;
    }
    bool set_at(unsigned index, Setlist &result, unsigned *position = nullptr) const {
        unsigned offset; if (!songs_end(offset)) return false;
        for (unsigned i = 0; i <= index && i < counts_.sets; ++i) {
            if (!payload(active_, offset, &result, sizeof(result))) return false;
            if (i == index) { if (position) *position = offset; return true; }
            offset += sizeof(Setlist) + result.count * sizeof(SongEntry);
        }
        return false;
    }
    const Setlist *setlist(uint32_t id) const {
        if (!id) return nullptr;
        for (unsigned i = 0; i < counts_.sets; ++i) {
            if (!set_at(i, set_cache_, &set_offset_)) return nullptr;
            if (set_cache_.id == id) return &set_cache_;
        }
        return nullptr;
    }
    bool entry(uint32_t set, unsigned index, SongEntry &result) const {
        const Setlist *s = setlist(set);
        return s && index < s->count && payload(active_, set_offset_ + sizeof(Setlist) + index * sizeof(SongEntry), &result, sizeof(result));
    }
    uint32_t browse(uint32_t from, bool backward, bool letter = false) const {
        Song selected; bool has = false;
        if (const auto *s = song(from)) { selected = *s; has = true; }
        Song best, wrap; bool have_best = false, have_wrap = false;
        auto compare = [](const Song &a, const Song &b) {
            int c = name_compare(a.name, b.name); return c ? c : a.id < b.id ? -1 : a.id > b.id ? 1 : 0;
        };
        auto initial = [](const Song &s) { char c = s.name[0]; return c >= 'A' && c <= 'Z' ? c + 32 : c; };
        for (unsigned i = 0; i < counts_.songs; ++i) {
            Song value; if (!song_at(i, value)) return 0;
            if (!have_wrap || (backward ? compare(value, wrap) > 0 : compare(value, wrap) < 0)) { wrap = value; have_wrap = true; }
            int relation = has ? compare(value, selected) : (backward ? -1 : 1);
            if ((backward ? relation < 0 : relation > 0) && (!letter || !has || initial(value) != initial(selected))
                && (!have_best || (backward ? compare(value, best) > 0 : compare(value, best) < 0))) { best = value; have_best = true; }
        }
        return have_best ? best.id : have_wrap ? wrap.id : 0;
    }
    bool save_song(const char *name, const SongPatch &patch, const char *document = "", const char *instrument = "", uint32_t bank = 0xffffffff) {
        if (counts_.next_id >= 0xfffffffe || length_ > 0xffffffffu - 20 - sizeof(Song)) return false;
        Song s; s.id = counts_.next_id; s.occurrence = counts_.next_id + 1; s.patch = patch; s.bank = bank;
        if (document[0]) { s.patch = SongPatch(); std::strcpy(s.patch.output, "din"); s.patch.program = patch.program; }
        std::snprintf(s.name, 64, "%s", name); std::snprintf(s.document, 64, "%s", document); std::snprintf(s.instrument, 32, "%s", instrument);
        if (!valid_song(s)) return false;
        SongCounts counts = counts_; counts.next_id += 2; ++counts.songs;
        if (!prepare(Edit::append_song, s.id)) return false;
        job_.counts = counts; std::memcpy(job_.inserted, &s, sizeof(s)); return true;
    }
    bool rename_song(uint32_t id, const char *name) {
        if (!prepare(Edit::rename_song, id)) return false;
        std::snprintf(job_.name, 64, "%s", name); return true;
    }
    bool change_song(uint32_t id, const SongPatch &patch) {
        if (!prepare(Edit::change_song, id)) return false;
        job_.patch = patch; return true;
    }
    uint32_t new_setlist() {
        if (counts_.next_id == 0xffffffff) return 0;
        Setlist s; s.id = counts_.next_id; std::snprintf(s.name, 64, "Setlist %lu", (unsigned long)s.id);
        SongCounts counts = counts_; ++counts.next_id; ++counts.sets;
        return edit(length_, 0, &s, sizeof(s), counts) ? s.id : 0;
    }
    bool rename_setlist(uint32_t id, const char *name) {
        if (!prepare(Edit::rename_set, id)) return false;
        std::snprintf(job_.name, 64, "%s", name); return true;
    }
    bool insert(uint32_t set, unsigned at, uint32_t song_id) {
        if (counts_.next_id == 0xffffffff || !prepare(Edit::insert, set)) return false;
        job_.at = at; job_.song = song_id; return true;
    }
    bool remove(uint32_t set, unsigned at) {
        if (!prepare(Edit::remove, set)) return false;
        job_.at = at; return true;
    }
    bool move(uint32_t set, unsigned from, unsigned to) {
        if (!prepare(Edit::move, set)) return false;
        job_.at = from; job_.to = to; return true;
    }
    bool undo() {
        if (busy() || !can_undo_) return false;
        uint8_t header[16];
        if (!storage_.read(undo_slot_, 0, header, 16) || std::memcmp(header, "CSG3", 4)) return false;
        unsigned length = u32(header + 8);
        if (length < 12 || length > 0xffffffffu - 20 || storage_.size(undo_slot_) != length + 20) return false;
        initialize_job(); job_.source = undo_slot_; job_.length = length; job_.crc = u32(header + 12);
        job_.undo = true;
        if (active_set_ == undo_set_ && current_entry_ == undo_entry_) job_.continuation = undo_continuation_;
        phase_ = Phase::source_hash; return true;
    }
    void choose(uint32_t set) {
        if (busy()) { job_.choose = set; return; }
        current_song_ = current_patch_ = patch_position_ = 0;
        active_set_ = set; current_entry_ = 0; continuation_ = 0; removed_ = false; current_name_[0] = 0;
    }
    unsigned next_position() const {
        const auto *p = setlist(active_set_); if (!p) return 0; unsigned count = p->count, offset = set_offset_ + sizeof(Setlist);
        SongEntry value;
        for (unsigned i = 0; i < count; ++i) {
            if (!payload(active_, offset + i * sizeof(SongEntry), &value, sizeof(value))) return 0xffffffffu;
            if (value.id == current_entry_) return i + 1;
        }
        return std::min(continuation_, count);
    }
    SongContinuation continuation() const { return {generation_, crc_, active_set_, current_entry_, current_song_, current_patch_, 0}; }
    void cancel_function() {
        if (!pending_function_) return;
        active_set_ = previous_cursor_.set; current_entry_ = previous_cursor_.entry; current_song_ = previous_cursor_.song; current_patch_ = previous_cursor_.patch;
        patch_position_ = previous_patch_position_; continuation_ = previous_continuation_; removed_ = previous_removed_;
        std::memcpy(current_name_, previous_name_, sizeof(current_name_)); pending_function_ = false; error_ = "Firmware switch failed";
    }
    bool resume(const SongContinuation &saved) {
        if (saved.generation != generation_ || saved.crc != crc_) { error_ = "Song data changed"; return false; }
        const Song *item = song(saved.song);
        if (!item) { error_ = "Song missing"; return false; }
        SongSelection current;
        unsigned at = 0;
        for (; at < item->count; ++at) { if (!patch_at(saved.song, at, current)) return false; if (current.occurrence == saved.patch) break; }
        if (at == item->count || !current.patch.function) return false;
        SongEntry entry_value; bool found = false;
        const auto *set = setlist(saved.set);
        if (!set) return false;
        unsigned count = set->count;
        for (unsigned i = 0; i < count; ++i) if (entry(saved.set, i, entry_value) && entry_value.id == saved.entry && entry_value.song == saved.song) found = true;
        if (!found) return false;
        active_set_ = saved.set; current_entry_ = saved.entry; current_song_ = saved.song; current_patch_ = saved.patch; patch_position_ = at; removed_ = false;
        std::snprintf(current_name_, 64, "%s", item->name);
        if (at + 1 == item->count) { if (saved.next) return false; error_ = "End of song"; return true; }
        SongSelection upcoming;
        if (!patch_at(saved.song, at + 1, upcoming) || upcoming.occurrence != saved.next) return false;
        uint32_t revision = 0;
        if (upcoming.document[0] && (!resolver_.revision(upcoming, revision) || revision != saved.source_crc)) { error_ = "Source changed"; return false; }
        return activate_patch(saved.song, at + 1, saved.entry);
    }
    bool next() {
        if (busy()) { error_ = "Song storage busy"; return false; }
        const auto *current = song(current_song_);
        if (current && !removed_) {
            unsigned count = current->count; SongSelection value; bool found = false;
            for (unsigned i = 0; i < count; ++i) {
                if (!patch_at(current_song_, i, value)) return false;
                if (value.occurrence == current_patch_) { patch_position_ = i; found = true; break; }
            }
            if (!found) { error_ = "Current patch removed"; return false; }
            if (patch_position_ + 1 < count) return activate_patch(current_song_, patch_position_ + 1, current_entry_);
        }
        SongEntry value;
        const auto *set = setlist(active_set_);
        if (!set) { error_ = active_set_ ? "Setlist read failed" : "Choose a setlist"; return false; }
        unsigned count = set->count, position = next_position();
        if (position == 0xffffffffu) { error_ = "Setlist read failed"; return false; }
        if (position >= count) { error_ = "End of set"; return false; }
        if (!entry(active_set_, position, value)) { error_ = "Setlist read failed"; return false; }
        const auto *p = song(value.song); if (!p) { error_ = "Song missing"; return false; }
        if (!activate_patch(p->id, 0, value.id)) return false;
        continuation_ = position + 1; return true;
    }
    bool current_patch(SongSelection &result) const {
        const auto *item = song(current_song_); if (!item) return false;
        unsigned count = item->count;
        for (unsigned i = 0; i < count; ++i) {
            if (!patch_at(current_song_, i, result)) return false;
            if (result.occurrence == current_patch_) return true;
        }
        return false;
    }
    bool upcoming_patch(SongSelection &result) const {
        const auto *item = song(current_song_);
        if (!item || removed_) return false;
        unsigned count = item->count;
        SongSelection current;
        for (unsigned i = 0; i + 1 < count; ++i) {
            if (!patch_at(current_song_, i, current)) return false;
            if (current.occurrence == current_patch_) return patch_at(current_song_, i + 1, result);
        }
        return false;
    }
    uint32_t active_set() const { return active_set_; }
    bool invalid() const { return invalid_; }
    const char *current_name() const { return current_name_; }
    bool removed() const { return removed_; }
    unsigned length() const { return length_; }
    uint32_t crc() const { return crc_; }
    bool read_payload(unsigned offset, uint8_t *data, unsigned n) const {
        if (offset > length_ || n > length_ - offset) return false;
        if (!generation_) { if (offset + n > sizeof(counts_)) return false; std::memcpy(data, reinterpret_cast<const uint8_t *>(&counts_) + offset, n); return true; }
        return payload(active_, offset, data, n);
    }
    bool import_begin(unsigned length) { return !busy() && ready_ && length >= 12 && length <= 0xffffffffu - 20 && storage_.begin(3); }
    bool import_write(const uint8_t *bytes, unsigned n) { return storage_.write(bytes, n); }
    void import_abort() { storage_.finish(); }
    bool import_finish(unsigned length, uint32_t crc) {
        if (busy() || !storage_.finish() || storage_.size(3) != length) return false;
        initialize_job(); job_.source = 3; job_.length = length; job_.crc = crc;
        phase_ = Phase::source_hash; return true;
    }
private:
    struct HeaderChange { unsigned offset = 0; Setlist value; bool song = false; Song song_value; };
    struct Move { unsigned offset, from, to; SongEntry value; };
    // Walk one record at a time, validating its bounds before advancing.
    struct Cursor {
        Song song_value; Setlist set_value; SongEntry entry_value;
        SongSelection selection_value;
        SongCounts counts;
        unsigned position = 0, song = 0, set = 0, entries = 0, entry = 0;
        uint32_t id = 0, reference = 0, set_id = 0;
        unsigned set_count = 0;
        unsigned patches = 0, patch_index = 0;
        bool first_occurrence = false;
        bool done = false, item = false, is_song = false, is_selection = false;
        bool step(Storage &storage, unsigned slot, unsigned base, unsigned length) {
            item = false;
            if (!position) {
                if (length < 12 || !storage.read(slot, base, reinterpret_cast<uint8_t *>(&counts), 12)
                    || !counts.next_id || counts.songs > (length - 12) / sizeof(Song)) return false;
                position = 12; return true;
            }
            reference = 0; is_song = is_selection = false;
            if (first_occurrence) {
                first_occurrence = false; selection_value = song_value; patch_index = 0;
                id = selection_value.occurrence; item = is_selection = true; return true;
            }
            if (patches) {
                if (!storage.read(slot, base + position, reinterpret_cast<uint8_t *>(&selection_value), sizeof(selection_value))
                    || !valid_selection(selection_value) || selection_value.occurrence >= counts.next_id) return false;
                position += sizeof(selection_value); --patches; ++patch_index;
                id = selection_value.occurrence; item = is_selection = true; return true;
            }
            if (song < counts.songs) {
                Song value;
                if (position > length || sizeof(value) > length - position
                    || !storage.read(slot, base + position, reinterpret_cast<uint8_t *>(&value), sizeof(value))
                    || !valid_song(value) || value.id >= counts.next_id || value.occurrence >= counts.next_id) return false;
                song_value = value; position += sizeof(value);
                if (value.count - 1 > (length - position) / sizeof(SongSelection)) return false;
                patches = value.count - 1; first_occurrence = true;
                ++song; id = value.id; is_song = item = true; return true;
            }
            if (entries) {
                SongEntry value;
                if (!storage.read(slot, base + position, reinterpret_cast<uint8_t *>(&value), sizeof(value))
                    || !value.id || value.id >= counts.next_id || !value.song || value.song >= counts.next_id) return false;
                entry_value = value; position += sizeof(value); --entries; ++entry;
                id = value.id; reference = value.song; item = true; return true;
            }
            if (set < counts.sets) {
                Setlist value;
                if (position > length || sizeof(value) > length - position
                    || !storage.read(slot, base + position, reinterpret_cast<uint8_t *>(&value), sizeof(value))
                    || !value.id || value.id >= counts.next_id
                    || !text_valid(reinterpret_cast<const uint8_t *>(value.name), 64)) return false;
                position += sizeof(value);
                if (value.count > (length - position) / sizeof(SongEntry)) return false;
                set_value = value; ++set; entries = set_count = value.count; entry = 0; set_id = id = value.id; item = true; return true;
            }
            done = true; return position == length;
        }
    };
    enum class Edit { append_song, rename_song, change_song, rename_set, insert, remove, move,
        insert_patch, replace_patch, remove_patch, move_patch };
    enum class Phase { idle, prepare, source_hash, source_structure, identities, identity_scan,
        hash, begin, header, write, footer, close, verify_header, verify_hash, verify_structure };
    struct Job {
        unsigned source = 0, destination = 0, length = 0, offset = 0, erased = 0, inserted_size = 0, position = 0;
        SongCounts counts;
        uint8_t inserted[2 * sizeof(SongSelection)] = {};
        HeaderChange change = {}; Move move = {};
        bool has_change = false, has_move = false, undo = false, opened = false;
        uint32_t crc = 0, running_crc = 0xffffffff, choose = 0;
        unsigned continuation = 0, matches = 0, references = 0, set_count = 0;
        bool found_set = false, found_entry = false;
        Edit edit = Edit::rename_song;
        uint32_t target = 0, song = 0;
        unsigned at = 0, to = 0;
        char name[64] = {};
        SongPatch patch; SongSelection selection, previous;
        bool song_exists = false;
        Cursor cursor, scan;
    } job_;
    Phase phase_ = Phase::idle;
    bool good_ = true;
    Storage &storage_; Hardware &hardware_; SongResolver &resolver_;
    SongCounts counts_;
    mutable Song song_cache_;
    mutable Setlist set_cache_;
    mutable unsigned set_offset_ = 0;
    uint32_t generation_ = 0, crc_ = checksum(reinterpret_cast<const uint8_t *>(&counts_), sizeof(counts_));
    unsigned active_ = 0, length_ = 12, undo_slot_ = 0;
    uint32_t active_set_ = 0, current_entry_ = 0, undo_set_ = 0, undo_entry_ = 0;
    unsigned continuation_ = 0, undo_continuation_ = 0, patch_position_ = 0;
    uint32_t current_song_ = 0, current_patch_ = 0;
    bool ready_ = false, can_undo_ = false, removed_ = false, invalid_ = false;
    char current_name_[64] = {}, previous_name_[64] = {};
    SongContinuation previous_cursor_;
    unsigned previous_patch_position_ = 0, previous_continuation_ = 0;
    bool pending_function_ = false, previous_removed_ = false;
    const char *error_ = "";
    bool activate_patch(uint32_t id, unsigned position, uint32_t entry_id) {
        SongSelection item;
        if (!patch_at(id, position, item)) { error_ = "Song patch missing"; return false; }
        SongPatch patch = item.patch;
        if (item.document[0] && !resolver_.resolve(item, patch)) { error_ = "Source missing/ambiguous"; return false; }
        if (!song_patch_valid(patch)) return false;
        if (patch.function) {
            SongContinuation saved{generation_, crc_, active_set_, entry_id, id, item.occurrence, 0};
            SongSelection next;
            const auto *song_item = song(id);
            if (!song_item) return false;
            if (position + 1 < song_item->count) { if (!patch_at(id, position + 1, next)) return false; saved.next = next.occurrence;
                if (next.document[0] && !resolver_.revision(next, saved.source_crc)) { error_ = "Source unavailable"; return false; }
            }
            previous_cursor_ = continuation(); previous_patch_position_ = patch_position_; previous_continuation_ = continuation_; previous_removed_ = removed_;
            std::memcpy(previous_name_, current_name_, sizeof(previous_name_));
            if (!hardware_.stock_firmware(patch.exit_buttons, &saved)) { error_ = "Firmware switch failed"; return false; }
            pending_function_ = true;
        } else {
            pending_function_ = false;
            if (!hardware_.supports(patch.output)) { error_ = "Song output unavailable"; return false; }
            uint8_t bytes[14]; unsigned n = 0;
            auto cc = [&](uint8_t number, uint8_t v) { if (v != 255) { bytes[n++] = 0xb0 | patch.channel; bytes[n++] = number; bytes[n++] = v; } };
            cc(0, patch.msb); cc(32, patch.lsb); bytes[n++] = 0xc0 | patch.channel; bytes[n++] = patch.program; cc(7, patch.volume); cc(10, patch.pan);
            if (!hardware_.send(patch.output, bytes, n)) { error_ = "MIDI output unavailable"; return false; }
        }
        const auto *song_item = song(id); if (!song_item) return false;
        std::snprintf(current_name_, 64, "%s", song_item->name);
        current_song_ = id; current_patch_ = item.occurrence; patch_position_ = position; current_entry_ = entry_id; removed_ = false; error_ = ""; return true;
    }
    static bool valid_song(const Song &s) {
        return s.id && s.count && s.occurrence && text_valid(reinterpret_cast<const uint8_t *>(s.name), 64) && valid_selection(s);
    }
    static bool valid_selection(const SongSelection &s) {
        return s.occurrence && song_patch_valid(s.patch) && (!s.patch.function || (!s.document[0] && !s.instrument[0] && s.bank == 0xffffffff))
            && (!s.document[0] || (text_valid(reinterpret_cast<const uint8_t *>(s.document), 64) && setup_name(s.document)
                && text_valid(reinterpret_cast<const uint8_t *>(s.instrument), 32)));
    }
    bool find_song(uint32_t id, Song &s, unsigned &index) const {
        for (index = 0; index < counts_.songs; ++index)
            if (!song_at(index, s)) return false; else if (s.id == id) return true;
        return false;
    }
    bool song_offset(unsigned index, unsigned &offset) const {
        offset = 12;
        if (index >= counts_.songs) return false;
        for (unsigned i = 0; i < index; ++i) {
            Song item; if (!payload(active_, offset, &item, sizeof(item))) return false;
            offset += sizeof(Song) + (item.count - 1) * sizeof(SongSelection);
        }
        return true;
    }
    bool songs_end(unsigned &offset) const {
        offset = 12;
        if (!counts_.songs) return true;
        if (!song_offset(counts_.songs - 1, offset)) return false;
        Song item; if (!payload(active_, offset, &item, sizeof(item))) return false;
        offset += sizeof(Song) + (item.count - 1) * sizeof(SongSelection); return true;
    }
    bool payload(unsigned slot, unsigned offset, void *data, unsigned n) const {
        return storage_.read(slot, (slot == 3 ? 0 : 16) + offset, static_cast<uint8_t *>(data), n);
    }
    bool crc_range(unsigned slot, unsigned offset, unsigned length, uint32_t &result) const {
        uint8_t buffer[256]; uint32_t crc = 0xffffffff;
        while (length) {
            unsigned n = std::min(length, unsigned(sizeof(buffer)));
            if (!storage_.read(slot, offset, buffer, n)) return false;
            update_crc(crc, buffer, n); offset += n; length -= n;
        }
        result = ~crc; return true;
    }
    static void update_crc(uint32_t &crc, const uint8_t *bytes, unsigned n) {
        for (unsigned i = 0; i < n; ++i) { crc ^= bytes[i]; for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1))); }
    }
    bool validate(unsigned slot, unsigned length, unsigned base) const {
        SongCounts counts;
        if (length < 12 || !storage_.read(slot, base, reinterpret_cast<uint8_t *>(&counts), 12) || !counts.next_id
            || counts.songs > (length - 12) / sizeof(Song)) return false;
        unsigned offset = 12;
        for (unsigned i = 0; i < counts.songs; ++i) {
            Song s; if (offset > length || sizeof(s) > length - offset || !storage_.read(slot, base + offset, reinterpret_cast<uint8_t *>(&s), sizeof(s)) || !valid_song(s) || s.id >= counts.next_id || s.occurrence >= counts.next_id) return false;
            offset += sizeof(s);
            if (s.count - 1 > (length - offset) / sizeof(SongSelection)) return false;
            for (unsigned j = 1; j < s.count; ++j, offset += sizeof(SongSelection)) {
                SongSelection value;
                if (!storage_.read(slot, base + offset, reinterpret_cast<uint8_t *>(&value), sizeof(value)) || !valid_selection(value) || value.occurrence >= counts.next_id) return false;
            }
        }
        for (unsigned i = 0; i < counts.sets; ++i) {
            Setlist s;
            if (offset > length || sizeof(s) > length - offset || !storage_.read(slot, base + offset, reinterpret_cast<uint8_t *>(&s), sizeof(s))
                || !s.id || s.id >= counts.next_id || !text_valid(reinterpret_cast<const uint8_t *>(s.name), 64)) return false;
            offset += sizeof(s); if (s.count > (length - offset) / sizeof(SongEntry)) return false;
            for (unsigned j = 0; j < s.count; ++j, offset += sizeof(SongEntry)) {
                SongEntry e;
                if (!storage_.read(slot, base + offset, reinterpret_cast<uint8_t *>(&e), sizeof(e)) || !e.id || e.id >= counts.next_id || !e.song || e.song >= counts.next_id) return false;
            }
        }
        return offset == length;
    }
    bool record(unsigned slot, uint32_t &generation, unsigned &length, uint32_t &crc, SongCounts &counts) const {
        uint8_t header[16], tail[4];
        if (!storage_.read(slot, 0, header, 16) || std::memcmp(header, "CSG3", 4)) return false;
        length = u32(header + 8); crc = u32(header + 12); generation = u32(header + 4);
        uint32_t actual;
        return length <= 0xffffffffu - 20 && storage_.size(slot) == length + 20
            && storage_.read(slot, 16 + length, tail, 4) && !std::memcmp(tail, "DONE", 4)
            && crc_range(slot, 16, length, actual) && actual == crc && validate(slot, length, 16)
            && storage_.read(slot, 16, reinterpret_cast<uint8_t *>(&counts), 12);
    }
    bool edit(unsigned offset, unsigned erased, const void *bytes, unsigned n, const SongCounts &counts) {
        if (offset > length_ || erased > length_ - offset || n > 0xffffffffu - 20 - (length_ - erased)) return false;
        return commit(active_, length_ - erased + n, counts, offset, erased, bytes, n, nullptr, nullptr);
    }
    bool prepare(Edit edit, uint32_t target) {
        if (!ready_ || invalid_ || busy() || !target) return false;
        initialize_job(); job_.source = active_; job_.length = length_; job_.counts = counts_;
        job_.edit = edit; job_.target = target; phase_ = Phase::prepare; return true;
    }
    void plan(unsigned offset, unsigned erased, const void *bytes, unsigned n) {
        job_.offset = offset; job_.erased = erased; job_.inserted_size = n;
        if (n && bytes != job_.inserted) std::memcpy(job_.inserted, bytes, n);
        job_.length = length_ - erased + n; start_hash();
    }
    void prepare_step() {
        auto &c = job_.cursor;
        if (job_.edit == Edit::append_song && c.position && c.song == counts_.songs && !c.first_occurrence && !c.patches) {
            plan(c.position, 0, job_.inserted, sizeof(Song)); return;
        }
        if (!c.step(storage_, active_, 16, length_) || c.done) { finish_job(false); return; }
        if (!c.item) return;
        if (patch_edit() && c.song_value.id == job_.target) { prepare_patch(c); return; }
        if (c.is_selection) return;
        if (c.is_song) {
            if (c.id == job_.song) job_.song_exists = true;
            if (c.id != job_.target || (job_.edit != Edit::rename_song && job_.edit != Edit::change_song)) return;
            Song s = c.song_value;
            if (job_.edit == Edit::rename_song) { std::memset(s.name, 0, 64); std::snprintf(s.name, 64, "%s", job_.name); }
            else { s.patch = job_.patch; std::memset(s.document, 0, 64); std::memset(s.instrument, 0, 32); s.bank = 0xffffffff; }
            if (!valid_song(s)) { finish_job(false); return; }
            plan(c.position - sizeof(Song), sizeof(Song), &s, sizeof(s)); return;
        }
        if (c.set_id != job_.target) return;
        if (!c.reference) {
            if (job_.edit == Edit::rename_set) {
                Setlist s = c.set_value; std::memset(s.name, 0, 64); std::snprintf(s.name, 64, "%s", job_.name);
                if (!text_valid(reinterpret_cast<const uint8_t *>(s.name), 64)) { finish_job(false); return; }
                plan(c.position - sizeof(Setlist), sizeof(Setlist), &s, sizeof(s)); return;
            }
            if (job_.edit != Edit::insert && job_.edit != Edit::remove && job_.edit != Edit::move) return;
            if (job_.at > c.set_count || (job_.edit != Edit::insert && job_.at == c.set_count)
                || (job_.edit == Edit::move && job_.to >= c.set_count)) { finish_job(false); return; }
            job_.change = HeaderChange(); job_.change.offset = c.position - unsigned(sizeof(Setlist)); job_.change.value = c.set_value;
            if (job_.edit == Edit::insert) {
                if (!job_.song_exists || length_ > 0xffffffffu - 20 - sizeof(SongEntry)) { finish_job(false); return; }
                ++job_.change.value.count; job_.has_change = true; ++job_.counts.next_id;
                SongEntry e{counts_.next_id, job_.song};
                if (active_set_ == job_.target && removed_ && job_.at < continuation_) ++job_.continuation;
                plan(c.position + job_.at * sizeof(SongEntry), 0, &e, sizeof(e));
            } else if (job_.edit == Edit::move && job_.at == job_.to) finish_job(true);
            return;
        }
        if (c.entry != job_.at + 1) return;
        if (job_.edit == Edit::remove) {
            --job_.change.value.count; job_.has_change = true;
            if (active_set_ == job_.target) {
                if (c.id == current_entry_) job_.continuation = job_.at;
                else if (removed_ && job_.at < continuation_) --job_.continuation;
            }
            plan(c.position - sizeof(SongEntry), sizeof(SongEntry), nullptr, 0);
        } else if (job_.edit == Edit::move) {
            job_.has_move = true;
            job_.move = {job_.change.offset + unsigned(sizeof(Setlist)), job_.at, job_.to, c.entry_value};
            if (active_set_ == job_.target && removed_) {
                if (job_.at < job_.continuation) --job_.continuation;
                if (job_.to < job_.continuation) ++job_.continuation;
            }
            start_hash();
        }
    }
    bool patch_edit() const {
        return job_.edit == Edit::insert_patch || job_.edit == Edit::replace_patch
            || job_.edit == Edit::remove_patch || job_.edit == Edit::move_patch;
    }
    void prepare_patch(const Cursor &c) {
        if (c.is_song) {
            if (job_.at >= c.song_value.count || (job_.edit == Edit::remove_patch && c.song_value.count == 1)
                || (job_.edit == Edit::move_patch && job_.to >= c.song_value.count)) { finish_job(false); return; }
            job_.change = HeaderChange(); job_.change.offset = c.position - sizeof(Song);
            job_.change.song = true; job_.change.song_value = c.song_value;
            if (job_.edit == Edit::insert_patch) {
                auto value = job_.selection; value.occurrence = counts_.next_id;
                if (!valid_selection(value)) { finish_job(false); return; }
                job_.has_change = true; ++job_.change.song_value.count; ++job_.counts.next_id;
                plan(c.position + job_.at * sizeof(SongSelection), 0, &value, sizeof(value));
            }
            return;
        }
        if (!c.is_selection) return;
        if (job_.edit == Edit::replace_patch && c.patch_index == job_.at) {
            auto value = job_.selection; value.occurrence = c.selection_value.occurrence;
            if (!valid_selection(value)) { finish_job(false); return; }
            if (!job_.at) {
                auto s = c.song_value; static_cast<SongSelection &>(s) = value;
                plan(job_.change.offset, sizeof(Song), &s, sizeof(s));
            } else plan(c.position - sizeof(SongSelection), sizeof(SongSelection), &value, sizeof(value));
        } else if (job_.edit == Edit::remove_patch && c.patch_index == (job_.at ? job_.at : 1)) {
            job_.has_change = true; --job_.change.song_value.count;
            if (!job_.at) static_cast<SongSelection &>(job_.change.song_value) = c.selection_value;
            plan(job_.change.offset + sizeof(Song) + (job_.at ? job_.at - 1 : 0) * sizeof(SongSelection), sizeof(SongSelection), nullptr, 0);
        } else if (job_.edit == Edit::move_patch) {
            unsigned first = std::min(job_.at, job_.to);
            if (c.patch_index == first) job_.previous = c.selection_value;
            if (c.patch_index != first + 1) return;
            if (!first) {
                job_.has_change = true; static_cast<SongSelection &>(job_.change.song_value) = c.selection_value;
                plan(job_.change.offset + sizeof(Song), sizeof(SongSelection), &job_.previous, sizeof(SongSelection));
            } else {
                SongSelection values[] = {c.selection_value, job_.previous};
                plan(job_.change.offset + sizeof(Song) + (first - 1) * sizeof(SongSelection), sizeof(values), values, sizeof(values));
            }
        }
    }
    bool commit(unsigned source, unsigned length, const SongCounts &counts, unsigned offset, unsigned erased,
                const void *inserted, unsigned inserted_size, const HeaderChange *change, const Move *move) {
        if (!ready_ || (invalid_ && source != 3) || busy() || inserted_size > sizeof(job_.inserted)) return false;
        initialize_job(); job_.source = source; job_.length = length; job_.counts = counts;
        job_.offset = offset; job_.erased = erased; job_.inserted_size = inserted_size;
        if (inserted_size) std::memcpy(job_.inserted, inserted, inserted_size);
        if (change) { job_.has_change = true; job_.change = *change; }
        if (move) { job_.has_move = true; job_.move = *move; }
        phase_ = Phase::hash; return true;
    }
    void initialize_job() {
        job_ = Job(); job_.continuation = continuation_; good_ = false; error_ = "";
        while (job_.destination == active_ || (can_undo_ && job_.destination == undo_slot_)) ++job_.destination;
    }
    void finish_job(bool good) {
        if (job_.opened) { storage_.finish(); job_.opened = false; }
        good_ = good; if (!good) error_ = "Not saved";
        phase_ = Phase::idle;
    }
    void header(uint8_t *bytes) const {
        std::memcpy(bytes, "CSG3", 4); put32(bytes + 4, generation_ + 1);
        put32(bytes + 8, job_.length); put32(bytes + 12, job_.crc);
    }
    bool stream(uint8_t *buffer, unsigned n) {
        for (unsigned j = 0; j < n;) {
            unsigned at = job_.position + j, take = n - j, original = at;
            const uint8_t *memory = nullptr;
            const auto &change = job_.change; const auto &move = job_.move;
            if (at < 12) { memory = reinterpret_cast<const uint8_t *>(&job_.counts) + at; take = 12 - at; }
            else if (job_.has_change && at >= change.offset && at < change.offset + (change.song ? sizeof(Song) : sizeof(Setlist))) {
                memory = (change.song ? reinterpret_cast<const uint8_t *>(&change.song_value)
                    : reinterpret_cast<const uint8_t *>(&change.value)) + (at - change.offset);
                take = change.offset + (change.song ? sizeof(Song) : sizeof(Setlist)) - at;
            } else if (job_.inserted_size && at >= job_.offset && at < job_.offset + job_.inserted_size) {
                memory = job_.inserted + (at - job_.offset); take = job_.offset + job_.inserted_size - at;
            } else {
                if (job_.inserted_size || job_.erased) {
                    if (at < job_.offset) take = std::min(take, job_.offset - at);
                    else original = at - job_.inserted_size + job_.erased;
                }
                if (job_.has_change && at < change.offset) take = std::min(take, change.offset - at);
                if (job_.has_move && at >= move.offset) {
                    unsigned index = (at - move.offset) / sizeof(SongEntry), byte = (at - move.offset) % sizeof(SongEntry);
                    if (index == move.to) { memory = reinterpret_cast<const uint8_t *>(&move.value) + byte; take = sizeof(SongEntry) - byte; }
                    else if (index >= std::min(move.from, move.to) && index <= std::max(move.from, move.to)) {
                        original = move.offset + (move.from < move.to ? index + 1 : index - 1) * sizeof(SongEntry) + byte;
                        take = sizeof(SongEntry) - byte;
                    } else if (index < std::min(move.from, move.to))
                        take = std::min(take, move.offset + std::min(move.from, move.to) * unsigned(sizeof(SongEntry)) - at);
                } else if (job_.has_move && at < move.offset) take = std::min(take, move.offset - at);
            }
            take = std::min(take, n - j);
            if (memory) std::memcpy(buffer + j, memory, take);
            else if (!payload(job_.source, original, buffer + j, take)) return false;
            j += take;
        }
        return true;
    }
    void start_hash() {
        job_.position = 0; job_.running_crc = 0xffffffff; phase_ = Phase::hash;
    }
    void publish() {
        undo_slot_ = active_; undo_set_ = active_set_; undo_entry_ = current_entry_; undo_continuation_ = continuation_;
        can_undo_ = generation_ != 0 && !job_.undo;
        active_ = job_.destination; ++generation_; length_ = job_.length; crc_ = job_.crc; counts_ = job_.counts;
        song_cache_ = Song(); set_cache_ = Setlist(); invalid_ = false;
        if (job_.choose) {
            current_song_ = current_patch_ = patch_position_ = 0;
            active_set_ = job_.choose; current_entry_ = continuation_ = 0; removed_ = false; current_name_[0] = 0;
        } else if (job_.found_set) {
            continuation_ = std::min(job_.continuation, job_.set_count);
            removed_ = current_entry_ && !job_.found_entry;
        } else active_set_ = current_entry_ = 0;
        finish_job(true);
    }
    void step() {
        uint8_t buffer[256];
        switch (phase_) {
        case Phase::idle: return;
        case Phase::prepare: prepare_step(); return;
        case Phase::source_hash:
        case Phase::verify_hash: {
            bool source = phase_ == Phase::source_hash;
            unsigned n = std::min(256u, job_.length - job_.position);
            if (!payload(source ? job_.source : job_.destination, job_.position, buffer, n)) { finish_job(false); return; }
            update_crc(job_.running_crc, buffer, n); job_.position += n;
            if (job_.position != job_.length) return;
            if (~job_.running_crc != job_.crc) { finish_job(false); return; }
            if (source && job_.source != 3 && (!storage_.read(job_.source, 16 + job_.length, buffer, 4)
                || std::memcmp(buffer, "DONE", 4))) { finish_job(false); return; }
            job_.cursor = Cursor(); phase_ = source ? Phase::source_structure : Phase::verify_structure; return;
        }
        case Phase::source_structure:
        case Phase::verify_structure: {
            bool source = phase_ == Phase::source_structure;
            if (!job_.cursor.step(storage_, source ? job_.source : job_.destination,
                    source && job_.source == 3 ? 0 : 16, job_.length)) { finish_job(false); return; }
            const auto &c = job_.cursor;
            if (!source && c.item && !c.is_song && c.set_id == (job_.choose ? job_.choose : active_set_)) {
                job_.found_set = true; job_.set_count = c.set_count;
                if (c.reference && c.id == current_entry_) job_.found_entry = true;
            }
            if (!c.done) return;
            if (!source) { publish(); return; }
            job_.counts = c.counts;
            if (job_.undo) { job_.counts.next_id = std::max(job_.counts.next_id, counts_.next_id); start_hash(); }
            else { job_.cursor = Cursor(); phase_ = Phase::identities; }
            return;
        }
        case Phase::identities:
            if (!job_.cursor.step(storage_, 3, 0, job_.length)) { finish_job(false); return; }
            if (job_.cursor.done) { start_hash(); return; }
            if (job_.cursor.item) {
                job_.matches = job_.references = 0; job_.scan = Cursor(); phase_ = Phase::identity_scan;
            }
            return;
        case Phase::identity_scan:
            if (!job_.scan.step(storage_, 3, 0, job_.length)) { finish_job(false); return; }
            if (job_.scan.item) {
                if (job_.scan.id == job_.cursor.id) ++job_.matches;
                if (job_.scan.is_song && job_.scan.id == job_.cursor.reference) ++job_.references;
            }
            if (job_.scan.done) {
                if (job_.matches != 1 || (job_.cursor.reference && job_.references != 1)) { finish_job(false); return; }
                phase_ = Phase::identities;
            }
            return;
        case Phase::hash:
        case Phase::write: {
            unsigned n = std::min(256u, job_.length - job_.position);
            if (!stream(buffer, n)) { finish_job(false); return; }
            update_crc(job_.running_crc, buffer, n);
            if (phase_ == Phase::write && !storage_.write(buffer, n)) { finish_job(false); return; }
            job_.position += n;
            if (job_.position != job_.length) return;
            if (phase_ == Phase::hash) { job_.crc = ~job_.running_crc; phase_ = Phase::begin; }
            else if (~job_.running_crc != job_.crc) finish_job(false);
            else phase_ = Phase::footer;
            return;
        }
        case Phase::begin:
            if (!storage_.begin(job_.destination)) { finish_job(false); return; }
            job_.opened = true; phase_ = Phase::header; return;
        case Phase::header:
            header(buffer);
            if (!storage_.write(buffer, 16)) { finish_job(false); return; }
            job_.position = 0; job_.running_crc = 0xffffffff; phase_ = Phase::write; return;
        case Phase::footer:
            if (!storage_.write(reinterpret_cast<const uint8_t *>("DONE"), 4)) { finish_job(false); return; }
            phase_ = Phase::close; return;
        case Phase::close:
            job_.opened = false;
            if (!storage_.finish()) { finish_job(false); return; }
            phase_ = Phase::verify_header; return;
        case Phase::verify_header: {
            uint8_t expected[16]; header(expected);
            if (storage_.size(job_.destination) != job_.length + 20
                || !storage_.read(job_.destination, 0, buffer, 16) || std::memcmp(buffer, expected, 16)
                || !storage_.read(job_.destination, 16 + job_.length, buffer, 4) || std::memcmp(buffer, "DONE", 4)) {
                finish_job(false); return;
            }
            job_.position = 0; job_.running_crc = 0xffffffff; phase_ = Phase::verify_hash; return;
        }
        }
    }

};
} // namespace cntrl
