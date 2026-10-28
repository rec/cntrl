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
        if (!found) return commit(active_, 12, counts_, 0, 0, nullptr, 0, nullptr, nullptr);
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
        Song item; unsigned position, offset;
        if (!find_song(id, item, position) || after >= item.count || !song_offset(position, offset) || counts_.next_id == 0xffffffff || length_ > 0xffffffffu - 20 - sizeof(SongSelection)) return false;
        SongSelection value = selection; value.occurrence = counts_.next_id;
        if (!valid_selection(value)) return false;
        SongCounts counts = counts_; ++counts.next_id;
        HeaderChange header{}; header.offset = offset; header.song = true; header.song_value = item; ++header.song_value.count;
        return commit(active_, length_ + sizeof(value), counts,
            offset + sizeof(Song) + after * sizeof(value), 0, &value, sizeof(value), &header, nullptr);
    }
    bool replace_patch(uint32_t id, unsigned at, const SongSelection &selection) {
        Song item; unsigned position, offset;
        if (!find_song(id, item, position) || at >= item.count || !song_offset(position, offset)) return false;
        SongSelection previous; if (!patch_at(id, at, previous)) return false;
        SongSelection value = selection; value.occurrence = previous.occurrence;
        if (!valid_selection(value)) return false;
        if (!at) { static_cast<SongSelection &>(item) = value; return edit(offset, sizeof(item), &item, sizeof(item), counts_); }
        return edit(offset + sizeof(Song) + (at - 1) * sizeof(value), sizeof(value), &value, sizeof(value), counts_);
    }
    bool remove_patch(uint32_t id, unsigned at) {
        Song item; unsigned position, offset;
        if (!find_song(id, item, position) || item.count <= 1 || at >= item.count || !song_offset(position, offset)) return false;
        HeaderChange header{}; header.offset = offset; header.song = true; header.song_value = item; --header.song_value.count;
        if (!at && !patch_at(id, 1, header.song_value)) return false;
        return commit(active_, length_ - sizeof(SongSelection), counts_, offset + sizeof(Song) + (at ? at - 1 : 0) * sizeof(SongSelection), sizeof(SongSelection), nullptr, 0, &header, nullptr);
    }
    bool move_patch(uint32_t id, unsigned at, bool earlier) {
        Song item; unsigned position, offset;
        if (!find_song(id, item, position) || at >= item.count || (earlier ? !at : at + 1 >= item.count) || !song_offset(position, offset)) return false;
        unsigned first = earlier ? at - 1 : at;
        SongSelection values[2]; if (!patch_at(id, first + 1, values[0]) || !patch_at(id, first, values[1])) return false;
        if (!first) {
            HeaderChange header{}; header.offset = offset; header.song = true; header.song_value = item;
            static_cast<SongSelection &>(header.song_value) = values[0];
            return commit(active_, length_, counts_, offset + sizeof(Song), sizeof(SongSelection), &values[1], sizeof(SongSelection), &header, nullptr);
        }
        return edit(offset + sizeof(Song) + (first - 1) * sizeof(SongSelection), sizeof(values), values, sizeof(values), counts_);
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
        if (counts_.next_id >= 0xfffffffe) return false;
        Song s; s.id = counts_.next_id; s.occurrence = counts_.next_id + 1; s.patch = patch; s.bank = bank;
        if (document[0]) { s.patch = SongPatch(); std::strcpy(s.patch.output, "din"); s.patch.program = patch.program; }
        std::snprintf(s.name, 64, "%s", name); std::snprintf(s.document, 64, "%s", document); std::snprintf(s.instrument, 32, "%s", instrument);
        if (!valid_song(s)) return false;
        SongCounts counts = counts_; counts.next_id += 2; ++counts.songs;
        unsigned offset; return songs_end(offset) && edit(offset, 0, &s, sizeof(s), counts);
    }
    bool rename_song(uint32_t id, const char *name) {
        Song s; unsigned index;
        if (!find_song(id, s, index)) return false;
        std::memset(s.name, 0, 64); std::snprintf(s.name, 64, "%s", name);
        unsigned offset; return valid_song(s) && song_offset(index, offset) && edit(offset, sizeof(s), &s, sizeof(s), counts_);
    }
    bool change_song(uint32_t id, const SongPatch &patch) {
        Song s; unsigned index; if (!find_song(id, s, index)) return false;
        s.patch = patch; std::memset(s.document, 0, 64); std::memset(s.instrument, 0, 32); s.bank = 0xffffffff;
        unsigned offset; return valid_song(s) && song_offset(index, offset) && edit(offset, sizeof(s), &s, sizeof(s), counts_);
    }
    uint32_t new_setlist() {
        if (counts_.next_id == 0xffffffff) return 0;
        Setlist s; s.id = counts_.next_id; std::snprintf(s.name, 64, "Setlist %lu", (unsigned long)s.id);
        SongCounts counts = counts_; ++counts.next_id; ++counts.sets;
        return edit(length_, 0, &s, sizeof(s), counts) ? s.id : 0;
    }
    bool rename_setlist(uint32_t id, const char *name) {
        const auto *p = setlist(id); if (!p) return false; Setlist s = *p;
        unsigned offset = set_offset_; std::memset(s.name, 0, 64); std::snprintf(s.name, 64, "%s", name);
        return text_valid(reinterpret_cast<const uint8_t *>(s.name), 64) && edit(offset, sizeof(s), &s, sizeof(s), counts_);
    }
    bool insert(uint32_t set, unsigned at, uint32_t song_id) {
        if (!song(song_id) || counts_.next_id == 0xffffffff) return false;
        const auto *p = setlist(set); if (!p || at > p->count) return false;
        Setlist s = *p; unsigned offset = set_offset_; ++s.count;
        SongEntry value{counts_.next_id, song_id}; SongCounts counts = counts_; ++counts.next_id;
        unsigned continuation = continuation_;
        if (!edit_entries(offset, s, at, 0, &value, 1, counts)) return false;
        if (set == active_set_ && removed_) continuation_ = continuation + (at < continuation ? 1 : 0);
        return true;
    }
    bool remove(uint32_t set, unsigned at) {
        SongEntry value; if (!entry(set, at, value)) return false;
        Setlist s = set_cache_; unsigned offset = set_offset_; --s.count;
        bool current = set == active_set_ && value.id == current_entry_; unsigned continuation = continuation_;
        if (!edit_entries(offset, s, at, 1, nullptr, 0, counts_)) return false;
        if (current) { continuation_ = at; removed_ = true; }
        else if (set == active_set_ && removed_) continuation_ = continuation - (at < continuation ? 1 : 0);
        return true;
    }
    bool move(uint32_t set, unsigned from, unsigned to) {
        const auto *p = setlist(set); if (!p || from >= p->count || to >= p->count) return false;
        if (from == to) return true;
        SongEntry value; if (!entry(set, from, value)) return false;
        // Stream the shifted range; the moved entry is the only buffered entry.
        unsigned offset = set_offset_ + sizeof(Setlist), continuation = continuation_;
        Move move{offset, from, to, value};
        if (!commit(active_, length_, counts_, 0, 0, nullptr, 0, nullptr, &move)) return false;
        if (set == active_set_ && removed_) {
            if (from < continuation) --continuation;
            if (to < continuation) ++continuation;
            continuation_ = continuation;
        }
        return true;
    }
    bool undo() {
        if (!can_undo_) return false;
        uint32_t generation, crc; unsigned length; SongCounts counts;
        if (!record(undo_slot_, generation, length, crc, counts)) return false;
        counts.next_id = std::max(counts.next_id, counts_.next_id);
        unsigned continuation = undo_continuation_; bool restore = active_set_ == undo_set_ && current_entry_ == undo_entry_;
        if (!commit(undo_slot_, length, counts, 0, 0, nullptr, 0, nullptr, nullptr)) return false;
        can_undo_ = false; if (restore) continuation_ = continuation; reconcile(); return true;
    }
    void choose(uint32_t set) { current_song_ = current_patch_ = patch_position_ = 0; active_set_ = set; current_entry_ = 0; continuation_ = 0; removed_ = false; current_name_[0] = 0; }
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
    bool import_begin(unsigned length) { return ready_ && length >= 12 && length <= 0xffffffffu - 20 && storage_.begin(3); }
    bool import_write(const uint8_t *bytes, unsigned n) { return storage_.write(bytes, n); }
    bool import_finish(unsigned length, uint32_t crc) {
        if (!storage_.finish() || storage_.size(3) != length) return false;
        uint32_t actual; if (!crc_range(3, 0, length, actual) || actual != crc || !validate(3, length, 0) || !validate_import(length)) return false;
        SongCounts counts; if (!storage_.read(3, 0, reinterpret_cast<uint8_t *>(&counts), sizeof(counts))) return false;
        return commit(3, length, counts, 0, 0, nullptr, 0, nullptr, nullptr);
    }
private:
    struct HeaderChange { unsigned offset = 0; Setlist value; bool song = false; Song song_value; };
    struct Move { unsigned offset, from, to; SongEntry value; };
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
    bool find_song(uint32_t id, Song &s, unsigned &index) const {
        for (index = 0; index < counts_.songs; ++index) if (!song_at(index, s)) return false; else if (s.id == id) return true;
        return false;
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
    // Imports are untrusted. Validate identities using repeated bounded scans;
    // normal edits allocate fresh IDs and preserve existing references.
    bool validate_import(unsigned length) const {
        SongCounts counts; if (!storage_.read(3, 0, reinterpret_cast<uint8_t *>(&counts), 12)) return false;
        auto scan = [&](auto visit) {
            unsigned offset = 12;
            for (unsigned i = 0; i < counts.songs; ++i) {
                Song item; if (!storage_.read(3, offset, reinterpret_cast<uint8_t *>(&item), sizeof(item)) || !visit(item.id, true, 0) || !visit(item.occurrence, false, 0)) return false;
                offset += sizeof(item);
                for (unsigned j = 1; j < item.count; ++j, offset += sizeof(SongSelection)) {
                    SongSelection value; if (!storage_.read(3, offset, reinterpret_cast<uint8_t *>(&value), sizeof(value)) || !visit(value.occurrence, false, 0)) return false;
                }
            }
            for (unsigned i = 0; i < counts.sets; ++i) {
                Setlist set; if (!storage_.read(3, offset, reinterpret_cast<uint8_t *>(&set), sizeof(set)) || !visit(set.id, false, 0)) return false;
                offset += sizeof(set);
                for (unsigned j = 0; j < set.count; ++j, offset += sizeof(SongEntry)) {
                    SongEntry entry; if (!storage_.read(3, offset, reinterpret_cast<uint8_t *>(&entry), sizeof(entry)) || !visit(entry.id, false, entry.song)) return false;
                }
            }
            return offset == length;
        };
        return scan([&](uint32_t id, bool, uint32_t reference) {
            unsigned matches = 0, references = 0;
            return scan([&](uint32_t other, bool song, uint32_t) { matches += other == id; references += other == reference && song; return true; })
                && matches == 1 && (!reference || references == 1);
        });
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
    void reconcile() {
        const auto *s = setlist(active_set_); if (!s) { active_set_ = current_entry_ = 0; return; }
        unsigned count = s->count, offset = set_offset_ + sizeof(Setlist); removed_ = current_entry_ != 0;
        for (unsigned i = 0; i < count; ++i) { SongEntry e; if (!payload(active_, offset + i * sizeof(SongEntry), &e, sizeof(e))) break; if (e.id == current_entry_) removed_ = false; }
        continuation_ = std::min(continuation_, count);
    }
    bool edit(unsigned offset, unsigned erased, const void *bytes, unsigned n, const SongCounts &counts) {
        if (offset > length_ || erased > length_ - offset || n > 0xffffffffu - 20 - (length_ - erased)) return false;
        return commit(active_, length_ - erased + n, counts, offset, erased, bytes, n, nullptr, nullptr);
    }
    bool edit_entries(unsigned offset, const Setlist &set, unsigned at, unsigned erased, const SongEntry *entries, unsigned n, const SongCounts &counts) {
        HeaderChange header{}; header.offset = offset; header.value = set;
        unsigned position = offset + sizeof(Setlist) + at * sizeof(SongEntry);
        if (n && length_ > 0xffffffffu - 20 - sizeof(SongEntry)) return false;
        return commit(active_, length_ - erased * sizeof(SongEntry) + n * sizeof(SongEntry), counts,
            position, erased * sizeof(SongEntry), entries, n * sizeof(SongEntry), &header, nullptr);
    }
    bool commit(unsigned source, unsigned length, const SongCounts &counts, unsigned offset, unsigned erased,
                const void *inserted, unsigned inserted_size, const HeaderChange *change, const Move *move) {
        if (!ready_ || (invalid_ && source != 3)) return false;
        unsigned destination = 0;
        while (destination == active_ || (can_undo_ && destination == undo_slot_)) ++destination;
        // Emit the variable-length payload in bounded chunks, once for its CRC,
        // then to the inactive SD file. Edits never buffer the library or list.
        auto stream = [&](bool write, uint32_t &crc) {
            uint8_t buffer[256];
            for (unsigned pos = 0; pos < length;) {
                unsigned n = std::min(unsigned(sizeof(buffer)), length - pos);
                for (unsigned j = 0; j < n;) {
                    unsigned at = pos + j, take; const uint8_t *memory = nullptr; unsigned original = at;
                    if (at < 12) { memory = reinterpret_cast<const uint8_t *>(&counts) + at; take = 12 - at; }
                    else if (change && at >= change->offset && at < change->offset + (change->song ? sizeof(Song) : sizeof(Setlist))) {
                        memory = (change->song ? reinterpret_cast<const uint8_t *>(&change->song_value) : reinterpret_cast<const uint8_t *>(&change->value)) + at - change->offset; take = change->offset + (change->song ? sizeof(Song) : sizeof(Setlist)) - at;
                    } else if (inserted && at >= offset && at < offset + inserted_size) {
                        memory = static_cast<const uint8_t *>(inserted) + at - offset; take = offset + inserted_size - at;
                    } else {
                        take = n - j;
                        if (inserted || erased) {
                            if (at < offset) take = std::min(take, offset - at);
                            else original = at - inserted_size + erased;
                        }
                        if (change && at < change->offset) take = std::min(take, change->offset - at);
                        if (move && at >= move->offset) {
                            unsigned index = (at - move->offset) / sizeof(SongEntry), byte = (at - move->offset) % sizeof(SongEntry);
                            if (index == move->to) { memory = reinterpret_cast<const uint8_t *>(&move->value) + byte; take = sizeof(SongEntry) - byte; }
                            else if (index >= std::min(move->from, move->to) && index <= std::max(move->from, move->to)) {
                                original = move->offset + (move->from < move->to ? index + 1 : index - 1) * sizeof(SongEntry) + byte;
                                take = sizeof(SongEntry) - byte;
                            } else if (index < std::min(move->from, move->to)) take = std::min(take, move->offset + std::min(move->from, move->to) * unsigned(sizeof(SongEntry)) - at);
                        } else if (move && at < move->offset) take = std::min(take, move->offset - at);
                    }
                    take = std::min(take, n - j);
                    if (memory) std::memcpy(buffer + j, memory, take);
                    else if (!payload(source, original, buffer + j, take)) return false;
                    j += take;
                }
                update_crc(crc, buffer, n); if (write && !storage_.write(buffer, n)) return false; pos += n;
            }
            return true;
        };
        uint32_t crc = 0xffffffff; if (!stream(false, crc)) return false; crc = ~crc;
        uint8_t header[16]; std::memcpy(header, "CSG3", 4); put32(header + 4, generation_ + 1); put32(header + 8, length); put32(header + 12, crc);
        if (!storage_.begin(destination)) return false;
        uint32_t verify_crc = 0xffffffff;
        bool good = storage_.write(header, 16) && stream(true, verify_crc) && ~verify_crc == crc
            && storage_.write(reinterpret_cast<const uint8_t *>("DONE"), 4);
        good = storage_.finish() && good;
        uint32_t generation, stored_crc; unsigned stored_length; SongCounts stored_counts;
        if (!good || !record(destination, generation, stored_length, stored_crc, stored_counts)
            || generation != generation_ + 1 || stored_length != length || stored_crc != crc) return false;
        undo_slot_ = active_; undo_set_ = active_set_; undo_entry_ = current_entry_; undo_continuation_ = continuation_;
        can_undo_ = generation_ != 0; active_ = destination; ++generation_; length_ = length; crc_ = crc; counts_ = counts;
        song_cache_ = Song(); set_cache_ = Setlist();
        invalid_ = false; reconcile(); return true;
    }
};
} // namespace cntrl
