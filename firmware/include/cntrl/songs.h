#pragma once
#include "documents.h"
#include <algorithm>

namespace cntrl {
#pragma pack(push, 1)
struct SongSelection {
    char document[64] = {}, key[64] = {}, label[64] = {};
    uint32_t source = 0xffffffff, node = 0;
};
struct Song : SongSelection {
    char name[64] = {};
    uint32_t count = 1;
};
struct SongContinuation {
    uint32_t generation = 0, crc = 0, entry = 0xffffffff, patch = 0, source_crc = 0;
    uint32_t node = 0, completed = 0, sources_crc = 0, caller = 0, source = 0xffffffff;
    char document[64] = {};
    char set[64] = {}, song[64] = {};
};
struct SongEntry { char song[64] = {}; };
struct Setlist { char name[64] = {}; uint32_t count = 0; };
struct SongCounts { uint32_t songs = 0, sets = 0; };
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
#ifdef CNTRL_NOINLINE
    CNTRL_NOINLINE
#endif
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
        if (busy()) return false;
        counts_ = {}; active_ = length_ = source_count_ = 0;
        can_undo_ = invalid_ = false;
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
            if (!commit(active_, 12, counts_, 8, 0, "CSP5", 4, nullptr, nullptr)) return false;
            while (busy()) tick(); // Initialization precedes the live main loop.
            return success();
        }
        return true;
    }
    bool song_at(unsigned index, Song &result) const {
        unsigned offset;
        return song_offset(index, offset) && payload(active_, offset, &result, sizeof(result));
    }
    bool patch_at(const char *name, unsigned index, SongSelection &result) const {
        Song item; unsigned position;
        if (!find_song(name, item, position) || index >= item.count) return false;
        if (!index) { result = item; return true; }
        unsigned offset; return song_offset(position, offset)
            && payload(active_, offset + sizeof(Song) + (index - 1) * sizeof(SongSelection), &result, sizeof(result));
    }
    bool insert_patch(const char *name, unsigned after, const SongSelection &selection) {
        if (length_ > 0xffffffffu - 20 - sizeof(SongSelection)
            || !prepare(Edit::insert_patch, name)) return false;
        job_.at = after; job_.selection = selection; return prepare_source(job_.selection);
    }
    bool replace_patch(const char *name, unsigned at, const SongSelection &selection) {
        if (!prepare(Edit::replace_patch, name)) return false;
        job_.at = at; job_.selection = selection; return prepare_source(job_.selection);
    }
    bool remove_patch(const char *name, unsigned at) {
        if (!prepare(Edit::remove_patch, name)) return false;
        job_.at = at; return true;
    }
    bool move_patch(const char *name, unsigned at, bool earlier) {
        if ((earlier && !at) || (!earlier && at == 0xffffffffu) || !prepare(Edit::move_patch, name)) return false;
        job_.at = at; job_.to = earlier ? at - 1 : at + 1; return true;
    }
    const Song *song(const char *name) const {
        if (!name[0]) return nullptr;
        if (!std::strcmp(song_cache_.name, name)) return &song_cache_;
        for (unsigned i = 0; i < counts_.songs; ++i) {
            if (!song_at(i, song_cache_)) return nullptr;
            if (!std::strcmp(song_cache_.name, name)) return &song_cache_;
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
    const Setlist *setlist(const char *name) const {
        if (!name[0]) return nullptr;
        char requested[64]; std::snprintf(requested, 64, "%s", name);
        for (unsigned i = 0; i < counts_.sets; ++i) {
            if (!set_at(i, set_cache_, &set_offset_)) return nullptr;
            if (!std::strcmp(set_cache_.name, requested)) return &set_cache_;
        }
        return nullptr;
    }
    bool entry(const char *set, unsigned index, SongEntry &result) const {
        const Setlist *s = setlist(set);
        return s && index < s->count && payload(active_, set_offset_ + sizeof(Setlist) + index * sizeof(SongEntry), &result, sizeof(result));
    }
    bool browse(const char *from, char *result, bool backward, bool letter = false) const {
        Song selected; bool has = false;
        if (const auto *s = song(from)) { selected = *s; has = true; }
        Song best, wrap; bool have_best = false, have_wrap = false;
        auto compare = [](const Song &a, const Song &b) {
            int c = name_compare(a.name, b.name); return c ? c : std::strcmp(a.name, b.name);
        };
        auto initial = [](const Song &s) { char c = s.name[0]; return c >= 'A' && c <= 'Z' ? c + 32 : c; };
        for (unsigned i = 0; i < counts_.songs; ++i) {
            Song value; if (!song_at(i, value)) return false;
            if (!have_wrap || (backward ? compare(value, wrap) > 0 : compare(value, wrap) < 0)) { wrap = value; have_wrap = true; }
            int relation = has ? compare(value, selected) : (backward ? -1 : 1);
            if ((backward ? relation < 0 : relation > 0) && (!letter || !has || initial(value) != initial(selected))
                && (!have_best || (backward ? compare(value, best) > 0 : compare(value, best) < 0))) { best = value; have_best = true; }
        }
        std::snprintf(result, 64, "%s", have_best ? best.name : have_wrap ? wrap.name : "");
        return have_best || have_wrap;
    }
    bool save_song(const char *name, const SongSelection &selection) {
        if (song(name)) { error_ = "Song name exists"; return false; }
        if (length_ > 0xffffffffu - 20 - sizeof(Song)) return false;
        Song s; static_cast<SongSelection &>(s) = selection;
        std::snprintf(s.name, 64, "%s", name);
        SongCounts counts = counts_; ++counts.songs;
        if (!prepare(Edit::append_song, name)) return false;
        if (!prepare_source(s) || !valid_song(s)) { finish_job(false); return false; }
        job_.counts = counts; std::memcpy(job_.inserted, &s, sizeof(s)); return true;
    }
    bool rename_song(const char *from, const char *name) {
        char previous[64]; std::snprintf(previous, 64, "%s", from);
        if (std::strcmp(previous, name) && song(name)) { error_ = "Song name exists"; return false; }
        if (!prepare(Edit::rename_song, previous)) return false;
        std::snprintf(job_.name, 64, "%s", name); return true;
    }
    bool change_song(const char *name, const SongSelection &selection) {
        if (!prepare(Edit::change_song, name)) return false;
        job_.selection = selection; return prepare_source(job_.selection);
    }
    bool new_setlist(char *name) {
        Setlist s;
        for (unsigned n = counts_.sets + 1;; ++n) {
            std::snprintf(s.name, 64, "Setlist %u", n);
            if (!setlist(s.name)) break;
        }
        SongCounts counts = counts_; ++counts.sets;
        if (!edit(records_end_, 0, &s, sizeof(s), counts)) return false;
        std::snprintf(name, 64, "%s", s.name); return true;
    }
    bool rename_setlist(const char *from, const char *name) {
        char previous[64]; std::snprintf(previous, 64, "%s", from);
        if (std::strcmp(previous, name) && setlist(name)) { error_ = "Setlist name exists"; return false; }
        if (!prepare(Edit::rename_set, previous)) return false;
        std::snprintf(job_.name, 64, "%s", name); return true;
    }
    bool insert(const char *set, unsigned at, const char *song_name) {
        if (!prepare(Edit::insert, set)) return false;
        job_.at = at; std::snprintf(job_.song, 64, "%s", song_name); return true;
    }
    bool remove(const char *set, unsigned at) {
        if (!prepare(Edit::remove, set)) return false;
        job_.at = at; return true;
    }
    bool move(const char *set, unsigned from, unsigned to) {
        if (!prepare(Edit::move, set)) return false;
        job_.at = from; job_.to = to; return true;
    }
    bool undo_source(unsigned &length, uint32_t &crc) {
        if (!can_undo_) { length = 0; crc = 0; return true; }
        uint8_t header[16];
        if (!storage_.read(undo_slot_, 0, header, 16) || std::memcmp(header, "CSG5", 4)) return false;
        length = u32(header + 8); crc = u32(header + 12);
        return length >= 12 && length <= 0xffffffffu - 20 && storage_.size(undo_slot_) == length + 20;
    }
    bool read_undo_payload(unsigned offset, uint8_t *data, unsigned length) {
        unsigned size = 0; uint32_t crc = 0;
        return undo_source(size, crc) && offset <= size && length <= size - offset
            && storage_.read(undo_slot_, 16 + offset, data, length);
    }
    bool undo() {
        if (busy() || !can_undo_) return false;
        uint8_t header[16];
        if (!storage_.read(undo_slot_, 0, header, 16) || std::memcmp(header, "CSG5", 4)) return false;
        unsigned length = u32(header + 8);
        if (length < 12 || length > 0xffffffffu - 20 || storage_.size(undo_slot_) != length + 20) return false;
        initialize_job(); job_.source = undo_slot_; job_.length = length; job_.crc = u32(header + 12);
        job_.undo = true;
        job_.playback = undo_cursor_; job_.removed = undo_removed_;
        phase_ = Phase::source_hash; return true;
    }
    void choose(const char *set) {
        if (executing()) { error_ = "Patch pending"; return; }
        if (busy()) { std::snprintf(job_.choose, 64, "%s", set); return; }
        current_song_[0] = 0; patch_position_ = 0;
        if (set != active_set_) std::snprintf(active_set_, 64, "%s", set);
        position_ = 0xffffffffu; removed_ = false;
    }
    unsigned next_position() const {
        const auto *p = setlist(active_set_); if (!p) return 0;
        return position_ == 0xffffffffu ? 0 : std::min(position_ + unsigned(!removed_), unsigned(p->count));
    }
    SongContinuation continuation() const {
        SongContinuation result; result.generation = generation_; result.crc = crc_;
        result.entry = position_; result.patch = patch_position_;
        std::snprintf(result.set, 64, "%s", active_set_); std::snprintf(result.song, 64, "%s", current_song_);
        return result;
    }
    void cancel_function() {
        if (!pending_function_) return;
        restore_cursor(previous_cursor_); removed_ = previous_removed_;
        pending_function_ = invocation_pending_ = false; error_ = "Firmware switch failed";
    }
    bool resume(const SongContinuation &saved) {
        if (saved.generation != generation_ || saved.crc != crc_) { error_ = "Song data changed"; return false; }
        const Song *item = song(saved.song);
        if (!item) { error_ = "Song missing"; return false; }
        SongSelection current; SongEntry entry_value;
        unsigned at = saved.patch, count = item->count;
        if (!patch_at(saved.song, at, current) || !entry(saved.set, saved.entry, entry_value)
            || std::strcmp(entry_value.song, saved.song)) return false;
        if (at + 1 < count) {
            SongSelection upcoming;
            if (!patch_at(saved.song, at + 1, upcoming)) return false;
            uint32_t revision = 0;
            if (upcoming.document[0] && (!resolver_.revision(upcoming, revision) || revision != saved.source_crc)) { error_ = "Source changed"; return false; }
        }
        if (!resolver_.activate(current, saved, true)) { error_ = "Patch return unavailable"; return false; }
        restore_cursor(saved); removed_ = false;
        resume_pending_ = true;
        if (resolver_.execution_status() == SongExecution::completed) { resume_pending_ = false; return advance_after_return(); }
        return true;
    }
    bool next() {
        if (busy() || executing()) { error_ = "Song work pending"; return false; }
        const auto *current = song(current_song_);
        if (current && !removed_) {
            if (patch_position_ == 0xffffffffu) { error_ = "Current patch removed"; return false; }
            if (patch_position_ + 1 < current->count) return activate_patch(current_song_, patch_position_ + 1, position_);
        }
        SongEntry value;
        const auto *set = setlist(active_set_);
        if (!set) { error_ = active_set_[0] ? "Setlist read failed" : "Choose a setlist"; return false; }
        unsigned count = set->count, position = next_position();
        if (position == 0xffffffffu) { error_ = "Setlist read failed"; return false; }
        if (position >= count) { error_ = "End of set"; return false; }
        if (!entry(active_set_, position, value)) { error_ = "Setlist read failed"; return false; }
        const auto *p = song(value.song); if (!p) { error_ = "Song missing"; return false; }
        return activate_patch(value.song, 0, position);
    }
    bool current_patch(SongSelection &result) const {
        return patch_at(current_song_, patch_position_, result);
    }
    bool upcoming_patch(SongSelection &result) const {
        const auto *item = song(current_song_);
        if (!item || removed_) return false;
        return patch_position_ != 0xffffffffu && patch_position_ + 1 < item->count
            && patch_at(current_song_, patch_position_ + 1, result);
    }
    const char *active_set() const { return active_set_; }
    bool executing() const {
        auto state = resolver_.execution_status();
        return state == SongExecution::pending || state == SongExecution::suspended;
    }
    bool invalid() const { return invalid_; }
    const char *current_name() const { return current_song_; }
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
    bool import_begin(unsigned length) { return !busy() && !executing() && ready_ && length >= 12 && length <= 0xffffffffu - 20 && storage_.begin(3); }
    bool import_write(const uint8_t *bytes, unsigned n) { return storage_.write(bytes, n); }
    void import_abort() { storage_.finish(); }
    bool import_finish(unsigned length, uint32_t crc) {
        if (busy() || !storage_.finish() || storage_.size(3) != length) return false;
        initialize_job(); job_.source = 3; job_.length = length; job_.crc = crc;
        job_.playback = SongContinuation(); job_.removed = false;
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
        unsigned set_count = 0;
        unsigned patches = 0, patch_index = 0;
        unsigned sources = 0, source_remaining = 0, greatest_source = 0, records_end = 8;
        unsigned source_start = 0, source_hashed = 0, extension = 0, extension_end = 0, extension_count = 0;
        uint32_t source_crc = 0xffffffff, source_expected = 0;
        bool source_envelope = false;
        bool graph_started = false, inline_source = false, verify_sources = true;
        bool first_selection = false;
        bool done = false, item = false, is_song = false, is_selection = false, is_entry = false;
#ifdef CNTRL_FLASH_ROUTINE
        CNTRL_FLASH_ROUTINE(songs_cursor_step)
#endif
        bool step(Storage &storage, unsigned slot, unsigned base, unsigned length) {
            item = false;
            if (!position) {
                if (length < 8 || !storage.read(slot, base, reinterpret_cast<uint8_t *>(&counts), 8)
                    || counts.songs > (length - 8) / sizeof(Song)) return false;
                position = 8; return true;
            }
            is_song = is_selection = is_entry = false;
            if (first_selection) {
                first_selection = false; selection_value = song_value; patch_index = 0;
                remember_source(); item = is_selection = true; return true;
            }
            if (patches) {
                if (!storage.read(slot, base + position, reinterpret_cast<uint8_t *>(&selection_value), sizeof(selection_value))
                    || !valid_selection(selection_value)) return false;
                position += sizeof(selection_value); --patches; ++patch_index;
                remember_source(); item = is_selection = true; return true;
            }
            if (song < counts.songs) {
                Song value;
                if (position > length || sizeof(value) > length - position
                    || !read_song_record(storage, slot, base + position, &value, sizeof(value))
                    || !valid_song(value)) return false;
                song_value = value; position += sizeof(value);
                if (value.count - 1 > (length - position) / sizeof(SongSelection)) return false;
                patches = value.count - 1; first_selection = true;
                ++song; is_song = item = true; return true;
            }
            if (entries) {
                SongEntry value;
                if (!storage.read(slot, base + position, reinterpret_cast<uint8_t *>(&value), sizeof(value))
                    || !text_valid(reinterpret_cast<const uint8_t *>(value.song), 64)) return false;
                entry_value = value; position += sizeof(value); --entries; ++entry;
                item = is_entry = true; return true;
            }
            if (set < counts.sets) {
                Setlist value;
                if (position > length || sizeof(value) > length - position
                    || !storage.read(slot, base + position, reinterpret_cast<uint8_t *>(&value), sizeof(value))
                    || !text_valid(reinterpret_cast<const uint8_t *>(value.name), 64)) return false;
                position += sizeof(value);
                if (value.count > (length - position) / sizeof(SongEntry)) return false;
                set_value = value; ++set; entries = set_count = value.count; entry = 0; item = true; return true;
            }
            if (!graph_started) {
                uint8_t magic[4];
                if (position > length || 4 > length - position || !storage.read(slot, base + position, magic, 4)
                    || std::memcmp(magic, "CSP5", 4)) return false;
                records_end = position; position += 4; graph_started = true; return true;
            }
            if (source_remaining) {
                uint8_t bytes[256]; unsigned n = std::min(256u, source_remaining);
                if (!storage.read(slot, base + position, bytes, n)) return false;
                if (source_envelope) {
                    for (unsigned i = 0; i < n; ++i)
                        if (source_hashed + i >= 16 && source_hashed + i < 20) bytes[i] = 0;
                    source_hashed += n;
                }
                update_crc(source_crc, bytes, n); position += n; source_remaining -= n;
                if (!source_remaining && ~source_crc != source_expected) return false;
                return true;
            }
            if (extension < extension_end) {
                uint8_t record[8];
                if (++extension_count >= 4096 || extension_end - extension < 8 || !storage.read(slot, base + extension, record, 8)
                    || !u16(record) || u16(record) == 1 || u16(record + 2)
                    || u32(record + 4) > extension_end - extension - 8) return false;
                extension += 8 + u32(record + 4); return true;
            }
            if (position == length) {
                done = true; return !inline_source || greatest_source < sources;
            }
            uint8_t header[32];
            if (position > length || 32 > length - position || !storage.read(slot, base + position, header, 32)) return false;
            unsigned n = u32(header);
            if (n >= 56 && !std::memcmp(header + 4, "CNBF", 4)) {
                uint8_t prefix[56];
                if (n > memory_budget || n > length - position - 4
                    || !storage.read(slot, base + position + 4, prefix, 56)
                    || u16(prefix + 4) != binary::development_format
                    || u16(prefix + 6) != unsigned(binary::Kind::setup)
                    || u16(prefix + 8) != 20 || u16(prefix + 10) || u32(prefix + 12) != n - 20
                    || u16(prefix + 20) != 1 || u16(prefix + 22) != 1
                    || u32(prefix + 24) < 76 || u32(prefix + 24) > n - 28
                    || std::memcmp(prefix + 28, "CSW1", 4) || u16(prefix + 32) != document_version
                    || u32(prefix + 36) != u32(prefix + 24)) return false;
                source_start = position + 4; source_hashed = 0; source_envelope = true;
                extension = source_start + 28 + u32(prefix + 24); extension_end = source_start + n;
                extension_count = 0;
                source_expected = u32(prefix + 16); source_crc = 0xffffffff;
                source_remaining = n; position += 4; ++sources;
                if (!verify_sources) { position += source_remaining; source_remaining = 0; }
                return true;
            }
            if (n < 76 || n > memory_budget || n > length - position - 4 || std::memcmp(header + 4, "CSW1", 4)
                || u16(header + 8) != document_version || u32(header + 12) != n) return false;
            source_expected = u32(header + 24); source_crc = 0xffffffff;
            source_envelope = false; extension = extension_end = 0;
            source_remaining = n - 28; position += 32; ++sources;
            if (!verify_sources) { position += source_remaining; source_remaining = 0; }
            return true;
        }
        void remember_source() {
            if (!selection_value.document[0]) { inline_source = true; greatest_source = std::max(greatest_source, unsigned(selection_value.source)); }
        }
    };
    enum class Edit { none, append_song, rename_song, change_song, rename_set, insert, remove, move,
        insert_patch, replace_patch, remove_patch, move_patch };
    enum class Phase { idle, prepare, source_hash, source_structure,
        hash, begin, header, write, footer, close, verify_header, verify_hash, verify_structure };
    struct Job {
        unsigned source = 0, destination = 0, length = 0, offset = 0, erased = 0, inserted_size = 0, position = 0;
        SongCounts counts;
        const Document *appended = nullptr;
        unsigned appended_length = 0;
        uint8_t inserted[2 * sizeof(SongSelection)] = {};
        HeaderChange change = {}; Move move = {};
        bool has_change = false, has_move = false, undo = false, opened = false;
        uint32_t crc = 0, running_crc = 0xffffffff;
        SongContinuation playback;
        bool removed = false;
        char choose[64] = {};
        Edit edit = Edit::none;
        char target[64] = {}, song[64] = {};
        unsigned at = 0, to = 0;
        char name[64] = {};
        SongSelection selection, previous;
        Cursor cursor;
    } job_;
    Phase phase_ = Phase::idle;
    bool good_ = true;
    Storage &storage_; SongResolver &resolver_;
    SongCounts counts_;
    mutable Song song_cache_;
    mutable Setlist set_cache_;
    mutable unsigned set_offset_ = 0;
    uint32_t generation_ = 0, crc_ = checksum(reinterpret_cast<const uint8_t *>(&counts_), sizeof(counts_));
    unsigned active_ = 0, length_ = 12, undo_slot_ = 0;
    unsigned source_count_ = 0, records_end_ = 8;
    mutable unsigned verified_source_count_ = 0, verified_records_end_ = 8;
    char active_set_[64] = {}, current_song_[64] = {};
    unsigned position_ = 0xffffffffu, patch_position_ = 0;
    SongContinuation undo_cursor_;
    bool undo_removed_ = false;
    bool ready_ = false, can_undo_ = false, removed_ = false, invalid_ = false;
    SongContinuation previous_cursor_;
    bool pending_function_ = false, previous_removed_ = false, resume_pending_ = false, invocation_pending_ = false;
    const char *error_ = "";
    void restore_cursor(const SongContinuation &cursor) {
        std::snprintf(active_set_, 64, "%s", cursor.set);
        std::snprintf(current_song_, 64, "%s", cursor.song);
        position_ = cursor.entry; patch_position_ = cursor.patch;
    }
    static void move_position(uint32_t &position, unsigned from, unsigned to) {
        if (position == from) position = to;
        else if (from < position && position <= to) --position;
        else if (to <= position && position < from) ++position;
    }
    bool activate_patch(const char *name, unsigned position, unsigned entry_index) {
        SongSelection item;
        if (!patch_at(name, position, item)) { error_ = "Song patch missing"; return false; }
        SongContinuation saved = continuation(); saved.entry = entry_index; saved.patch = position;
        std::snprintf(saved.song, 64, "%s", name);
        saved.caller = 1; saved.source = item.source; saved.node = item.node;
        std::snprintf(saved.document, sizeof(saved.document), "%s", item.document);
        SongSelection upcoming;
        const auto *current = song(name);
        if (!current) return false;
        if (position + 1 < current->count) {
            if (!patch_at(name, position + 1, upcoming)) return false;
            if (upcoming.document[0] && !resolver_.revision(upcoming, saved.source_crc)) { error_ = "Source unavailable"; return false; }
        }
        previous_cursor_ = continuation(); previous_removed_ = removed_;
        if (!resolver_.activate(item, saved, false)) { error_ = "Patch unavailable"; return false; }
        pending_function_ = resolver_.execution_status() == SongExecution::suspended;
        invocation_pending_ = executing();
        std::memcpy(current_song_, saved.song, 64);
        patch_position_ = position; position_ = entry_index; removed_ = false;
        error_ = resolver_.execution_status() == SongExecution::completed ? "Completed locally" : "Pending"; return true;
    }
    static bool valid_song(const Song &s) {
        return s.count && text_valid(reinterpret_cast<const uint8_t *>(s.name), 64) && valid_selection(s);
    }
    static bool valid_selection(const SongSelection &s) {
        return (!s.label[0] || label_valid(reinterpret_cast<const uint8_t *>(s.label), sizeof(s.label))) && (s.document[0]
            ? s.source == 0xffffffff && text_valid(reinterpret_cast<const uint8_t *>(s.document), 64) && setup_name(s.document)
                && label_valid(reinterpret_cast<const uint8_t *>(s.key), 64)
            : !s.key[0] && s.source < 0xfffffffe && s.node < 65535);
    }
    bool advance_after_return() {
        const auto *item = song(current_song_);
        if (!item) { error_ = "Song missing"; return false; }
        if (patch_position_ + 1 == item->count) { error_ = "End of song"; return true; }
        return activate_patch(current_song_, patch_position_ + 1, position_);
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
    bool find_song(const char *name, Song &s, unsigned &index) const {
        for (index = 0; index < counts_.songs; ++index)
            if (!song_at(index, s)) return false; else if (!std::strcmp(s.name, name)) return true;
        return false;
    }
    bool song_offset(unsigned index, unsigned &offset) const {
        offset = 8;
        if (index >= counts_.songs) return false;
        for (unsigned i = 0; i < index; ++i) {
            Song item; if (!payload(active_, offset, &item, sizeof(item))) return false;
            offset += sizeof(Song) + (item.count - 1) * sizeof(SongSelection);
        }
        return true;
    }
    bool songs_end(unsigned &offset) const {
        offset = 8;
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
        if (!storage_.read(slot, 0, header, 16) || std::memcmp(header, "CSG5", 4)) return false;
        length = u32(header + 8); crc = u32(header + 12); generation = u32(header + 4);
        uint32_t actual;
        return length <= 0xffffffffu - 20 && storage_.size(slot) == length + 20
            && storage_.read(slot, 16 + length, tail, 4) && !std::memcmp(tail, "DONE", 4)
            && crc_range(slot, 16, length, actual) && actual == crc && validate(slot, length, 16)
            && storage_.read(slot, 16, reinterpret_cast<uint8_t *>(&counts), 8);
    }
    bool edit(unsigned offset, unsigned erased, const void *bytes, unsigned n, const SongCounts &counts) {
        if (offset > length_ || erased > length_ - offset || n > 0xffffffffu - 20 - (length_ - erased)) return false;
        return commit(active_, length_ - erased + n, counts, offset, erased, bytes, n, nullptr, nullptr);
    }
    bool prepare(Edit edit, const char *target) {
        if (!ready_ || invalid_ || busy() || executing() || !target[0]) return false;
        initialize_job(); job_.source = active_; job_.length = length_; job_.counts = counts_;
        job_.edit = edit; std::snprintf(job_.target, 64, "%s", target); phase_ = Phase::prepare; return true;
    }
    void plan(unsigned offset, unsigned erased, const void *bytes, unsigned n) {
        job_.offset = offset; job_.erased = erased; job_.inserted_size = n;
        if (n && bytes != job_.inserted) std::memcpy(job_.inserted, bytes, n);
        job_.length = length_ - erased + n + job_.appended_length; start_hash();
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(songs_prepare_step)
#endif
    void prepare_step() {
        auto &c = job_.cursor;
        if (job_.edit == Edit::append_song && c.position && c.song == counts_.songs && !c.first_selection && !c.patches) {
            plan(c.position, 0, job_.inserted, sizeof(Song)); return;
        }
        if (!c.step(storage_, active_, 16, length_) || c.done) { finish_job(false); return; }
        if (!c.item) return;
        if (patch_edit() && !std::strcmp(c.song_value.name, job_.target)) { prepare_patch(c); return; }
        if (c.is_selection) return;
        if (c.is_song) {
            if (std::strcmp(c.song_value.name, job_.target) || (job_.edit != Edit::rename_song && job_.edit != Edit::change_song)) return;
            Song s = c.song_value;
            if (job_.edit == Edit::rename_song) { std::memset(s.name, 0, 64); std::snprintf(s.name, 64, "%s", job_.name); }
            else static_cast<SongSelection &>(s) = job_.selection;
            if (!valid_song(s)) { finish_job(false); return; }
            if (job_.edit == Edit::rename_song && !std::strcmp(job_.playback.song, job_.target))
                std::snprintf(job_.playback.song, 64, "%s", job_.name);
            plan(c.position - sizeof(Song), sizeof(Song), &s, sizeof(s)); return;
        }
        if (std::strcmp(c.set_value.name, job_.target)) return;
        if (!c.is_entry) {
            if (job_.edit == Edit::rename_set) {
                Setlist s = c.set_value; std::memset(s.name, 0, 64); std::snprintf(s.name, 64, "%s", job_.name);
                if (!text_valid(reinterpret_cast<const uint8_t *>(s.name), 64)) { finish_job(false); return; }
                if (!std::strcmp(job_.playback.set, job_.target)) std::snprintf(job_.playback.set, 64, "%s", job_.name);
                plan(c.position - sizeof(Setlist), sizeof(Setlist), &s, sizeof(s)); return;
            }
            if (job_.edit != Edit::insert && job_.edit != Edit::remove && job_.edit != Edit::move) return;
            if (job_.at > c.set_count || (job_.edit != Edit::insert && job_.at == c.set_count)
                || (job_.edit == Edit::move && job_.to >= c.set_count)) { finish_job(false); return; }
            job_.change = HeaderChange(); job_.change.offset = c.position - unsigned(sizeof(Setlist)); job_.change.value = c.set_value;
            if (job_.edit == Edit::insert) {
                if (length_ > 0xffffffffu - 20 - sizeof(SongEntry)) { finish_job(false); return; }
                ++job_.change.value.count; job_.has_change = true;
                SongEntry e; std::snprintf(e.song, 64, "%s", job_.song);
                if (!std::strcmp(active_set_, job_.target) && position_ != 0xffffffffu
                    && (job_.at < position_ || (!removed_ && job_.at == position_))) ++job_.playback.entry;
                plan(c.position + job_.at * sizeof(SongEntry), 0, &e, sizeof(e));
            } else if (job_.edit == Edit::move && job_.at == job_.to) finish_job(true);
            return;
        }
        if (c.entry != job_.at + 1) return;
        if (job_.edit == Edit::remove) {
            --job_.change.value.count; job_.has_change = true;
            if (!std::strcmp(active_set_, job_.target) && position_ != 0xffffffffu) {
                if (job_.at == position_) job_.removed = true;
                else if (job_.at < position_) --job_.playback.entry;
            }
            plan(c.position - sizeof(SongEntry), sizeof(SongEntry), nullptr, 0);
        } else if (job_.edit == Edit::move) {
            job_.has_move = true;
            job_.move = {job_.change.offset + unsigned(sizeof(Setlist)), job_.at, job_.to, c.entry_value};
            if (!std::strcmp(active_set_, job_.target) && position_ != 0xffffffffu) {
                if (removed_) {
                    if (job_.at < job_.playback.entry) --job_.playback.entry;
                    if (job_.to < job_.playback.entry) ++job_.playback.entry;
                } else move_position(job_.playback.entry, job_.at, job_.to);
            }
            start_hash();
        }
    }
    bool patch_edit() const {
        return job_.edit == Edit::insert_patch || job_.edit == Edit::replace_patch
            || job_.edit == Edit::remove_patch || job_.edit == Edit::move_patch;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(songs_prepare_patch)
#endif
    void prepare_patch(const Cursor &c) {
        if (c.is_song) {
            if (job_.at >= c.song_value.count || (job_.edit == Edit::remove_patch && c.song_value.count == 1)
                || (job_.edit == Edit::move_patch && job_.to >= c.song_value.count)) { finish_job(false); return; }
            job_.change = HeaderChange(); job_.change.offset = c.position - sizeof(Song);
            job_.change.song = true; job_.change.song_value = c.song_value;
            if (job_.edit == Edit::insert_patch) {
                auto value = job_.selection;
                if (!valid_selection(value)) { finish_job(false); return; }
                job_.has_change = true; ++job_.change.song_value.count;
                if (!std::strcmp(current_song_, job_.target) && patch_position_ != 0xffffffffu && job_.at < patch_position_) ++job_.playback.patch;
                plan(c.position + job_.at * sizeof(SongSelection), 0, &value, sizeof(value));
            }
            return;
        }
        if (!c.is_selection) return;
        if (job_.edit == Edit::replace_patch && c.patch_index == job_.at) {
            auto value = job_.selection;
            if (!valid_selection(value)) { finish_job(false); return; }
            if (!job_.at) {
                auto s = c.song_value; static_cast<SongSelection &>(s) = value;
                plan(job_.change.offset, sizeof(Song), &s, sizeof(s));
            } else plan(c.position - sizeof(SongSelection), sizeof(SongSelection), &value, sizeof(value));
        } else if (job_.edit == Edit::remove_patch && c.patch_index == (job_.at ? job_.at : 1)) {
            job_.has_change = true; --job_.change.song_value.count;
            if (!std::strcmp(current_song_, job_.target) && patch_position_ != 0xffffffffu) {
                if (job_.at == patch_position_) job_.playback.patch = 0xffffffffu;
                else if (job_.at < patch_position_) --job_.playback.patch;
            }
            if (!job_.at) static_cast<SongSelection &>(job_.change.song_value) = c.selection_value;
            plan(job_.change.offset + sizeof(Song) + (job_.at ? job_.at - 1 : 0) * sizeof(SongSelection), sizeof(SongSelection), nullptr, 0);
        } else if (job_.edit == Edit::move_patch) {
            unsigned first = std::min(job_.at, job_.to);
            if (c.patch_index == first) job_.previous = c.selection_value;
            if (c.patch_index != first + 1) return;
            if (!std::strcmp(current_song_, job_.target) && patch_position_ != 0xffffffffu)
                move_position(job_.playback.patch, job_.at, job_.to);
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
        job_ = Job(); job_.playback = continuation(); job_.removed = removed_; good_ = false; error_ = "";
        while (job_.destination == active_ || (can_undo_ && job_.destination == undo_slot_)) ++job_.destination;
    }
    void finish_job(bool good) {
        if (job_.opened) { storage_.finish(); job_.opened = false; }
        good_ = good; if (!good) error_ = "Not saved";
        phase_ = Phase::idle;
    }
    void header(uint8_t *bytes) const {
        std::memcpy(bytes, "CSG5", 4); put32(bytes + 4, generation_ + 1);
        put32(bytes + 8, job_.length); put32(bytes + 12, job_.crc);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(songs_stream_edit)
#endif
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
            else if (at < 8) { memory = reinterpret_cast<const uint8_t *>(&job_.counts) + at; take = 8 - at; }
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
            if (job_.edit == Edit::rename_song && !memory && original < records_end_) {
                auto &c = job_.cursor;
                while (!c.done && (!c.item || c.position <= original))
                    if (!c.step(storage_, job_.source, 16, length_)) return false;
                if (c.is_entry && !std::strcmp(c.entry_value.song, job_.target)) {
                    unsigned start = c.position - sizeof(SongEntry);
                    if (original < start) take = std::min(take, start - original);
                    else { memory = reinterpret_cast<const uint8_t *>(job_.name) + original - start; take = std::min(take, c.position - original); }
                } else if (c.item && original < c.position) take = std::min(take, c.position - original);
            }
            if (job_.appended && at < append_at) take = std::min(take, append_at - at);
            if (memory) std::memcpy(buffer + j, memory, take);
            else if (!payload(job_.source, original, buffer + j, take)) return false;
            j += take;
        }
        return true;
    }
    void start_hash() {
        job_.position = 0; job_.running_crc = 0xffffffff; job_.cursor = Cursor(); job_.cursor.verify_sources = false; phase_ = Phase::hash;
    }
    void publish() {
        undo_slot_ = active_; undo_cursor_ = continuation(); undo_removed_ = removed_;
        can_undo_ = generation_ != 0 && !job_.undo;
        active_ = job_.destination; ++generation_; length_ = job_.length; crc_ = job_.crc; counts_ = job_.counts;
        source_count_ = job_.cursor.sources; records_end_ = job_.cursor.records_end;
        song_cache_ = Song(); set_cache_ = Setlist(); invalid_ = false;
        restore_cursor(job_.playback); removed_ = job_.removed;
        if (job_.choose[0]) {
            current_song_[0] = 0; patch_position_ = 0;
            std::snprintf(active_set_, 64, "%s", job_.choose); position_ = 0xffffffffu; removed_ = false;
        }
        finish_job(true);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(songs_edit_step)
#endif
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
            if (!c.done) return;
            if (!source) { publish(); return; }
            job_.counts = c.counts;
            start_hash();
            return;
        }
        case Phase::hash:
        case Phase::write: {
            // Renaming also walks records to rewrite setlist references.
            unsigned chunk = job_.edit == Edit::rename_song ? 128u : 256u;
            unsigned n = std::min(chunk, job_.length - job_.position);
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
            job_.position = 0; job_.running_crc = 0xffffffff; job_.cursor = Cursor(); job_.cursor.verify_sources = false; phase_ = Phase::write; return;
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
