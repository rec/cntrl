#pragma once
#include "ui/messages.h"
#include "ui/fonts.h"
#include "ui/presentation.h"
#include "ui/prompts.h"
#include "ui/utf8.h"
#include "songs.h"
#include <algorithm>

namespace cntrl {
struct SongCapture {
    virtual ~SongCapture() = default;
    virtual bool capture(Song &song, bool reference) = 0;
    virtual void recalled() = 0;
};
class SongsUi {
public:
    SongsUi(Songs &songs, Hardware &hardware, SongCapture &capture, Documents *documents = nullptr) : songs_(songs), hardware_(hardware), capture_(capture), documents_(documents) {}
    bool open() const { return mode_ != closed || waiting_; }
    bool performing() const { return mode_ == performance; }
    bool editing_name() const { return mode_ == naming; }
    bool patch_chooser_requested = false;
    void refresh() { if (open()) { feedback_[0] = 0; render(); } }
    void leave() {
        mode_ = closed; waiting_ = managing_library_ = managing_setlists_ = false;
        named_work_ = NamedWork::none;
        patch_banks_.release();
    }
    void perform() { managing_library_ = managing_setlists_ = false; mode_ = performance; cursor_ = 0; locked_ = true; ui::utf8::copy(feedback_, sizeof(feedback_), songs_.last_error()); render(); }
    void enter() { managing_library_ = managing_setlists_ = false; mode_ = root; cursor_ = 0; locked_ = true; patch_chooser_requested = false; render(); }
    void edit_library(bool setlists, bool shortcut_hint) {
        managing_library_ = true; managing_setlists_ = setlists; shortcut_hint_ = shortcut_hint;
        mode_ = setlists ? sets : library; cursor_ = set_index_ = 0; locked_ = true;
        return_to_performance_ = false; feedback_[0] = 0;
        std::snprintf(picker_, sizeof(picker_), "%s", setlists ? "" : songs_.current_name());
        render();
    }
    bool return_from_library() {
        if (!managing_library_ || songs_.busy() || waiting_ || named_work_ != NamedWork::none) return false;
        return_to_performance_ = true;
        if (mode_ == moving && cursor_ != entry_) review_move();
        else if (mode_ == patch_value && patch_draft_.differs(patch_values_))
            review_library_edit(LibraryEdit::change_patch_value, patch_value);
        else if (mode_ == naming) {
            name_button(2);
            if (mode_ != library_decision) leave();
        } else if (mode_ != library_decision) leave();
        if (open()) render();
        return true;
    }
    bool library_returning_to_performance() const { return return_to_performance_; }
    void poll(uint8_t held, uint32_t now = 0) {
        if (!held) locked_ = false;
        if (named_work_ != NamedWork::none && !documents_->busy()) poll_named_patch(now);
        if (mode_ == performance && !songs_.busy() && std::strcmp(feedback_, songs_.last_error())) {
            ui::utf8::copy(feedback_, sizeof(feedback_), songs_.last_error()); render();
        }
        if (waiting_ && !songs_.busy()) {
            waiting_ = false;
            if (!songs_.success()) {
                mode_ = saved_.mode; cursor_ = saved_.cursor; insertion_ = saved_.insertion;
                added_ = saved_.added; std::snprintf(picker_, 64, "%s", saved_.picker);
                std::snprintf(song_name_, 64, "%s", saved_.song); fail();
            } else {
                if (saved_.mode == library_decision && library_edit_ == LibraryEdit::change_patch_value)
                    patch_banks_.release();
                if (saved_.mode == library_decision && library_edit_ == LibraryEdit::rename_song) {
                    std::strcpy(song_name_, name_);
                    std::strcpy(picker_, name_);
                }
                if (saved_.mode == library_decision && library_edit_ == LibraryEdit::rename_setlist)
                    std::strcpy(managed_set_, name_);
                if (mode_ == patches && saved_.mode == library_decision && library_edit_ == LibraryEdit::remove_patch)
                    if (const auto *song = songs_.song(song_name_)) cursor_ = std::min(cursor_, unsigned(song->count) - 1);
                if (!feedback_[0]) ui::utf8::copy(feedback_, sizeof(feedback_), ui::i18n::text(ui::i18n::Message::saved));
            }
            if (open()) render();
        }
    }
    void button(unsigned button, bool down) {
        if (songs_.busy() || waiting_ || named_work_ != NamedWork::none) return;
        saved_ = {mode_, cursor_, insertion_, added_, {}, {}};
        std::snprintf(saved_.picker, 64, "%s", picker_);
        std::snprintf(saved_.song, 64, "%s", song_name_);
        handle_button(button, down);
        if (songs_.busy()) { waiting_ = true; render(); }
    }
    void key(uint8_t key) {
        if (mode_ != naming || locked_ || songs_.busy() || waiting_) return;
        if (key == '\n' || key == 27) { button(key == '\n' ? 2 : 5, true); return; }
        if (key != '\b' && (key < 32 || key > 126)) return;
        unsigned length = unsigned(std::strlen(name_));
        character_ = std::min(character_, length);
        if (key == '\b') {
            if (!character_) return;
            std::memmove(name_ + character_ - 1, name_ + character_, length - character_ + 1);
            --character_;
        } else {
            if (length == sizeof(name_) - 1) return;
            std::memmove(name_ + character_ + 1, name_ + character_, length - character_ + 1);
            name_[character_++] = char(key);
        }
        feedback_[0] = 0;
        render();
    }
    void keyboard_overflow() {
        if (mode_ != naming || dropped_keys_) return;
        dropped_keys_ = true;
        render();
    }
private:
    void handle_button(unsigned button, bool down) {
        if (!down || locked_ || !open() || (mode_ == performance && button == 2)) return;
        uint32_t before = songs_.generation();
        feedback_[0] = 0;
        if (mode_ == naming) { name_button(button); render(); return; }
        if (mode_ == library_decision) { library_decision_button(button); if (open()) render(); return; }
        if (mode_ == patch_scope || mode_ == patch_value) {
            if (mode_ == patch_scope) {
                if (button < 2) shared_patch_ = !shared_patch_;
                else if (button == 2) mode_ = patch_value;
                else if (button == 5) mode_ = patch_actions;
            } else if (button < 2) {
                if (patch_field_ == 3) patch_draft_.bank = adjacent_patch_bank(patch_banks_, patch_draft_.bank, button != 0);
                else {
                    const auto &field = patch_fields[unsigned(patch_draft_.kind)][patch_field_];
                    auto &value = patch_draft_.values[patch_field_];
                    value = std::clamp(value + (button ? 1 : -1), field.minimum, field.maximum);
                }
            } else if (button == 3 || button == 4) {
                const unsigned count = patch_draft_.kind == PatchKind::channel_program ? 2 : 3 + bool(patch_banks_.size());
                patch_field_ = (patch_field_ + (button == 3 ? count - 1 : 1)) % count;
            } else if (button == 2 || button == 5) {
                if (patch_draft_.differs(patch_values_)) review_library_edit(LibraryEdit::change_patch_value, patch_value);
                else mode_ = patch_actions;
            }
            if (mode_ == patch_actions) patch_banks_.release();
            render(); return;
        }
        if (button == 5) {
            if (managing_setlists_ && mode_ == moving && cursor_ != entry_) review_move();
            else if (managing_setlists_) {
                if (mode_ == sets) leave();
                else if (mode_ == set_entries) { mode_ = sets; cursor_ = set_index_; }
                else { mode_ = set_entries; cursor_ = entry_; }
            } else if (managing_library_) {
                if (mode_ == library) leave();
                else {
                    mode_ = mode_ == patch_actions ? patches : mode_ == patches ? song_actions : library;
                    cursor_ = mode_ == patches ? patch_cursor_ : 0;
                }
            } else {
                mode_ = mode_ == root ? closed : mode_ == adding ? performance : mode_ == patch_actions ? patches : mode_ == patches ? song_actions : root;
                cursor_ = 0;
            }
            if (open()) render(); return;
        }
        unsigned count = mode_ == root ? 5 + bool(documents_ && documents_->document().valid() && documents_->document().page_layout().controllers()) : mode_ == sets ? songs_.counts().sets + !managing_setlists_
            : mode_ == library || mode_ == adding ? songs_.counts().songs
            : mode_ == editing ? 9 : mode_ == set_actions ? 3 + bool(current_set() && current_set()->count)
            : mode_ == song_actions ? (managing_library_ ? 2 : 4) : mode_ == patch_actions ? (managing_library_ ? 4 : 5)
            : mode_ == patches ? (songs_.song(song_name_) ? songs_.song(song_name_)->count : 0)
            : mode_ == moving || mode_ == performance || mode_ == set_entries ? (current_set() ? current_set()->count : 0) : 0;
        if (button < 2 && (mode_ == library || mode_ == adding)) songs_.browse(picker_, picker_, button == 0);
        else if (button < 2 && count) cursor_ = (cursor_ + count + (button == 0 ? -1 : 1)) % count;
        else if ((mode_ == library || mode_ == adding) && (button == 3 || button == 4)) jump(button == 3);
        else if (mode_ == performance) {
            if (button == 3) { mode_ = editing; entry_ = cursor_; cursor_ = 0; }
        } else if (mode_ == root && button == 2) {
            unsigned action = cursor_; cursor_ = 0;
            if (action == 0) mode_ = performance;
            else if (action == 1) mode_ = sets;
            else if (action == 2) mode_ = library;
            else if (action == 5) { patch_chooser_requested = true; mode_ = closed; }
            else {
                Song item;
                if (!capture_.capture(item, action == 4) || !songs_.save_song(item.name, item)) fail();
                else { mode_ = library; picker_[0] = 0; }
            }
        } else if (mode_ == sets && button == 2) {
            if (managing_setlists_) {
                Setlist selected;
                if (!songs_.set_at(cursor_, selected)) return;
                std::strcpy(managed_set_, selected.name);
                set_index_ = cursor_; cursor_ = 0; mode_ = set_entries;
            } else if (cursor_ == songs_.counts().sets) {
                char name[64];
                if (!songs_.new_setlist(name)) fail(); else { songs_.choose(name); insertion_ = 0; cursor_ = 0; mode_ = adding; picker_[0] = 0; }
            } else { Setlist selected; if (songs_.set_at(cursor_, selected)) songs_.choose(selected.name); mode_ = performance; cursor_ = 0; }
        } else if (mode_ == library && button == 2 && count) {
            ensure_picker(); std::snprintf(song_name_, 64, "%s", picker_); mode_ = song_actions; cursor_ = 0;
        } else if (mode_ == song_actions && button == 2) {
            if (!cursor_) {
                const Song *song = songs_.song(song_name_);
                if (song) begin_name(song->name, false);
                else ui::utf8::copy(feedback_, sizeof(feedback_), ui::i18n::text(ui::i18n::Message::song_read_failed));
            }
            else if (cursor_ == 2 || managing_library_) { mode_ = patches; cursor_ = 0; }
            else if (cursor_ == 3) { songs_.choose_song(song_name_); perform(); }
            else {
                Song item;
                if (!capture_.capture(item, false) || !songs_.change_song(song_name_, item)) fail();
                else { mode_ = library; cursor_ = 0; }
            }
        } else if (mode_ == patches && button == 2 && count) {
            patch_cursor_ = cursor_; mode_ = patch_actions; cursor_ = 0;
            library_generation_ = songs_.generation();
        } else if (mode_ == patch_actions && button == 2) {
            if (managing_library_) {
                if (cursor_ == 2) {
                    patch_banks_.release();
                    SongSelection selection;
                    if (songs_.patch_at(song_name_, patch_cursor_, selection) && selection.document[0]) {
                        if (!begin_named_patch(NamedWork::inspect, selection))
                            ui::utf8::copy(feedback_, sizeof(feedback_), "Cannot read the owning setup");
                    } else if (songs_.patch_values(song_name_, patch_cursor_, hardware_, patch_values_, &patch_banks_)) {
                        named_patch_ = false;
                        patch_draft_ = patch_values_; patch_field_ = 0;
                        shared_patch_ = false;
                        mode_ = patch_scope;
                    } else ui::utf8::copy(feedback_, sizeof(feedback_), "Cannot edit this patch value");
                    render(); return;
                }
                const auto *song = songs_.song(song_name_);
                if (!song || (cursor_ == 0 ? song->count <= 1 : cursor_ == 1 ? !patch_cursor_ : patch_cursor_ + 1 >= song->count)) fail();
                else review_library_edit(cursor_ == 0 ? LibraryEdit::remove_patch : cursor_ == 1 ? LibraryEdit::move_patch_earlier : LibraryEdit::move_patch_later, patch_actions);
                render(); return;
            }
            bool good = false; Song captured;
            if (cursor_ < 2) {
                if (capture_.capture(captured, false)) good = cursor_ == 0 ? songs_.replace_patch(song_name_, patch_cursor_, captured) : songs_.insert_patch(song_name_, patch_cursor_, captured);
            } else if (cursor_ == 2) good = songs_.remove_patch(song_name_, patch_cursor_);
            else good = songs_.move_patch(song_name_, patch_cursor_, cursor_ == 3);
            if (!good) fail(); else { mode_ = patches; cursor_ = 0; }
        } else if (mode_ == adding && button == 2 && count) {
            ensure_picker(); const Song *found = songs_.song(picker_); if (!found) { fail(); render(); return; } Song item = *found;
            if (managing_setlists_) review_library_edit(LibraryEdit::insert, adding);
            else if (!songs_.insert(songs_.active_set(), insertion_, item.name)) fail();
            else { ++insertion_; ++added_; ui::i18n::format(feedback_, 64, ui::i18n::Message::song_added, item.name); }
        } else if (mode_ == editing && button == 2) {
            const Setlist *set = current_set();
            if (!set) { mode_ = sets; cursor_ = 0; }
            else if (cursor_ < 2) {
                insertion_ = cursor_ == 0 ? songs_.next_position() : set->count;
                mode_ = adding; cursor_ = added_ = 0; picker_[0] = 0; feedback_[0] = 0;
            } else if (cursor_ == 2) {
                if (!songs_.remove(set->name, entry_)) fail(); else { mode_ = performance; cursor_ = 0; }
            } else if (cursor_ == 3) { mode_ = moving; cursor_ = entry_; }
            else if (cursor_ == 4) begin_name(set->name, true);
            else if (cursor_ == 5) { if (!songs_.undo()) fail(); }
            else if (cursor_ == 6) { songs_.choose(set->name); mode_ = performance; cursor_ = 0; }
            else if (cursor_ == 7) {
                if (!documents_ || !documents_->startup_set(!std::strcmp(documents_->startup_set(), set->name) ? "" : set->name)) fail();
                else ui::utf8::copy(feedback_, sizeof(feedback_), ui::i18n::text(ui::i18n::Message::saved));
            }
            else { mode_ = performance; cursor_ = 0; }
        } else if (mode_ == set_entries && button == 2 && count > 1) {
            entry_ = cursor_; library_generation_ = songs_.generation(); mode_ = moving;
        } else if (mode_ == set_entries && button == 3) {
            entry_ = cursor_; cursor_ = 0; library_generation_ = songs_.generation(); mode_ = set_actions;
        } else if (mode_ == set_actions && button == 2) {
            const auto *set = current_set();
            if (!set) { fail(); return; }
            if (cursor_ < 2) {
                insertion_ = cursor_ == 0 ? std::min(entry_ + 1, unsigned(set->count)) : set->count;
                mode_ = adding; picker_[0] = 0;
            } else if (cursor_ == 2) begin_name(set->name, true);
            else review_library_edit(LibraryEdit::remove, set_actions);
        } else if (mode_ == moving && button == 2) {
            if (managing_setlists_) {
                if (cursor_ != entry_) review_move();
                else mode_ = set_entries;
            } else if (!songs_.move(songs_.active_set(), entry_, cursor_)) fail();
            else mode_ = performance;
        }
        if (songs_.generation() != before && !feedback_[0]) ui::utf8::copy(feedback_, sizeof(feedback_), ui::i18n::text(ui::i18n::Message::saved));
        render();
    }
private:
    enum Mode { closed, root, sets, set_entries, set_actions, library, song_actions, performance, editing, adding, moving, library_decision, naming, patches, patch_actions, patch_scope, patch_value };
    Songs &songs_; Hardware &hardware_; SongCapture &capture_;
    Documents *documents_;
    Mode mode_ = closed, return_mode_ = root;
    unsigned patch_cursor_ = 0;
    unsigned cursor_ = 0, entry_ = 0, insertion_ = 0, added_ = 0, character_ = 0;
    char song_name_[64] = {}, picker_[64] = {};
    bool locked_ = false, naming_set_ = false, dropped_keys_ = false;
    struct Saved { Mode mode; unsigned cursor, insertion, added; char picker[64], song[64]; } saved_ = {};
    bool waiting_ = false;
    char feedback_[64] = {}, name_[64] = {};
    bool managing_library_ = false, managing_setlists_ = false, shortcut_hint_ = false, return_to_performance_ = false;
    enum class LibraryEdit { move, insert, remove, rename_song, rename_setlist, remove_patch, move_patch_earlier, move_patch_later, change_patch_value };
    LibraryEdit library_edit_ = LibraryEdit::move;
    Mode library_resume_ = moving;
    unsigned set_index_ = 0, library_choice_ = 0;
    uint32_t library_generation_ = 0;
    char managed_set_[64] = {};
    PatchValues patch_values_, patch_draft_;
    CheckedArray<uint32_t> patch_banks_;
    unsigned patch_field_ = 0;
    bool shared_patch_ = false, named_patch_ = false, patch_reloads_ = false;
    uint32_t named_patch_crc_ = 0;
    enum class NamedWork { none, inspect, load_for_save, saving };
    NamedWork named_work_ = NamedWork::none;
    const Setlist *current_set() const { return songs_.setlist(managing_setlists_ ? managed_set_ : songs_.active_set()); }
    void review_move() { review_library_edit(LibraryEdit::move, moving); }
    void review_library_edit(LibraryEdit edit, Mode resume) {
        library_edit_ = edit; library_resume_ = resume; mode_ = library_decision; library_choice_ = 2;
    }
    void library_decision_button(unsigned button) {
        if (button < 2) library_choice_ = (library_choice_ + (button ? 1 : 2)) % 3;
        else if (button == 5 || (button == 2 && library_choice_ == 2)) {
            mode_ = library_resume_; return_to_performance_ = false;
        } else if (button == 2) {
            if (!library_choice_) {
                if (songs_.generation() != library_generation_) { fail(); return; }
                if (library_edit_ == LibraryEdit::change_patch_value && named_patch_) {
                    SongSelection selection;
                    if (!songs_.patch_at(song_name_, patch_cursor_, selection)
                        || !begin_named_patch(NamedWork::load_for_save, selection)) fail();
                    return;
                }
                if (!save_library_edit()) {
                    if (library_edit_ == LibraryEdit::rename_setlist && songs_.last_error()[0])
                        ui::utf8::copy(feedback_, sizeof(feedback_), songs_.last_error());
                    else fail();
                    return;
                }
                if (managing_setlists_) {
                    if (library_edit_ == LibraryEdit::insert) cursor_ = insertion_;
                    else if (library_edit_ == LibraryEdit::remove || library_edit_ == LibraryEdit::rename_setlist) cursor_ = entry_;
                }
            } else if (managing_setlists_) cursor_ = entry_;
            finish_library_edit();
        }
    }
    void finish_library_edit() {
        if (library_edit_ == LibraryEdit::change_patch_value && !songs_.busy()) patch_banks_.release();
        if (managing_setlists_) mode_ = set_entries;
        else if (library_edit_ == LibraryEdit::rename_song) { mode_ = song_actions; cursor_ = 0; }
        else {
            mode_ = patches; cursor_ = patch_cursor_;
            if (!library_choice_ && library_edit_ == LibraryEdit::move_patch_earlier) --cursor_;
            if (!library_choice_ && library_edit_ == LibraryEdit::move_patch_later) ++cursor_;
        }
        if (return_to_performance_) mode_ = closed;
    }
    bool begin_named_patch(NamedWork work, const SongSelection &selection) {
        if (!documents_ || !selection.document[0] || !documents_->edit_begin(selection.document)) return false;
        named_work_ = work;
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(songs_ui_poll_named_patch)
#endif
    void poll_named_patch(uint32_t now) {
        const auto work = named_work_;
        named_work_ = NamedWork::none;
        if (work == NamedWork::saving) {
            if (documents_->dirty() || documents_->last_error()[0]) {
                documents_->edit_cancel(); fail();
            } else {
                finish_library_edit();
                ui::utf8::copy(feedback_, sizeof(feedback_), ui::i18n::text(ui::i18n::Message::saved));
            }
        } else {
            SongSelection selection;
            Document source;
            const bool loaded = songs_.generation() == library_generation_ && songs_.patch_at(song_name_, patch_cursor_, selection)
                && !name_compare(documents_->edit_name(), selection.document) && documents_->edit_document(source);
            const int root = loaded ? source.named_patch(selection.key) : -1;
            if (work == NamedWork::inspect) {
                const auto *node = root >= 0 ? editable_patch_values(source, unsigned(root), patch_values_) : nullptr;
                if (node && read_patch_banks(source, node, patch_banks_)) {
                    named_patch_crc_ = checksum(source.data(), source.length());
                    patch_reloads_ = documents_->edit_reloads();
                    named_patch_ = true; shared_patch_ = false;
                    patch_draft_ = patch_values_; patch_field_ = 0;
                    mode_ = patch_scope;
                } else ui::utf8::copy(feedback_, sizeof(feedback_), "Cannot edit this patch value");
                documents_->edit_cancel();
            } else if (root < 0 || checksum(source.data(), source.length()) != named_patch_crc_
                || (shared_patch_ && documents_->edit_reloads() != patch_reloads_)) {
                documents_->edit_cancel();
                ui::utf8::copy(feedback_, sizeof(feedback_), "Setup changed. Reopen this patch to edit it.");
            } else if (shared_patch_) {
                if (documents_->edit_named_patch_values(selection.key, patch_draft_) && documents_->edit_save(now))
                    named_work_ = NamedWork::saving;
                else { documents_->edit_cancel(); fail(); }
            } else {
                const bool saved = songs_.save_patch_values(song_name_, patch_cursor_, hardware_, patch_draft_, false, &source);
                documents_->edit_cancel();
                if (saved) { finish_library_edit(); waiting_ = true; }
                else fail();
            }
        }
        if (open()) render();
    }
    bool save_library_edit() {
        switch (library_edit_) {
        case LibraryEdit::move: return songs_.move(managed_set_, entry_, cursor_);
        case LibraryEdit::insert: return songs_.insert(managed_set_, insertion_, picker_);
        case LibraryEdit::remove: return songs_.remove(managed_set_, entry_);
        case LibraryEdit::rename_song: return songs_.rename_song(song_name_, name_);
        case LibraryEdit::rename_setlist: return songs_.rename_setlist(managed_set_, name_);
        case LibraryEdit::remove_patch: return songs_.remove_patch(song_name_, patch_cursor_);
        case LibraryEdit::move_patch_earlier: return songs_.move_patch(song_name_, patch_cursor_, true);
        case LibraryEdit::move_patch_later: return songs_.move_patch(song_name_, patch_cursor_, false);
        case LibraryEdit::change_patch_value: return songs_.save_patch_values(song_name_, patch_cursor_, hardware_, patch_draft_, shared_patch_);
        }
        return false;
    }
    void fail() { ui::utf8::copy(feedback_, sizeof(feedback_), ui::i18n::text(ui::i18n::Message::not_saved)); }
    void ensure_picker() { if (!songs_.song(picker_)) songs_.browse("", picker_, false); }
    void jump(bool backward) { ensure_picker(); songs_.browse(picker_, picker_, backward, true); }
    void begin_name(const char *name, bool set) {
        std::snprintf(name_, 64, "%s", name); naming_set_ = set;
        library_generation_ = songs_.generation();
        dropped_keys_ = false;
        return_mode_ = mode_; mode_ = naming; character_ = 0;
    }
    void name_button(unsigned button) {
        unsigned length = unsigned(std::strlen(name_));
        if (button == 5 && !managing_library_) { mode_ = return_mode_; return; }
        if (button == 3 && character_) --character_;
        if (button == 4 && character_ < 62) ++character_;
        if (button < 2 && character_ < sizeof(name_) - 1) {
            while (length <= character_) name_[length++] = ' ';
            name_[length] = 0;
            char &c = name_[character_]; c = char(32 + (c - 32 + 95 + (button == 0 ? -1 : 1)) % 95);
        }
        if (button == 2 || button == 5) {
            dropped_keys_ = false;
            while (length && name_[length - 1] == ' ') name_[--length] = 0;
            if (managing_library_) {
                if (std::strcmp(naming_set_ ? managed_set_ : song_name_, name_))
                    review_library_edit(naming_set_ ? LibraryEdit::rename_setlist : LibraryEdit::rename_song, naming);
                else mode_ = return_mode_;
                return;
            }
            bool good = naming_set_ ? songs_.rename_setlist(songs_.active_set(), name_) : songs_.rename_song(song_name_, name_);
            if (!good) fail(); else { if (!naming_set_) { std::snprintf(song_name_, 64, "%s", name_); std::snprintf(picker_, 64, "%s", name_); } ui::utf8::copy(feedback_, sizeof(feedback_), ui::i18n::text(ui::i18n::Message::saved)); mode_ = return_mode_; cursor_ = 0; }
        }
    }
    void render() {
        ui::Prompt prompt(hardware_);
        using Role = ControlRole;
        Pane panes[3];
        for (auto &p : panes) for (auto &line : p.lines) { line.graphics.line_height = line.graphics.size = 16; line.graphics.font = ui::localized_face; line.wrap = true; }
        auto style = [&](unsigned pane, const Song *song) {
            if (!song) return;
            auto &p = panes[pane];
            p.graphics.foreground = song->foreground; p.graphics.background = song->background;
            for (auto &line : p.lines) { line.graphics.foreground = song->foreground; line.graphics.background = song->background; }
            if (pane != 1 || (mode_ != song_actions && mode_ != patch_actions)) { p.lines[1].graphics.font = song->font; p.lines[1].fit = true; }
        };
        if (!managing_library_) style(0, songs_.song(songs_.current_name()));
        if (mode_ == song_actions || mode_ == patches || mode_ == patch_actions || (mode_ == naming && !naming_set_)) style(1, songs_.song(song_name_));
        if (songs_.busy()) {
            ui::copy_caption(panes[1].lines[0], ui::i18n::text(ui::i18n::Message::saving_wait));
            ui::present(hardware_, panes, 1);
            return;
        }
        auto text = [&](unsigned pane, unsigned line, const char *s) { ui::copy_caption(panes[pane].lines[line], s); };
        text(0, 0, songs_.removed() ? ui::i18n::text(ui::i18n::Message::current_removed) : ui::i18n::text(ui::i18n::Message::current)); text(0, 1, songs_.current_name());
        SongSelection current_patch;
        if (songs_.current_patch(current_patch)) {
            text(0, 2, current_patch.label[0] ? current_patch.label : current_patch.document[0] ? current_patch.key : ui::i18n::text(ui::i18n::Message::patch));
            panes[0].lines[2].graphics.font = panes[0].lines[1].graphics.font; panes[0].lines[2].fit = true;
        }
        if (songs_.boundary()) text(0, 2, ui::i18n::text(ui::i18n::Message::empty_position));
        Setlist selected_set;
        const auto *selected = current_set();
        if (selected) selected_set = *selected;
        const auto *set = selected ? &selected_set : nullptr;
        unsigned next = songs_.next_position();
        text(2, 0, ui::i18n::text(ui::i18n::Message::next));
        SongEntry next_entry;
        const Song *upcoming = set && next < set->count && songs_.entry(set->name, next, next_entry) ? songs_.song(next_entry.song) : nullptr;
        if (!managing_library_) style(2, upcoming);
        text(2, 1, upcoming ? upcoming->name : set && !set->count ? ui::i18n::text(ui::i18n::Message::add_songs) : ui::i18n::text(ui::i18n::Message::end_of_set));
        SongSelection next_patch;
        if (songs_.upcoming_patch(next_patch)) {
            if (!managing_library_) style(2, songs_.song(songs_.current_name()));
            text(2, 0, ui::i18n::text(ui::i18n::Message::next_patch));
            text(2, 1, next_patch.label[0] ? next_patch.label : next_patch.document[0] ? next_patch.key : ui::i18n::text(ui::i18n::Message::patch));
        }
        if (songs_.upcoming_boundary()) {
            style(2, songs_.song(songs_.current_name()));
            panes[2].lines[1].graphics.font = ui::localized_face;
            text(2, 0, ui::i18n::text(ui::i18n::Message::next_patch));
            text(2, 1, ui::i18n::text(ui::i18n::Message::empty_position));
        }
        const char *roots[] = {ui::i18n::text(ui::i18n::Message::perform_setlist), ui::i18n::text(ui::i18n::Message::setlists), ui::i18n::text(ui::i18n::Message::songs), ui::i18n::text(ui::i18n::Message::save_as_song), ui::i18n::text(ui::i18n::Message::save_reference), ui::i18n::text(ui::i18n::Message::patch_chooser)};
        const char *edits[] = {ui::i18n::text(ui::i18n::Message::add_next), ui::i18n::text(ui::i18n::Message::add_to_end), ui::i18n::text(ui::i18n::Message::remove_from_setlist), ui::i18n::text(ui::i18n::Message::move), ui::i18n::text(ui::i18n::Message::rename_setlist), ui::i18n::text(ui::i18n::Message::undo_edit), ui::i18n::text(ui::i18n::Message::restart_setlist), ui::i18n::text(ui::i18n::Message::startup_set), ui::i18n::text(ui::i18n::Message::done)};
        text(1, 0, mode_ == root ? ui::i18n::text(ui::i18n::Message::songs_and_setlists) : mode_ == naming ? ui::i18n::text(ui::i18n::Message::rename) : set ? set->name : ui::i18n::text(ui::i18n::Message::songs));
        if (mode_ == root) text(1, 1, roots[cursor_]);
        else if (mode_ == sets) {
            Setlist choice;
            text(1, 1, songs_.set_at(cursor_, choice) ? choice.name
                : ui::i18n::text(managing_setlists_ ? ui::i18n::Message::empty : ui::i18n::Message::new_setlist));
        }
        else if (mode_ == editing) {
            text(1, 1, edits[cursor_]);
            if (cursor_ == 7 && documents_) text(1, 2, documents_->startup_set());
        }
        else if (mode_ == naming) { text(1, 1, name_); ui::i18n::format(panes[1].lines[2].text, 64, ui::i18n::Message::character_number, character_ + 1); }
        else if (mode_ == song_actions) {
            const ui::i18n::Message actions[] = {ui::i18n::Message::rename_song, ui::i18n::Message::use_selected_patch,
                ui::i18n::Message::edit_patches, ui::i18n::Message::perform_song};
            text(1, 1, ui::i18n::text(actions[managing_library_ && cursor_ ? 2 : cursor_]));
        }
        else if (mode_ == patches) {
            SongSelection selected;
            if (songs_.patch_at(song_name_, cursor_, selected)) {
                text(1, 1, selected.label[0] ? selected.label : selected.document[0] ? selected.key : ui::i18n::text(ui::i18n::Message::patch));
                const Song *song = songs_.song(song_name_);
                if (song) ui::i18n::format(panes[1].lines[2].text, 64, ui::i18n::Message::patch_position, cursor_ + 1, (unsigned long)song->count);
                else text(1, 2, ui::i18n::text(ui::i18n::Message::song_read_failed));
            }
        } else if (mode_ == patch_actions) {
            const ui::i18n::Message actions[] = {ui::i18n::Message::replace_with_selected, ui::i18n::Message::insert_selected_after,
                ui::i18n::Message::remove_patch, ui::i18n::Message::move_earlier, ui::i18n::Message::move_later};
            if (managing_library_ && cursor_ == 2) text(1, 1, "Edit patch value");
            else text(1, 1, ui::i18n::text(actions[managing_library_ ? (cursor_ == 3 ? 4 : cursor_ + 2) : cursor_]));
        }
        else if (mode_ == library || mode_ == adding) {
            ensure_picker();
            if (!songs_.counts().songs) text(1, 1, ui::i18n::text(ui::i18n::Message::save_song_first));
            else {
                const auto *found = songs_.song(picker_); if (!found) return; const Song s = *found;
                style(1, &s);
                text(1, 1, s.name);
                ui::i18n::format(panes[1].lines[2].text, 64, ui::i18n::Message::patches_count, (unsigned long)s.count);
            }
        } else if (mode_ == performance && !set && songs_.performing()) {
            style(1, songs_.song(songs_.performance_name()));
            text(1, 0, ui::i18n::text(ui::i18n::Message::perform_song));
            text(1, 1, songs_.performance_name());
        } else if (set && set->count) {
            cursor_ %= set->count; SongEntry selected;
            const auto *item = songs_.entry(set->name, cursor_, selected) ? songs_.song(selected.song) : nullptr;
            style(1, item);
            text(1, 1, item ? item->name : ui::i18n::text(ui::i18n::Message::missing_song));
            std::snprintf(panes[1].lines[2].text, 64, "%u/%u", cursor_ + 1, unsigned(set->count));
        } else text(1, 1, ui::i18n::text(ui::i18n::Message::add_songs));
        prompt.pair(Role::previous, Role::next, mode_ == performance ? ui::i18n::text(ui::i18n::Message::entry_action) : mode_ == naming ? ui::i18n::text(ui::i18n::Message::letter_action) : ui::i18n::text(ui::i18n::Message::browse));
        if (mode_ == performance) prompt.add(Role::previous_section, ui::i18n::text(ui::i18n::Message::edit_action));
        else prompt.pair(Role::previous_section, Role::next_section, mode_ == naming ? ui::i18n::text(ui::i18n::Message::position_action) : ui::i18n::text(ui::i18n::Message::jump_action));
        text(0, 3, prompt.text());
        prompt.clear().add(mode_ == performance ? Role::back : Role::confirm, mode_ == performance ? ui::i18n::text(ui::i18n::Message::next_action) : mode_ == adding ? ui::i18n::text(ui::i18n::Message::add_action) : ui::i18n::text(ui::i18n::Message::ok));
        if (mode_ == performance && !hardware_.setup_navigation()) {
            const char *back = hardware_.control_label(Role::back);
            const char *action = ui::i18n::text(ui::i18n::Message::back);
            std::snprintf(panes[2].lines[2].text, 64, "2x %s %s", back, action);
            ui::utf8::copy(panes[2].lines[2].text, sizeof(panes[2].lines[2].text), panes[2].lines[2].text);
        } else if (mode_ != performance) prompt.add(Role::back, mode_ == adding ? ui::i18n::text(ui::i18n::Message::done_action) : ui::i18n::text(ui::i18n::Message::back));
        text(2, 3, prompt.text());
        if (const auto *nav = hardware_.setup_navigation(); mode_ == performance && nav) {
            if (nav[0]) text(2, 3, prompt.clear().chord(nav[0], ui::i18n::text(ui::i18n::Message::next_action)).text());
            if (nav[1]) text(2, 2, prompt.clear().chord(nav[1], ui::i18n::text(ui::i18n::Message::back)).text());
        }
        if (managing_library_) {
            for (unsigned pane : {0u, 2u})
                for (unsigned line = 0; line < 3; ++line) panes[pane].lines[line].text[0] = 0;
            text(1, 3, ui::i18n::text(ui::i18n::Message::settings));
            if (shortcut_hint_) {
                std::snprintf(panes[2].lines[4].text, 64, "%s+%s %s", hardware_.control_label(Role::confirm),
                    hardware_.control_label(Role::back), ui::i18n::text(ui::i18n::Message::back));
            }
        }
        if (managing_setlists_) {
            text(1, 0, mode_ == sets ? ui::i18n::text(ui::i18n::Message::setlists) : managed_set_);
            if (mode_ == set_actions) {
                const ui::i18n::Message actions[] = {ui::i18n::Message::add_next,
                    ui::i18n::Message::add_to_end, ui::i18n::Message::rename_setlist, ui::i18n::Message::remove_from_setlist};
                text(1, 1, ui::i18n::text(actions[cursor_]));
                panes[1].lines[2].text[0] = 0;
                SongEntry entry;
                if (set && songs_.entry(set->name, entry_, entry)) {
                    text(0, 1, entry.song);
                    std::snprintf(panes[0].lines[2].text, 64, "%u/%u", entry_ + 1, unsigned(set->count));
                }
            }
            if (mode_ == library_decision && library_edit_ == LibraryEdit::rename_setlist) {
                text(0, 0, ui::i18n::text(ui::i18n::Message::rename_setlist));
                text(0, 1, name_);
                if (documents_ && !std::strcmp(documents_->startup_set(), managed_set_))
                    text(2, 0, ui::i18n::text(ui::i18n::Message::startup_set));
            } else if ((mode_ == moving || mode_ == library_decision) && set) {
                SongEntry entry;
                const auto action = mode_ == moving || library_edit_ == LibraryEdit::move ? ui::i18n::Message::move
                    : library_edit_ == LibraryEdit::insert ? ui::i18n::Message::add_action : ui::i18n::Message::remove_from_setlist;
                text(0, 0, ui::i18n::text(action));
                if (action == ui::i18n::Message::add_action) {
                    text(0, 1, picker_);
                    std::snprintf(panes[2].lines[2].text, 64, "%u/%u", insertion_ + 1, unsigned(set->count) + 1);
                } else {
                    if (songs_.entry(set->name, entry_, entry)) text(0, 1, entry.song);
                    std::snprintf(panes[0].lines[2].text, 64, "%u/%u", entry_ + 1, unsigned(set->count));
                    if (action == ui::i18n::Message::move)
                        std::snprintf(panes[2].lines[2].text, 64, "%u/%u", cursor_ + 1, unsigned(set->count));
                }
                if (panes[2].lines[2].text[0]) text(2, 0, ui::i18n::text(ui::i18n::Message::position_action));
            }
            if (mode_ == set_entries) {
                prompt.clear().add(Role::previous_section, ui::i18n::text(ui::i18n::Message::edit_action));
                if (set && set->count > 1) prompt.add(Role::confirm, ui::i18n::text(ui::i18n::Message::move));
                text(2, 3, prompt.add(Role::back, ui::i18n::text(ui::i18n::Message::back)).text());
            }
        }
        if (managing_library_ && !managing_setlists_) {
            text(1, 0, mode_ == library ? ui::i18n::text(ui::i18n::Message::songs) : song_name_);
            if (mode_ == patch_scope || mode_ == patch_value || (mode_ == library_decision && library_edit_ == LibraryEdit::change_patch_value)) {
                std::snprintf(panes[0].lines[0].text, 64, "Patch %u", patch_cursor_ + 1);
                text(0, 1, shared_patch_ ? "Every use of this patch" : "Only this song step");
                if (!shared_patch_) text(0, 2, "A copy stops following shared edits");
                else if (named_patch_) {
                    SongSelection selection;
                    if (songs_.patch_at(song_name_, patch_cursor_, selection)) {
                        auto &title = panes[0].lines[0].text;
                        const unsigned used = unsigned(std::snprintf(title, sizeof(title), "Patch %u ", patch_cursor_ + 1));
                        ui::utf8::copy(title + used, sizeof(title) - used, selection.key);
                        text(0, 2, selection.document);
                    }
                }
                const auto &fields = patch_fields[unsigned(patch_draft_.kind)];
                const auto show_field = [&](unsigned field, unsigned row) {
                    auto &line = panes[2].lines[row].text;
                    if (field == 3) {
                        if (patch_draft_.bank == UINT32_MAX)
                            std::snprintf(line, sizeof(line), "Bank %s", ui::i18n::text(ui::i18n::Message::none));
                        else std::snprintf(line, sizeof(line), "Bank %lu", (unsigned long)patch_draft_.bank);
                        return;
                    }
                    const int value = patch_draft_.values[field];
                    if (value == -65 || (field == 1 && patch_draft_.kind == PatchKind::instrument_program && value == -1))
                        std::snprintf(line, sizeof(line), "%s %s", fields[field].label, ui::i18n::text(ui::i18n::Message::none));
                    else std::snprintf(line, sizeof(line), "%s %d", fields[field].label, value);
                };
                if (mode_ == library_decision) {
                    unsigned row = 0;
                    for (unsigned i = 0; i < 3; ++i)
                        if (patch_draft_.values[i] != patch_values_.values[i]) show_field(i, row++);
                    if (patch_draft_.bank != patch_values_.bank) {
                        if (row == 3) {
                            panes[2].lines[5] = panes[2].lines[4];
                            panes[2].lines[4] = panes[2].lines[3];
                        }
                        show_field(3, row);
                    }
                } else show_field(patch_field_, 1);
                if (named_patch_ && shared_patch_ && patch_reloads_ && mode_ != library_decision)
                    text(2, 2, ui::i18n::text(ui::i18n::Message::reload_stops_playback));
                text(1, 1, mode_ == patch_scope ? "Edit scope" : "Edit patch value");
                if (mode_ == patch_scope || mode_ == patch_value) {
                    text(0, 3, prompt.clear().pair(Role::previous, Role::next,
                        ui::i18n::text(mode_ == patch_value ? ui::i18n::Message::adjust : ui::i18n::Message::browse)).text());
                    if (mode_ == patch_value)
                        text(1, 2, prompt.clear().pair(Role::previous_section, Role::next_section,
                            ui::i18n::text(ui::i18n::Message::field)).text());
                }
            }
            if (mode_ == library_decision) {
                if (library_edit_ == LibraryEdit::rename_song) {
                    text(0, 0, ui::i18n::text(ui::i18n::Message::rename_song));
                    text(0, 1, name_);
                } else if (library_edit_ != LibraryEdit::change_patch_value) {
                    const auto action = library_edit_ == LibraryEdit::remove_patch ? ui::i18n::Message::remove_patch
                        : library_edit_ == LibraryEdit::move_patch_earlier ? ui::i18n::Message::move_earlier : ui::i18n::Message::move_later;
                    text(0, 0, ui::i18n::text(action));
                    SongSelection patch;
                    if (songs_.patch_at(song_name_, patch_cursor_, patch)) {
                        text(0, 1, patch.label[0] ? patch.label : patch.document[0] ? patch.key : ui::i18n::text(ui::i18n::Message::patch));
                        if (const auto *song = songs_.song(song_name_))
                            ui::i18n::format(panes[0].lines[2].text, 64, ui::i18n::Message::patch_position, patch_cursor_ + 1, (unsigned long)song->count);
                    }
                }
            }
        }
        if (mode_ == library_decision) {
            const bool reload = library_edit_ == LibraryEdit::change_patch_value && named_patch_ && shared_patch_ && patch_reloads_;
            const auto save = reload ? ui::i18n::Message::save_reload_setup : ui::i18n::Message::save;
            const auto action = library_choice_ == 0 ? save
                : library_choice_ == 1 ? ui::i18n::Message::discard : ui::i18n::Message::keep_editing;
            text(1, 1, ui::i18n::text(action));
            text(1, 2, ui::i18n::text(reload ? ui::i18n::Message::reload_stops_playback : ui::i18n::Message::unsaved_edits));
        }
        if (named_work_ != NamedWork::none)
            text(1, 1, named_work_ == NamedWork::saving ? "Saving setup" : "Loading setup");
        const char *feedback = mode_ == naming && dropped_keys_
            ? ui::i18n::text(ui::i18n::Message::keyboard_input_dropped) : feedback_;
        if (*feedback) { text(0, 2, feedback); panes[0].lines[2].graphics.font = ui::localized_face; }
        for (unsigned i = 0; i < 3; ++i) {
            unsigned rows = 0;
            for (const auto &line : panes[i].lines) rows += (unsigned(std::strlen(line.text)) + 11) / 12;
            if (rows > 7) for (auto &line : panes[i].lines) line.graphics.line_height = line.graphics.size = 8;
        }
        ui::present(hardware_, panes, 1);
    }
};
} // namespace cntrl
