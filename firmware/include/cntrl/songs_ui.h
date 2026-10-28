#pragma once
#include "songs.h"

namespace cntrl {
struct SongCapture {
    virtual ~SongCapture() = default;
    virtual bool capture(Song &song, bool reference) = 0;
    virtual void recalled() = 0;
};
class SongsUi {
public:
    SongsUi(Songs &songs, Hardware &hardware, SongCapture &capture) : songs_(songs), hardware_(hardware), capture_(capture) {}
    bool open() const { return mode_ != closed; }
    void leave() { mode_ = closed; }
    void perform() { mode_ = performance; cursor_ = 0; locked_ = true; std::snprintf(feedback_, 64, "%s", songs_.last_error()); render(); }
    void enter() { mode_ = root; cursor_ = 0; locked_ = true; render(); }
    void poll(uint8_t held) {
        if (!held) locked_ = false;
        if (mode_ == performance && !songs_.busy() && std::strcmp(feedback_, songs_.last_error())) {
            std::snprintf(feedback_, sizeof(feedback_), "%s", songs_.last_error()); render();
        }
        if (waiting_ && !songs_.busy()) {
            waiting_ = false;
            if (!songs_.success()) {
                mode_ = saved_.mode; cursor_ = saved_.cursor; insertion_ = saved_.insertion;
                added_ = saved_.added; picker_ = saved_.picker; fail();
            } else if (!feedback_[0]) std::strcpy(feedback_, "Saved");
            render();
        }
    }
    void button(unsigned button, bool down) {
        if (songs_.busy() || waiting_) return;
        saved_ = {mode_, cursor_, insertion_, added_, picker_};
        handle_button(button, down);
        if (songs_.busy()) { waiting_ = true; render(); }
    }
private:
    void handle_button(unsigned button, bool down) {
        if (!down || locked_ || !open()) return;
        uint32_t before = songs_.generation();
        feedback_[0] = 0;
        if (mode_ == naming) { name_button(button); render(); return; }
        if (button == 5) {
            mode_ = mode_ == root ? closed : mode_ == adding ? performance : mode_ == patch_actions ? patches : mode_ == patches ? song_actions : root;
            cursor_ = 0; if (open()) render(); return;
        }
        unsigned count = mode_ == root ? 5 : mode_ == sets ? songs_.counts().sets + 1
            : mode_ == library || mode_ == adding ? songs_.counts().songs
            : mode_ == editing ? 8 : mode_ == song_actions ? 3 : mode_ == patch_actions ? 5 : mode_ == patches ? (songs_.song(song_id_) ? songs_.song(song_id_)->count : 0)
            : mode_ == moving || mode_ == performance ? (current_set() ? current_set()->count : 0) : 0;
        if (button < 2 && (mode_ == library || mode_ == adding)) picker_ = songs_.browse(picker_, button == 0);
        else if (button < 2 && count) cursor_ = (cursor_ + count + (button == 0 ? -1 : 1)) % count;
        else if ((mode_ == library || mode_ == adding) && (button == 3 || button == 4)) jump(button == 3);
        else if (mode_ == performance) {
            if (button == 2) { if (!songs_.next()) std::snprintf(feedback_, 64, "%s", songs_.last_error()); else capture_.recalled(); }
            else if (button == 3) { mode_ = editing; entry_ = cursor_; cursor_ = 0; }
        } else if (mode_ == root && button == 2) {
            unsigned action = cursor_; cursor_ = 0;
            if (action == 0) mode_ = performance;
            else if (action == 1) mode_ = sets;
            else if (action == 2) mode_ = library;
            else {
                Song item;
                if (!capture_.capture(item, action == 4) || !songs_.save_song(item.name, item)) fail();
                else { mode_ = library; picker_ = 0; }
            }
        } else if (mode_ == sets && button == 2) {
            if (cursor_ == songs_.counts().sets) {
                uint32_t id = songs_.new_setlist();
                if (!id) fail(); else { songs_.choose(id); insertion_ = 0; cursor_ = 0; mode_ = adding; picker_ = 0; }
            } else { Setlist selected; if (songs_.set_at(cursor_, selected)) songs_.choose(selected.id); mode_ = performance; cursor_ = 0; }
        } else if (mode_ == library && button == 2 && count) {
            ensure_picker(); song_id_ = picker_; mode_ = song_actions; cursor_ = 0;
        } else if (mode_ == song_actions && button == 2) {
            if (!cursor_) {
                const Song *song = songs_.song(song_id_);
                if (song) begin_name(song->name, false);
                else std::strcpy(feedback_, "Song read failed");
            }
            else if (cursor_ == 2) { mode_ = patches; cursor_ = 0; }
            else {
                Song item;
                if (!capture_.capture(item, false) || !songs_.change_song(song_id_, item)) fail();
                else { mode_ = library; cursor_ = 0; }
            }
        } else if (mode_ == patches && button == 2 && count) {
            patch_cursor_ = cursor_; mode_ = patch_actions; cursor_ = 0;
        } else if (mode_ == patch_actions && button == 2) {
            bool good = false; Song captured;
            if (cursor_ < 2) {
                if (capture_.capture(captured, false)) good = cursor_ == 0 ? songs_.replace_patch(song_id_, patch_cursor_, captured) : songs_.insert_patch(song_id_, patch_cursor_, captured);
            } else if (cursor_ == 2) good = songs_.remove_patch(song_id_, patch_cursor_);
            else good = songs_.move_patch(song_id_, patch_cursor_, cursor_ == 3);
            if (!good) fail(); else { mode_ = patches; cursor_ = 0; }
        } else if (mode_ == adding && button == 2 && count) {
            ensure_picker(); const Song *found = songs_.song(picker_); if (!found) { fail(); render(); return; } Song item = *found;
            if (!songs_.insert(songs_.active_set(), insertion_, item.id)) fail();
            else { ++insertion_; ++added_; std::snprintf(feedback_, 64, "Added %.57s", item.name); }
        } else if (mode_ == editing && button == 2) {
            const Setlist *set = current_set();
            if (!set) { mode_ = sets; cursor_ = 0; }
            else if (cursor_ < 2) {
                insertion_ = cursor_ == 0 ? songs_.next_position() : set->count;
                mode_ = adding; cursor_ = added_ = 0; picker_ = 0; feedback_[0] = 0;
            } else if (cursor_ == 2) {
                if (!songs_.remove(set->id, entry_)) fail(); else { mode_ = performance; cursor_ = 0; }
            } else if (cursor_ == 3) { mode_ = moving; cursor_ = entry_; }
            else if (cursor_ == 4) begin_name(set->name, true);
            else if (cursor_ == 5) { if (!songs_.undo()) fail(); }
            else if (cursor_ == 6) { songs_.choose(set->id); mode_ = performance; cursor_ = 0; }
            else { mode_ = performance; cursor_ = 0; }
        } else if (mode_ == moving && button == 2) {
            if (!songs_.move(songs_.active_set(), entry_, cursor_)) fail(); else mode_ = performance;
        }
        if (songs_.generation() != before && !feedback_[0]) std::strcpy(feedback_, "Saved");
        render();
    }
private:
    enum Mode { closed, root, sets, library, song_actions, performance, editing, adding, moving, naming, patches, patch_actions };
    Songs &songs_; Hardware &hardware_; SongCapture &capture_;
    Mode mode_ = closed, return_mode_ = root;
    unsigned patch_cursor_ = 0;
    unsigned cursor_ = 0, entry_ = 0, insertion_ = 0, added_ = 0, character_ = 0;
    uint32_t song_id_ = 0;
    uint32_t picker_ = 0;
    bool locked_ = false, naming_set_ = false;
    struct Saved { Mode mode; unsigned cursor, insertion, added; uint32_t picker; } saved_ = {};
    bool waiting_ = false;
    char feedback_[64] = {}, name_[64] = {};
    const Setlist *current_set() const { return songs_.setlist(songs_.active_set()); }
    void fail() { std::strcpy(feedback_, "Not saved"); }
    void ensure_picker() { if (!songs_.song(picker_)) picker_ = songs_.browse(0, false); }
    void jump(bool backward) { ensure_picker(); picker_ = songs_.browse(picker_, backward, true); }
    void begin_name(const char *name, bool set) {
        std::snprintf(name_, 64, "%s", name); naming_set_ = set;
        return_mode_ = mode_; mode_ = naming; character_ = 0;
    }
    void name_button(unsigned button) {
        unsigned length = unsigned(std::strlen(name_));
        if (button == 5) { mode_ = return_mode_; return; }
        if (button == 3 && character_) --character_;
        if (button == 4 && character_ < 62) ++character_;
        if (button < 2) {
            while (length <= character_) name_[length++] = ' ';
            name_[length] = 0;
            char &c = name_[character_]; c = char(32 + (c - 32 + 95 + (button == 0 ? -1 : 1)) % 95);
        }
        if (button == 2) {
            while (length && name_[length - 1] == ' ') name_[--length] = 0;
            bool good = naming_set_ ? songs_.rename_setlist(songs_.active_set(), name_) : songs_.rename_song(song_id_, name_);
            if (!good) fail(); else { std::strcpy(feedback_, "Saved"); mode_ = return_mode_; cursor_ = 0; }
        }
    }
    void render() {
        Pane panes[3];
        for (auto &p : panes) for (auto &line : p.lines) { line.graphics.size = line.graphics.line_height = 16; line.wrap = true; }
        if (songs_.busy()) {
            std::strcpy(panes[1].lines[0].text, "Saving...");
            for (unsigned i = 0; i < 3; ++i) hardware_.show(i, panes[i]);
            return;
        }
        auto text = [&](unsigned pane, unsigned line, const char *s) { std::snprintf(panes[pane].lines[line].text, 64, "%s", s); };
        text(0, 0, songs_.removed() ? "Current (removed)" : "Current"); text(0, 1, songs_.current_name());
        SongSelection current_patch;
        if (songs_.current_patch(current_patch)) {
            text(0, 2, current_patch.label[0] ? current_patch.label : current_patch.document[0] ? current_patch.key : "Patch");
        }
        const Setlist *set = current_set(); unsigned next = songs_.next_position();
        text(2, 0, "Next");
        SongEntry next_entry;
        const Song *upcoming = set && next < set->count && songs_.entry(set->id, next, next_entry) ? songs_.song(next_entry.song) : nullptr;
        text(2, 1, upcoming ? upcoming->name : set && !set->count ? "Add songs" : "End of set");
        SongSelection next_patch;
        if (songs_.upcoming_patch(next_patch)) {
            text(2, 0, "Next patch");
            text(2, 1, next_patch.label[0] ? next_patch.label : next_patch.document[0] ? next_patch.key : "Patch");
        }
        static const char *roots[] = {"Perform setlist", "Setlists", "Songs", "Save as song", "Save reference"};
        static const char *edits[] = {"Add next", "Add to end", "Remove from setlist", "Move", "Rename setlist", "Undo edit", "Restart setlist", "Done"};
        text(1, 0, mode_ == root ? "Songs and setlists" : mode_ == naming ? "Rename" : set ? set->name : "Songs");
        if (mode_ == root) text(1, 1, roots[cursor_]);
        else if (mode_ == sets) { Setlist selected; text(1, 1, songs_.set_at(cursor_, selected) ? selected.name : "New setlist"); }
        else if (mode_ == editing) text(1, 1, edits[cursor_]);
        else if (mode_ == naming) { text(1, 1, name_); std::snprintf(panes[1].lines[2].text, 64, "Character %u", character_ + 1); }
        else if (mode_ == song_actions) { static const char *actions[] = {"Rename song", "Use selected patch", "Edit patches"}; text(1, 1, actions[cursor_]); }
        else if (mode_ == patches) {
            SongSelection selected;
            if (songs_.patch_at(song_id_, cursor_, selected)) {
                text(1, 1, selected.label[0] ? selected.label : selected.document[0] ? selected.key : "Patch");
                const Song *song = songs_.song(song_id_);
                if (song) std::snprintf(panes[1].lines[2].text, 64, "Patch %u/%lu", cursor_ + 1, (unsigned long)song->count);
                else text(1, 2, "Song read failed");
            }
        } else if (mode_ == patch_actions) { static const char *actions[] = {"Replace with selected", "Insert selected after", "Remove patch", "Move earlier", "Move later"}; text(1, 1, actions[cursor_]); }
        else if (mode_ == library || mode_ == adding) {
            ensure_picker();
            if (!songs_.counts().songs) text(1, 1, "Save a song first");
            else {
                const auto *found = songs_.song(picker_); if (!found) return; const Song s = *found;
                text(1, 1, s.name);
                std::snprintf(panes[1].lines[2].text, 64, "%lu patches #%lu", (unsigned long)s.count, (unsigned long)s.id);
            }
        } else if (set && set->count) {
            cursor_ %= set->count; SongEntry selected;
            const auto *item = songs_.entry(set->id, cursor_, selected) ? songs_.song(selected.song) : nullptr;
            text(1, 1, item ? item->name : "Missing song");
            std::snprintf(panes[1].lines[2].text, 64, "%u/%u", cursor_ + 1, unsigned(set->count));
        } else text(1, 1, "Add songs");
        text(0, 3, mode_ == performance ? "A/B entry D edit" : mode_ == naming ? "A/B letter D/E pos" : "A/B browse D/E jump");
        text(2, 3, mode_ == performance ? "C next F menu" : mode_ == adding ? "C add F done" : "C OK F back");
        if (feedback_[0]) text(0, 2, feedback_);
        for (unsigned i = 0; i < 3; ++i) {
            unsigned rows = 0;
            for (const auto &line : panes[i].lines) rows += (unsigned(std::strlen(line.text)) + 11) / 12;
            if (rows > 7) for (auto &line : panes[i].lines) line.graphics.size = line.graphics.line_height = 8;
            hardware_.show(i, panes[i]);
        }
    }
};
} // namespace cntrl
