#pragma once
#include "documents.h"
#include <algorithm>

namespace cntrl {
#pragma pack(push, 1)
struct SongSelection {
    char document[64] = {}, key[64] = {}, label[64] = {};
    uint32_t source = 0xffffffff, node = 0, occurrence = 0;
};
struct Song : SongSelection {
    uint32_t id = 0;
    char name[64] = {};
    uint32_t count = 1;
};
struct SongContinuation {
    uint32_t generation = 0, crc = 0, set = 0, entry = 0, song = 0, patch = 0, next = 0, source_crc = 0;
    uint32_t node = 0, completed = 0, sources_crc = 0, caller = 0, source = 0xffffffff;
    char document[64] = {};
};
struct SongEntry { uint32_t id = 0, song = 0; };
struct Setlist { uint32_t id = 0; char name[64] = {}; uint32_t count = 0; };
struct SongCounts { uint32_t next_id = 1, songs = 0, sets = 0; };
#pragma pack(pop)
enum class SongExecution { pending, completed, failed, unknown, suspended };
inline bool read_song_record(Storage &storage, unsigned slot, unsigned offset, void *destination, unsigned length) {
    auto *bytes = static_cast<uint8_t *>(destination);
    for (unsigned i = 0; i < length;) {
        unsigned n = std::min(256u, length - i);
        if (!storage.read(slot, offset + i, bytes + i, n)) return false;
        i += n;
    }
    return true;
}
struct SongResolver {
    virtual ~SongResolver() = default;
    virtual bool activate(const SongSelection &, const SongContinuation &, bool resume) = 0;
    virtual SongExecution execution_status() const = 0;
    virtual bool capture_source(const Document *&) = 0;
    virtual bool revision(const SongSelection &, uint32_t &crc) = 0;
};

// Three committed generations permit an atomic edit or undo. Slot 3 is an
// uncommitted streaming import, never selected at boot. No collection is in RAM.
class Songs {
public:
    Songs(Storage &storage, SongResolver &resolver) : storage_(storage), resolver_(resolver) {}
    const SongCounts &counts() const { return counts_; }
    uint32_t generation() const { return generation_; }
    const char *last_error() const { return error_; }
    bool busy() const { return phase_ != Phase::idle; }
    bool success() const { return good_; }
    // One bounded storage/hash/validation step. Completion publishes atomically.
    void tick() {
        step();
        if (invocation_pending_) {
            auto state = resolver_.execution_status();
            if (state == SongExecution::suspended) { pending_function_ = true; error_ = "Visiting stock firmware"; }
            else if (state != SongExecution::pending) {
                invocation_pending_ = false;
                error_ = state == SongExecution::completed ? "Completed locally"
                    : state == SongExecution::unknown ? "Remote state unknown" : "Patch failed";
            }
        }
        if (resume_pending_) {
            auto state = resolver_.execution_status();
            if (state == SongExecution::completed) {
                resume_pending_ = false;
                advance_after_return();
            } else if (state == SongExecution::failed || state == SongExecution::unknown) {
                resume_pending_ = false; error_ = state == SongExecution::unknown ? "Remote state unknown" : "Patch failed";
            }
        }
    }
    bool boot() {
        bool found = false; uint32_t best = 0;
        for (unsigned i = 0; i < 3; ++i) {
            uint32_t generation, crc; unsigned length; SongCounts counts;
            if (record(i, generation, length, crc, counts)
                && (!found || int32_t(generation - best) > 0)) {
                found = true; best = generation; active_ = i; length_ = length; crc_ = crc; counts_ = counts;
                source_count_ = verified_source_count_; records_end_ = verified_records_end_;
            }
        }
        if (!found && (storage_.size(0) || storage_.size(1) || storage_.size(2))) { ready_ = true; invalid_ = true; return false; }
        generation_ = best; ready_ = true;
        if (!found) {
            if (!commit(active_, 16, counts_, 12, 0, "CSP4", 4, nullptr, nullptr)) return false;
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
        job_.at = after; job_.selection = selection; return prepare_source(job_.selection);
    }
    bool replace_patch(uint32_t id, unsigned at, const SongSelection &selection) {
        if (!prepare(Edit::replace_patch, id)) return false;
        job_.at = at; job_.selection = selection; return prepare_source(job_.selection);
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
    bool save_song(const char *name, const SongSelection &selection) {
        if (counts_.next_id >= 0xfffffffe || length_ > 0xffffffffu - 20 - sizeof(Song)) return false;
        Song s; static_cast<SongSelection &>(s) = selection;
        s.id = counts_.next_id; s.occurrence = counts_.next_id + 1;
        std::snprintf(s.name, 64, "%s", name);
        SongCounts counts = counts_; counts.next_id += 2; ++counts.songs;
        if (!prepare(Edit::append_song, s.id)) return false;
        if (!prepare_source(s) || !valid_song(s)) { finish_job(false); return false; }
        job_.counts = counts; std::memcpy(job_.inserted, &s, sizeof(s)); return true;
    }
    bool rename_song(uint32_t id, const char *name) {
        if (!prepare(Edit::rename_song, id)) return false;
        std::snprintf(job_.name, 64, "%s", name); return true;
    }
    bool change_song(uint32_t id, const SongSelection &selection) {
        if (!prepare(Edit::change_song, id)) return false;
        job_.selection = selection; return prepare_source(job_.selection);
    }
    uint32_t new_setlist() {
        if (counts_.next_id == 0xffffffff) return 0;
        Setlist s; s.id = counts_.next_id; std::snprintf(s.name, 64, "Setlist %lu", (unsigned long)s.id);
        SongCounts counts = counts_; ++counts.next_id; ++counts.sets;
        return edit(records_end_, 0, &s, sizeof(s), counts) ? s.id : 0;
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
        if (!storage_.read(undo_slot_, 0, header, 16) || std::memcmp(header, "CSG4", 4)) return false;
        unsigned length = u32(header + 8);
        if (length < 16 || length > 0xffffffffu - 20 || storage_.size(undo_slot_) != length + 20) return false;
        initialize_job(); job_.source = undo_slot_; job_.length = length; job_.crc = u32(header + 12);
        job_.undo = true;
        if (active_set_ == undo_set_ && current_entry_ == undo_entry_) job_.continuation = undo_continuation_;
        phase_ = Phase::source_hash; return true;
    }
    void choose(uint32_t set) {
        if (executing()) { error_ = "Patch pending"; return; }
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
        std::memcpy(current_name_, previous_name_, sizeof(current_name_)); pending_function_ = invocation_pending_ = false; error_ = "Firmware switch failed";
    }
    bool resume(const SongContinuation &saved) {
        if (saved.generation != generation_ || saved.crc != crc_) { error_ = "Song data changed"; return false; }
        const Song *item = song(saved.song);
        if (!item) { error_ = "Song missing"; return false; }
        SongSelection current;
        unsigned at = 0;
        for (; at < item->count; ++at) { if (!patch_at(saved.song, at, current)) return false; if (current.occurrence == saved.patch) break; }
        if (at == item->count) return false;
        SongEntry entry_value; bool found = false;
        const auto *set = setlist(saved.set);
        if (!set) return false;
        unsigned count = set->count;
        for (unsigned i = 0; i < count; ++i) if (entry(saved.set, i, entry_value) && entry_value.id == saved.entry && entry_value.song == saved.song) found = true;
        if (!found) return false;
        if (at + 1 == item->count) { if (saved.next) return false; }
        else {
            SongSelection upcoming;
            if (!patch_at(saved.song, at + 1, upcoming) || upcoming.occurrence != saved.next) return false;
            uint32_t revision = 0;
            if (upcoming.document[0] && (!resolver_.revision(upcoming, revision) || revision != saved.source_crc)) { error_ = "Source changed"; return false; }
        }
        if (!resolver_.activate(current, saved, true)) { error_ = "Patch return unavailable"; return false; }
        active_set_ = saved.set; current_entry_ = saved.entry; current_song_ = saved.song; current_patch_ = saved.patch; patch_position_ = at; removed_ = false;
        std::snprintf(current_name_, 64, "%s", item->name);
        resume_pending_ = true;
        if (resolver_.execution_status() == SongExecution::completed) { resume_pending_ = false; return advance_after_return(); }
        return true;
    }
    bool next() {
        if (busy() || executing()) { error_ = "Song work pending"; return false; }
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
    bool executing() const {
        auto state = resolver_.execution_status();
        return state == SongExecution::pending || state == SongExecution::suspended;
    }
    bool invalid() const { return invalid_; }
    const char *current_name() const { return current_name_; }
    bool removed() const { return removed_; }
    unsigned length() const { return length_; }
    uint32_t crc() const { return crc_; }
    unsigned source_size(unsigned index) const {
        unsigned offset, length;
        return source_offset(index, offset, length) ? length : 0;
    }
    bool read_source(unsigned index, unsigned offset, uint8_t *data, unsigned n) const {
        unsigned start, length;
        return source_offset(index, start, length) && offset <= length && n <= length - offset
            && payload(active_, start + offset, data, n);
    }
    bool read_payload(unsigned offset, uint8_t *data, unsigned n) const {
        if (offset > length_ || n > length_ - offset) return false;
        if (!generation_) { if (offset + n > sizeof(counts_)) return false; std::memcpy(data, reinterpret_cast<const uint8_t *>(&counts_) + offset, n); return true; }
        return payload(active_, offset, data, n);
    }
    bool import_begin(unsigned length) { return !busy() && !executing() && ready_ && length >= 16 && length <= 0xffffffffu - 20 && storage_.begin(3); }
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
        unsigned sources = 0, source_remaining = 0, greatest_source = 0, records_end = 12;
        uint32_t source_crc = 0xffffffff, source_expected = 0;
        bool graph_started = false, inline_source = false, verify_sources = true;
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
                remember_source(); id = selection_value.occurrence; item = is_selection = true; return true;
            }
            if (patches) {
                if (!storage.read(slot, base + position, reinterpret_cast<uint8_t *>(&selection_value), sizeof(selection_value))
                    || !valid_selection(selection_value) || selection_value.occurrence >= counts.next_id) return false;
                position += sizeof(selection_value); --patches; ++patch_index;
                remember_source(); id = selection_value.occurrence; item = is_selection = true; return true;
            }
            if (song < counts.songs) {
                Song value;
                if (position > length || sizeof(value) > length - position
                    || !read_song_record(storage, slot, base + position, &value, sizeof(value))
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
            if (!graph_started) {
                uint8_t magic[4];
                if (position > length || 4 > length - position || !storage.read(slot, base + position, magic, 4)
                    || std::memcmp(magic, "CSP4", 4)) return false;
                records_end = position; position += 4; graph_started = true; return true;
            }
            if (source_remaining) {
                uint8_t bytes[256]; unsigned n = std::min(256u, source_remaining);
                if (!storage.read(slot, base + position, bytes, n)) return false;
                update_crc(source_crc, bytes, n); position += n; source_remaining -= n;
                if (!source_remaining && ~source_crc != source_expected) return false;
                return true;
            }
            if (position == length) {
                done = true; return !inline_source || greatest_source < sources;
            }
            uint8_t header[32];
            if (position > length || 32 > length - position || !storage.read(slot, base + position, header, 32)) return false;
            unsigned n = u32(header);
            if (n < 76 || n > max_document || n > length - position - 4 || std::memcmp(header + 4, "CSW1", 4)
                || u16(header + 8) != document_version || u32(header + 12) != n) return false;
            source_expected = u32(header + 24); source_crc = 0xffffffff;
            source_remaining = n - 28; position += 32; ++sources;
            if (!verify_sources) { position += source_remaining; source_remaining = 0; }
            return true;
        }
        void remember_source() {
            if (!selection_value.document[0]) { inline_source = true; greatest_source = std::max(greatest_source, unsigned(selection_value.source)); }
        }
    };
    enum class Edit { append_song, rename_song, change_song, rename_set, insert, remove, move,
        insert_patch, replace_patch, remove_patch, move_patch };
    enum class Phase { idle, prepare, source_hash, source_structure, identities, identity_scan,
        hash, begin, header, write, footer, close, verify_header, verify_hash, verify_structure };
    struct Job {
        unsigned source = 0, destination = 0, length = 0, offset = 0, erased = 0, inserted_size = 0, position = 0;
        SongCounts counts;
        const Document *appended = nullptr;
        unsigned appended_length = 0;
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
        SongSelection selection, previous;
        bool song_exists = false;
        Cursor cursor, scan;
    } job_;
    Phase phase_ = Phase::idle;
    bool good_ = true;
    Storage &storage_; SongResolver &resolver_;
    SongCounts counts_;
    mutable Song song_cache_;
    mutable Setlist set_cache_;
    mutable unsigned set_offset_ = 0;
    uint32_t generation_ = 0, crc_ = checksum(reinterpret_cast<const uint8_t *>(&counts_), sizeof(counts_));
    unsigned active_ = 0, length_ = 16, undo_slot_ = 0;
    unsigned source_count_ = 0, records_end_ = 12;
    mutable unsigned verified_source_count_ = 0, verified_records_end_ = 12;
    uint32_t active_set_ = 0, current_entry_ = 0, undo_set_ = 0, undo_entry_ = 0;
    unsigned continuation_ = 0, undo_continuation_ = 0, patch_position_ = 0;
    uint32_t current_song_ = 0, current_patch_ = 0;
    bool ready_ = false, can_undo_ = false, removed_ = false, invalid_ = false;
    char current_name_[64] = {}, previous_name_[64] = {};
    SongContinuation previous_cursor_;
    unsigned previous_patch_position_ = 0, previous_continuation_ = 0;
    bool pending_function_ = false, previous_removed_ = false, resume_pending_ = false, invocation_pending_ = false;
    const char *error_ = "";
    bool activate_patch(uint32_t id, unsigned position, uint32_t entry_id) {
        SongSelection item;
        if (!patch_at(id, position, item)) { error_ = "Song patch missing"; return false; }
        SongContinuation saved{generation_, crc_, active_set_, entry_id, id, item.occurrence, 0};
        saved.caller = 1; saved.source = item.source; saved.node = item.node;
        std::snprintf(saved.document, sizeof(saved.document), "%s", item.document);
        SongSelection upcoming;
        const auto *current = song(id);
        if (!current) return false;
        if (position + 1 < current->count) {
            if (!patch_at(id, position + 1, upcoming)) return false;
            saved.next = upcoming.occurrence;
            if (upcoming.document[0] && !resolver_.revision(upcoming, saved.source_crc)) { error_ = "Source unavailable"; return false; }
        }
        previous_cursor_ = continuation(); previous_patch_position_ = patch_position_; previous_continuation_ = continuation_; previous_removed_ = removed_;
        std::memcpy(previous_name_, current_name_, sizeof(previous_name_));
        if (!resolver_.activate(item, saved, false)) { error_ = "Patch unavailable"; return false; }
        pending_function_ = resolver_.execution_status() == SongExecution::suspended;
        invocation_pending_ = executing();
        const auto *song_item = song(id); if (!song_item) return false;
        std::snprintf(current_name_, 64, "%s", song_item->name);
        current_song_ = id; current_patch_ = item.occurrence; patch_position_ = position; current_entry_ = entry_id; removed_ = false;
        error_ = resolver_.execution_status() == SongExecution::completed ? "Completed locally" : "Pending"; return true;
    }
    static bool valid_song(const Song &s) {
        return s.id && s.count && s.occurrence && text_valid(reinterpret_cast<const uint8_t *>(s.name), 64) && valid_selection(s);
    }
    static bool valid_selection(const SongSelection &s) {
        return s.occurrence && (!s.label[0] || label_valid(reinterpret_cast<const uint8_t *>(s.label), sizeof(s.label))) && (s.document[0]
            ? s.source == 0xffffffff && text_valid(reinterpret_cast<const uint8_t *>(s.document), 64) && setup_name(s.document)
                && text_valid(reinterpret_cast<const uint8_t *>(s.key), 64)
            : !s.key[0] && s.source < 0xfffffffe && s.node < 65535);
    }
    bool advance_after_return() {
        const auto *item = song(current_song_);
        if (!item) { error_ = "Song missing"; return false; }
        if (patch_position_ + 1 == item->count) { error_ = "End of song"; return true; }
        return activate_patch(current_song_, patch_position_ + 1, current_entry_);
    }
    bool source_offset(unsigned index, unsigned &offset, unsigned &length) const {
        offset = records_end_ + 4;
        uint8_t bytes[4];
        for (unsigned i = 0; offset < length_; ++i) {
            if (!payload(active_, offset, bytes, 4)) return false;
            length = u32(bytes); offset += 4;
            if (length > length_ - offset) return false;
            if (i == index) return true;
            offset += length;
        }
        return false;
    }
    bool prepare_source(SongSelection &selection) {
        if (selection.source != 0xfffffffe || selection.document[0]) return true;
        const Document *source = nullptr;
        if (!resolver_.capture_source(source) || !source || !source->valid()
            || source->length() > 0xffffffffu - 24 - length_) { finish_job(false); return false; }
        selection.source = source_count_;
        job_.appended = source; job_.appended_length = source->length() + 4;
        return true;
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
        return read_song_record(storage_, slot, (slot == 3 ? 0 : 16) + offset, data, n);
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
        Cursor cursor;
        while (!cursor.done) if (!cursor.step(storage_, slot, base, length)) return false;
        verified_source_count_ = cursor.sources; verified_records_end_ = cursor.records_end;
        return true;
    }

    bool record(unsigned slot, uint32_t &generation, unsigned &length, uint32_t &crc, SongCounts &counts) const {
        uint8_t header[16], tail[4];
        if (!storage_.read(slot, 0, header, 16) || std::memcmp(header, "CSG4", 4)) return false;
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
        if (!ready_ || invalid_ || busy() || executing() || !target) return false;
        initialize_job(); job_.source = active_; job_.length = length_; job_.counts = counts_;
        job_.edit = edit; job_.target = target; phase_ = Phase::prepare; return true;
    }
    void plan(unsigned offset, unsigned erased, const void *bytes, unsigned n) {
        job_.offset = offset; job_.erased = erased; job_.inserted_size = n;
        if (n && bytes != job_.inserted) std::memcpy(job_.inserted, bytes, n);
        job_.length = length_ - erased + n + job_.appended_length; start_hash();
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
            else { uint32_t occurrence = s.occurrence; static_cast<SongSelection &>(s) = job_.selection; s.occurrence = occurrence; }
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
        if (!ready_ || (invalid_ && source != 3) || busy() || executing() || inserted_size > sizeof(job_.inserted)) return false;
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
        std::memcpy(bytes, "CSG4", 4); put32(bytes + 4, generation_ + 1);
        put32(bytes + 8, job_.length); put32(bytes + 12, job_.crc);
    }
    bool stream(uint8_t *buffer, unsigned n) {
        for (unsigned j = 0; j < n;) {
            unsigned at = job_.position + j, take = n - j, original = at;
            const uint8_t *memory = nullptr;
            const auto &change = job_.change; const auto &move = job_.move;
            uint8_t appended_size[4];
            unsigned append_at = job_.length - job_.appended_length;
            if (job_.appended && at >= append_at) {
                unsigned relative = at - append_at;
                if (relative < 4) { put32(appended_size, job_.appended->length()); memory = appended_size + relative; take = 4 - relative; }
                else { memory = job_.appended->data() + relative - 4; take = job_.appended_length - relative; }
            }
            else if (at < 12) { memory = reinterpret_cast<const uint8_t *>(&job_.counts) + at; take = 12 - at; }
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
            if (job_.appended && at < append_at) take = std::min(take, append_at - at);
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
        source_count_ = job_.cursor.sources; records_end_ = job_.cursor.records_end;
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
            else { job_.cursor = Cursor(); job_.cursor.verify_sources = false; phase_ = Phase::identities; }
            return;
        }
        case Phase::identities:
            if (!job_.cursor.step(storage_, 3, 0, job_.length)) { finish_job(false); return; }
            if (job_.cursor.done) { start_hash(); return; }
            if (job_.cursor.item) {
                job_.matches = job_.references = 0; job_.scan = Cursor(); job_.scan.verify_sources = false; phase_ = Phase::identity_scan;
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
