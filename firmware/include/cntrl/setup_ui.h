#pragma once
#include "ui/messages.h"
#include "ui/fonts.h"
#include "ui/presentation.h"
#include "ui/prompts.h"
#include "documents.h"
#include "songs.h"

namespace cntrl {
// Foot-operated management. A/B browse or adjust, C confirms, F goes back.
class SetupUi {
public:
    SetupUi(Documents &documents, Hardware &hardware) : docs_(documents), hardware_(hardware) {}
    bool open() const { return mode_ != Mode::closed; }
    void refresh() { if (open()) render(); }
    void enter() {
        if (docs_.busy() || docs_.dirty()) return;
        settings_ = false;
        mode_ = Mode::catalogue; locked_ = true;
        std::snprintf(name_, sizeof(name_), "%s", docs_.selected());
        if (!name_[0]) browse(false);
        render();
    }
    bool enter_settings(unsigned patch, const Songs &songs, bool performing) {
        if (open() || docs_.busy() || docs_.dirty() || !docs_.document().valid()) return false;
        settings_ = true;
        const auto &doc = docs_.document();
        current_patch_ = patch < doc.patches() && u16(doc.patch(patch)) != UINT16_MAX
            && patch_edit_node(doc, doc.browser_root(patch)) >= 0 ? patch : UINT16_MAX;
        songs_ = &songs;
        std::snprintf(current_set_, sizeof(current_set_), "%s", performing ? songs.active_set() : "");
        shortcut_ = has_performance_shortcut() ? Shortcut::performance : Shortcut::setup;
        startup_edit_ = false;
        locked_ = true;
        mode_ = Mode::current;
        render();
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_ui_poll)
#endif
    void poll(uint8_t held) {
        if (locked_ && !held) locked_ = false;
        if (mode_ == Mode::selecting && !docs_.busy()) {
            if (docs_.last_error()[0]) { mode_ = Mode::failed; render(); }
            else if (select_edit_) begin_edit();
            else close();
        }
        if (browsing_ && !docs_.busy()) {
            browsing_ = false;
            if (docs_.last_error()[0]) mode_ = Mode::failed;
            else std::snprintf(name_, sizeof(name_), "%s", docs_.browse_result());
            render();
        }
        if (mode_ == Mode::history_work && !docs_.busy()) {
            if (docs_.last_error()[0]) mode_ = Mode::failed;
            else {
                mode_ = history_destination_; revision_ = docs_.history_revision();
                const auto &comparison = docs_.history_comparison();
                changed_patches_ = comparison.patches; changed_routes_ = comparison.routes;
                changed_settings_ = comparison.settings; changed_formatting_ = comparison.formatting;
            }
            render();
        }
        if (mode_ == Mode::saving && !docs_.busy()) {
            if (docs_.last_error()[0]) { mode_ = Mode::failed; render(); }
            else { mode_ = Mode::closed; docs_.redraw(); }
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_ui_button)
#endif
    void button(unsigned button, bool down, uint32_t now) {
        if (!down || locked_ || browsing_ || mode_ == Mode::selecting || (mode_ == Mode::saving || mode_ == Mode::history_work)) return;
        if (mode_ == Mode::failed) {
            if (button == 2 || button == 5) {
                if (docs_.dirty()) { mode_ = Mode::decision; item_ = 0; render(); }
                else close();
            }
            return;
        }
        if (mode_ == Mode::current) {
            if (button == 5) { close(); return; }
            const unsigned first = has_performance_shortcut() ? 0 : 1;
            if (button < 2) {
                const unsigned count = 3 - first;
                shortcut_ = Shortcut(first + (unsigned(shortcut_) - first + count + (button == 0 ? -1 : 1)) % count);
            }
            if (button == 2) {
                if (shortcut_ == Shortcut::pedal || (shortcut_ == Shortcut::performance && current_set_[0])) {
                    startup_edit_ = true;
                    startup_choice_ = 0;
                    const char *initial = shortcut_ == Shortcut::performance ? current_set_ : docs_.startup_set();
                    Setlist set;
                    for (unsigned i = 0; i < songs_->counts().sets && songs_->set_at(i, set); ++i)
                        if (!std::strcmp(initial, set.name)) { startup_choice_ = i + 1; break; }
                    std::snprintf(startup_draft_, sizeof(startup_draft_), "%s", initial);
                    mode_ = Mode::startup_set;
                    render();
                    return;
                }
                if (!docs_.edit_begin()) { hardware_.error(ui::i18n::text(ui::i18n::Message::operation_failed)); return; }
                original_ = false;
                changed_fields_ = 0;
                current_field_ = shortcut_ == Shortcut::performance;
                patch_ = current_field_ ? current_patch_ : 0;
                field_ = current_field_ ? 2 : 5;
                mode_ = Mode::field;
            }
        } else if (mode_ == Mode::startup_set) {
            if (button < 2) {
                const unsigned count = songs_->counts().sets + 1;
                const unsigned choice = (startup_choice_ + count + (button == 0 ? -1 : 1)) % count;
                Setlist set;
                if (choice && !songs_->set_at(choice - 1, set)) {
                    hardware_.error(ui::i18n::text(ui::i18n::Message::operation_failed)); return;
                }
                startup_choice_ = choice;
                std::snprintf(startup_draft_, sizeof(startup_draft_), "%s", set.name);
            } else if ((button == 2 || button == 5) && std::strcmp(startup_draft_, docs_.startup_set())) {
                mode_ = Mode::decision; item_ = 2;
            } else if (button == 2 || button == 5) { mode_ = Mode::current; startup_edit_ = false; }
        } else if (mode_ == Mode::catalogue) {
            if (button < 2) browse(button == 0);
            else if (button == 2 && name_[0]) { mode_ = Mode::action; item_ = 0; }
            else if (button == 5) { close(); return; }
        } else if (mode_ == Mode::action) {
            if (button < 2) item_ = (item_ + 3 + (button == 0 ? -1 : 1)) % 3;
            else if (button == 5) mode_ = Mode::catalogue;
            else if (button == 2) {
                if (item_ == 2) {
                    revision_ = Revision(); mode_ = Mode::history; render(); return;
                }
                if (item_ == 0) {
                    if (!docs_.select(name_)) { hardware_.error(ui::i18n::text(ui::i18n::Message::selection_failed)); return; }
                    if (docs_.busy()) { select_edit_ = false; mode_ = Mode::selecting; render(); }
                    else close();
                    return;
                }
                if (name_compare(name_, docs_.selected())) {
                    if (!docs_.select(name_)) { hardware_.error(ui::i18n::text(ui::i18n::Message::selection_failed)); return; }
                    if (docs_.busy()) { select_edit_ = true; mode_ = Mode::selecting; render(); return; }
                }
                begin_edit();
            }
        } else if (mode_ == Mode::history) {
            if (button == 2 && !revision_.number) {
                if (!docs_.history_save(name_, "")) { hardware_.error(ui::i18n::text(ui::i18n::Message::snapshot_failed)); return; }
                history_destination_ = Mode::history; mode_ = Mode::history_work; render(); return;
            }
            if (button == 5) mode_ = Mode::action;
            else if (button < 2) {
                if (button == 0) revision_ = Revision();
                else if (docs_.history_next(name_, revision_.number ? revision_.number : UINT64_MAX)) {
                    history_destination_ = Mode::history; mode_ = Mode::history_work;
                }
            } else if (button == 2 && revision_.number) {
                if (!docs_.history_compare(name_, revision_.number)) {
                    hardware_.error(ui::i18n::text(ui::i18n::Message::cannot_compare_revision)); return;
                }
                history_destination_ = Mode::comparison; mode_ = Mode::history_work; item_ = 0;
            }
        } else if (mode_ == Mode::comparison) {
            if (button == 5) mode_ = Mode::history;
            else if (button < 2) item_ = (item_ + 5 + (button == 0 ? -1 : 1)) % 5;
            else if (button == 2 && item_ == 4) {
                if (docs_.history_restore(name_, revision_.number, now)) mode_ = Mode::saving;
                else { hardware_.error(ui::i18n::text(ui::i18n::Message::restore_failed)); return; }
            }
        } else if (mode_ == Mode::cycle_instrument || mode_ == Mode::cycle_tag
                   || mode_ == Mode::cycle_confirm || mode_ == Mode::cycle_name) {
            cycle_button(button);
        } else if ((mode_ == Mode::patch || mode_ == Mode::field) && button == 4
                   && docs_.document().cycle_catalogue().size()) {
            cycle_return_ = mode_;
            alias_ = 0;
            while (!docs_.document().cycle_catalogue().count(alias_)) ++alias_;
            mode_ = Mode::cycle_instrument;
        } else if (mode_ == Mode::patch) {
            if (button < 2) {
                patch_ = (patch_ + docs_.document().patches() + (button == 0 ? -1 : 1)) % docs_.document().patches();
                original_ = false;
            }
            else if (button == 2) { mode_ = Mode::field; field_ = 0; original_ = false; }
            else if (button == 5) { mode_ = Mode::decision; item_ = 2; }
        } else if (mode_ == Mode::field) {
            if (button < 2) {
                if (settings_ && current_field_) return;
                const unsigned first = settings_ || !docs_.document().patches() ? 5 : 0;
                const unsigned count = 8 - first + (first == 0 && docs_.edit_has_original(patch_));
                field_ = first + (field_ - first + count + (button == 0 ? -1 : 1)) % count;
            }
            else if (button == 2 && field_ == 8) { original_ = !original_; field_ = 0; }
            else if (button == 2) mode_ = Mode::value;
            else if (button == 5 && settings_) {
                if (changed_fields_) { mode_ = Mode::decision; item_ = 2; }
                else { docs_.edit_cancel(); mode_ = Mode::current; }
            }
            else if (button == 5) { mode_ = docs_.document().patches() ? Mode::patch : Mode::decision; item_ = 2; }
        } else if (mode_ == Mode::value) {
            if (button < 2 && docs_.edit_adjust(patch_, field_, button == 0 ? -1 : 1, original_ && field_ < 5))
                changed_fields_ |= 1u << field_;
            else if (button == 2 || button == 5) mode_ = Mode::field;
        } else if (mode_ == Mode::decision && startup_edit_) {
            if (button < 2) item_ = (item_ + 3 + (button == 0 ? -1 : 1)) % 3;
            else if (button == 5 || (button == 2 && item_ == 2)) mode_ = Mode::startup_set;
            else if (button == 2) {
                if (!item_ && ((startup_draft_[0] && !songs_->setlist(startup_draft_)) || !docs_.startup_set(startup_draft_))) {
                    hardware_.error(ui::i18n::text(ui::i18n::Message::save_failed)); return;
                }
                close(); return;
            }
        } else if (mode_ == Mode::decision) {
            if (button < 2) item_ = (item_ + 3 + (button == 0 ? -1 : 1)) % 3;
            else if (button == 5) mode_ = settings_ || !docs_.document().patches() ? Mode::field : Mode::patch;
            else if (button == 2) {
                if (item_ == 0) { if (docs_.edit_save(now)) mode_ = Mode::saving; else { hardware_.error(ui::i18n::text(ui::i18n::Message::save_failed)); return; } }
                else if (item_ == 1) { docs_.edit_cancel(); close(); return; }
                else mode_ = settings_ || !docs_.document().patches() ? Mode::field : Mode::patch;
            }
        }
        render();
    }
private:
    enum class Mode { closed, current, startup_set, catalogue, action, selecting, patch, field, value, decision, saving, failed, history, comparison, history_work,
                      cycle_instrument, cycle_tag, cycle_confirm, cycle_name };
    Documents &docs_; Hardware &hardware_;
    Mode mode_ = Mode::closed, history_destination_ = Mode::history;
    Revision revision_;
    unsigned changed_patches_ = 0, changed_routes_ = 0;
    bool changed_settings_ = false, changed_formatting_ = false;
    bool locked_ = false, browsing_ = false, select_edit_ = false, original_ = false;
    bool settings_ = false, current_field_ = false;
    unsigned current_patch_ = UINT16_MAX, changed_fields_ = 0;
    enum class Shortcut { performance, setup, pedal };
    Shortcut shortcut_ = Shortcut::setup;
    const Songs *songs_ = nullptr;
    bool startup_edit_ = false;
    unsigned startup_choice_ = 0;
    char startup_draft_[64] = {};
    char current_set_[64] = {};
    bool has_performance_shortcut() const { return current_patch_ != UINT16_MAX || current_set_[0]; }
    char name_[64] = {};
    unsigned item_ = 0, patch_ = 0, field_ = 0;
    Mode cycle_return_ = Mode::closed;
    unsigned alias_ = 0, tag_ = CycleCatalogue::all, character_ = 0;
    char cycle_name_[64] = {};
    char suggested_[64] = {};
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_cycle_name)
#endif
    void propose_cycle_name() {
        const auto &doc = docs_.document();
        const char *alias = reinterpret_cast<const char *>(doc.instrument(alias_));
        const char *tag = tag_ == CycleCatalogue::all ? "" : doc.cycle_catalogue().tag(tag_);
        std::snprintf(suggested_, sizeof(suggested_), "%s", alias);
        if (*tag) {
            const auto used = std::strlen(suggested_);
            suggested_[used] = ' ';
            ui::utf8::copy(suggested_ + used + 1, sizeof(suggested_) - used - 1, tag);
        }
        std::strcpy(cycle_name_, suggested_);
        for (unsigned suffix = 2;; ++suffix) {
            if (!docs_.edit_cycle_name_used(cycle_name_)) break;
            char number[12];
            std::snprintf(number, sizeof(number), " %u", suffix);
            ui::utf8::copy(cycle_name_, sizeof(cycle_name_) - std::strlen(number), suggested_);
            std::strcat(cycle_name_, number);
        }
        std::strcpy(suggested_, cycle_name_);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_cycle_button)
#endif
    void cycle_button(unsigned button) {
        const auto &doc = docs_.document();
        const auto &catalogue = doc.cycle_catalogue();
        if (mode_ == Mode::cycle_instrument) {
            if (button == 5) mode_ = cycle_return_;
            else if (button < 2) do {
                alias_ = (alias_ + doc.instruments() + (button ? 1 : -1)) % doc.instruments();
            } while (!catalogue.count(alias_));
            else if (button == 2) { tag_ = CycleCatalogue::all; mode_ = Mode::cycle_tag; }
        } else if (mode_ == Mode::cycle_tag) {
            if (button == 5) mode_ = Mode::cycle_instrument;
            else if (button < 2) {
                const unsigned count = catalogue.tags() + 1;
                unsigned choice = tag_ == CycleCatalogue::all ? 0 : tag_ + 1;
                do { choice = (choice + count + (button ? 1 : -1)) % count; }
                while (choice && !catalogue.count(alias_, choice - 1));
                tag_ = choice ? choice - 1 : CycleCatalogue::all;
            } else if (button == 2) { propose_cycle_name(); mode_ = Mode::cycle_confirm; }
        } else if (mode_ == Mode::cycle_confirm) {
            if (button == 5) mode_ = Mode::cycle_tag;
            else if (button == 3) {
                std::strcpy(suggested_, cycle_name_);
                for (const char *p = cycle_name_; *p; ++p) if (uint8_t(*p) >= 128) {
                    cycle_name_[0] = 0; break;
                }
                character_ = 0;
                mode_ = Mode::cycle_name;
            } else if (button == 2) {
                if (docs_.edit_cycle_name_used(cycle_name_)) {
                    hardware_.error("Cycle name already used");
                    return;
                }
                if (!docs_.edit_add_cycle(cycle_name_, alias_, tag_)) {
                    hardware_.error("Cycle exceeds available setup capacity");
                    return;
                }
                mode_ = Mode::decision; item_ = 0;
            }
        } else {
            unsigned length = unsigned(std::strlen(cycle_name_));
            if (button == 5) std::strcpy(cycle_name_, suggested_);
            if (button == 2 || button == 5) {
                while (length && button == 2 && cycle_name_[length - 1] == ' ') cycle_name_[--length] = 0;
                if (!cycle_name_[0]) std::strcpy(cycle_name_, suggested_);
                mode_ = Mode::cycle_confirm;
            } else if (button == 3 && character_) --character_;
            else if (button == 4 && character_ < 62) ++character_;
            else if (button < 2) {
                while (length <= character_) cycle_name_[length++] = ' ';
                cycle_name_[length] = 0;
                char &letter = cycle_name_[character_];
                letter = char(32 + (letter - 32 + 95 + (button ? 1 : -1)) % 95);
            }
        }
    }
    void close() { mode_ = Mode::closed; docs_.redraw(); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_ui_begin_edit)
#endif
    void begin_edit() {
        if (!docs_.edit_begin()) { mode_ = Mode::failed; render(); return; }
        original_ = false;
        mode_ = Mode::patch; patch_ = field_ = 0;
        if (!docs_.document().patches()) { mode_ = Mode::field; field_ = 5; }
        render();
    }
    void browse(bool backward) {
        if (docs_.browse(name_, backward)) browsing_ = true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_ui_render)
#endif
    void render() {
        ui::Prompt prompt(hardware_);
        using Role = ControlRole;
        const char *title = ui::i18n::text(ui::i18n::Message::setups), *value = browsing_ ? ui::i18n::text(ui::i18n::Message::working) : name_[0] ? name_ : ui::i18n::text(ui::i18n::Message::no_setups);
        const char *value_label = ui::i18n::text(ui::i18n::Message::action);
        char number[64] = {};
        if (mode_ == Mode::current) {
            const auto &doc = docs_.document();
            title = ui::i18n::text(ui::i18n::Message::current);
            if (shortcut_ == Shortcut::performance && current_set_[0]) {
                value_label = ui::i18n::text(ui::i18n::Message::setlist);
                value = current_set_;
            } else if (shortcut_ == Shortcut::performance) {
                value_label = ui::i18n::text(ui::i18n::Message::patch);
                value = reinterpret_cast<const char *>(doc.patch(current_patch_) + 12);
            } else if (shortcut_ == Shortcut::setup) {
                value_label = ui::i18n::text(ui::i18n::Message::setup);
                value = docs_.selected();
            } else {
                value_label = ui::i18n::text(ui::i18n::Message::pedal);
                value = ui::i18n::text(ui::i18n::Message::startup_set);
            }
        }
        else if (mode_ == Mode::startup_set) {
            title = ui::i18n::text(ui::i18n::Message::startup_set);
            value_label = ui::i18n::text(ui::i18n::Message::pedal);
            value = startup_draft_[0] ? startup_draft_ : ui::i18n::text(ui::i18n::Message::none);
        }
        else if (mode_ == Mode::action) { title = name_; value = item_ == 2 ? ui::i18n::text(ui::i18n::Message::history) : item_ ? ui::i18n::text(ui::i18n::Message::edit) : ui::i18n::text(ui::i18n::Message::select); }
        else if (mode_ == Mode::patch) {
            title = reinterpret_cast<const char *>(docs_.document().patch_cycle(u16(docs_.document().patch(patch_) + 2)));
            value = reinterpret_cast<const char *>(docs_.document().patch(patch_) + 12);
            value_label = ui::i18n::text(ui::i18n::Message::patch);
        }
        else if (mode_ == Mode::field && field_ == 8) {
            title = ui::i18n::text(original_ ? ui::i18n::Message::edit_patch_entry : ui::i18n::Message::edit_original_patch);
            value = "";
        } else if (mode_ == Mode::field || mode_ == Mode::value) {
            const char *fields[] = {ui::i18n::text(ui::i18n::Message::program), ui::i18n::text(ui::i18n::Message::bank), ui::i18n::text(ui::i18n::Message::instrument_channel), ui::i18n::text(ui::i18n::Message::volume), ui::i18n::text(ui::i18n::Message::pan), ui::i18n::text(ui::i18n::Message::brightness), ui::i18n::text(ui::i18n::Message::idle_seconds), ui::i18n::text(ui::i18n::Message::usb_midi)};
            title = fields[field_];
            int64_t v = docs_.edit_value(patch_, field_);
            if ((field_ == 1 && v == -1) || (field_ == 3 && v == -1) || (field_ == 4 && v == -65)) ui::i18n::format(number, sizeof(number), ui::i18n::Message::none);
            else if (field_ == 7) std::snprintf(number, sizeof(number), "%s", v ? ui::i18n::text(ui::i18n::Message::on) : ui::i18n::text(ui::i18n::Message::off));
            else if (v >= 0) std::snprintf(number, sizeof(number), "%lu", (unsigned long)v);
            else std::snprintf(number, sizeof(number), "%ld", (long)v);
            value = number;
            value_label = ui::i18n::text(ui::i18n::Message::value);
        } else if (mode_ == Mode::decision) {
            title = ui::i18n::text(ui::i18n::Message::unsaved_edits); value = item_ == 0 ? ui::i18n::text(ui::i18n::Message::save) : item_ == 1 ? ui::i18n::text(ui::i18n::Message::discard) : ui::i18n::text(ui::i18n::Message::cancel);
        } else if (mode_ == Mode::history_work) { title = ui::i18n::text(ui::i18n::Message::history); value = ui::i18n::text(ui::i18n::Message::working); }
        else if (mode_ == Mode::saving) { title = ui::i18n::text(ui::i18n::Message::saving); value = ui::i18n::text(ui::i18n::Message::please_wait); }
        else if (mode_ == Mode::selecting) { title = ui::i18n::text(ui::i18n::Message::changing_setup); value = ui::i18n::text(ui::i18n::Message::please_wait); }
        else if (mode_ == Mode::failed) { title = ui::i18n::text(ui::i18n::Message::operation_failed); value = docs_.last_error(); }
        const bool adding_cycle = mode_ == Mode::cycle_instrument || mode_ == Mode::cycle_tag
            || mode_ == Mode::cycle_confirm || mode_ == Mode::cycle_name;
        if (adding_cycle) {
            const auto &doc = docs_.document();
            title = "Add patch cycle";
            if (mode_ == Mode::cycle_instrument) {
                value_label = "Instrument";
                value = reinterpret_cast<const char *>(doc.instrument(alias_));
            } else if (mode_ == Mode::cycle_tag) {
                value_label = "Programs";
                value = tag_ == CycleCatalogue::all ? "All programs" : doc.cycle_catalogue().tag(tag_);
            } else if (mode_ == Mode::cycle_confirm) {
                std::snprintf(number, sizeof(number), "%u programs", doc.cycle_catalogue().count(alias_, tag_));
                value_label = number; value = cycle_name_;
            } else {
                std::snprintf(number, sizeof(number), "Character %u", character_ + 1);
                value_label = number; value = cycle_name_;
            }
        }
        if (mode_ == Mode::history) {
            title = ui::i18n::text(ui::i18n::Message::history); value_label = ui::i18n::text(ui::i18n::Message::revision);
            if (revision_.number) {
                char id[21]; revision_text(revision_.number, id);
                std::snprintf(number, sizeof(number), "%s @%lu %.24s", id, (unsigned long)revision_.modified, revision_.label);
            }
            else ui::i18n::format(number, sizeof(number), ui::i18n::Message::current_snapshot, prompt.clear().add(Role::confirm, ui::i18n::text(ui::i18n::Message::snapshot)).text());
            value = number;
        } else if (mode_ == Mode::comparison) {
            title = ui::i18n::text(ui::i18n::Message::compare_current); value_label = ui::i18n::text(ui::i18n::Message::change);
            if (item_ == 0) ui::i18n::format(number, sizeof(number), ui::i18n::Message::changed_patches, changed_patches_);
            else if (item_ == 1) ui::i18n::format(number, sizeof(number), ui::i18n::Message::changed_routes, changed_routes_);
            else if (item_ == 2) ui::i18n::format(number, sizeof(number), ui::i18n::Message::settings_change, changed_settings_ ? ui::i18n::text(ui::i18n::Message::changed) : ui::i18n::text(ui::i18n::Message::same));
            else if (item_ == 3) ui::i18n::format(number, sizeof(number), ui::i18n::Message::authored_data_change, changed_formatting_ ? ui::i18n::text(ui::i18n::Message::changed) : ui::i18n::text(ui::i18n::Message::same));
            else std::snprintf(number, sizeof(number), "%s", prompt.clear().add(Role::confirm, ui::i18n::text(ui::i18n::Message::restore)).add(Role::back, ui::i18n::text(ui::i18n::Message::back_title), ", ").text());
            value = number;
        }
        // Management has its own readable defaults; performance typography may
        // be too large for field labels or absent altogether on an empty card.
        Pane views[3];
        for (auto &view : views) {
            for (auto &line : view.lines) line.graphics.font = ui::localized_face;
            for (unsigned i = 0; i < 2; ++i)
                view.lines[i].graphics.line_height = view.lines[i].graphics.size = 16;
            view.lines[1].wrap = true;
        }
        std::snprintf(views[0].lines[0].text, 64, "%s", prompt.clear().pair(Role::previous, Role::next, ui::i18n::text(ui::i18n::Message::browse)).text());
        std::snprintf(views[0].lines[1].text, 64, "%s", prompt.clear().add(Role::confirm, ui::i18n::text(ui::i18n::Message::ok)).add(Role::back, ui::i18n::text(ui::i18n::Message::back), ", ").text());
        if (mode_ == Mode::value) std::snprintf(views[0].lines[0].text, 64, "%s", prompt.clear().pair(Role::previous, Role::next, ui::i18n::text(ui::i18n::Message::adjust)).text());
        views[0].lines[2].graphics.line_height = views[0].lines[2].graphics.size = 16;
        if ((mode_ == Mode::patch || mode_ == Mode::field) && docs_.document().cycle_catalogue().size())
            ui::copy_caption(views[0].lines[2], prompt.clear().add(Role::next_section, "Add cycle").text());
        if (mode_ == Mode::cycle_confirm)
            ui::copy_caption(views[0].lines[2], prompt.clear().add(Role::previous_section, ui::i18n::text(ui::i18n::Message::rename)).text());
        if (mode_ == Mode::cycle_name)
            ui::copy_caption(views[0].lines[2], prompt.clear().pair(Role::previous_section, Role::next_section, ui::i18n::text(ui::i18n::Message::position_action)).text());
        if (mode_ == Mode::catalogue) {
            ui::copy_caption(views[1].lines[0], ui::i18n::text(ui::i18n::Message::setups));
            std::snprintf(views[1].lines[1].text, 64, "%s", value);
            ui::copy_caption(views[2].lines[0], ui::i18n::text(ui::i18n::Message::setup_menu));
            ui::copy_caption(views[2].lines[1], ui::i18n::text(ui::i18n::Message::no_midi_sent));
        } else {
            const bool shared_edit = original_ && (mode_ == Mode::field || mode_ == Mode::value) && field_ < 5;
            ui::copy_caption(views[1].lines[0], ui::i18n::text(mode_ == Mode::action || adding_cycle ? ui::i18n::Message::setup : mode_ == Mode::patch ? ui::i18n::Message::patch_cycle : shared_edit ? ui::i18n::Message::edit_original_patch : ui::i18n::Message::field));
            ui::copy_caption(views[1].lines[1], title);
            ui::copy_caption(views[2].lines[0], value_label);
            ui::copy_caption(views[2].lines[1], value);
        }
        if (settings_) {
            views[2].lines[2].graphics.line_height = views[2].lines[2].graphics.size = 16;
            views[0].lines[2].wrap = true;
            ui::copy_caption(views[1].lines[0], ui::i18n::text(ui::i18n::Message::settings));
            if (mode_ == Mode::current && shortcut_ == Shortcut::performance && current_set_[0])
                ui::copy_caption(views[0].lines[2], prompt.clear().add(Role::confirm, ui::i18n::text(ui::i18n::Message::use_set_at_startup)).text());
            if (mode_ == Mode::current && current_set_[0] && songs_->boundary()) {
                ui::copy_caption(views[1].lines[1], ui::i18n::text(ui::i18n::Message::between_songs));
                views[1].lines[2].graphics.line_height = views[1].lines[2].graphics.size = 16;
                SongEntry entry;
                if (songs_->entry(current_set_, songs_->next_position(), entry))
                    ui::i18n::format(views[1].lines[2].text, sizeof(views[1].lines[2].text), ui::i18n::Message::next_song_name, entry.song);
            }
            if ((mode_ == Mode::field || mode_ == Mode::value) && field_ == 2) {
                const auto &doc = docs_.document();
                ui::copy_caption(views[2].lines[0], reinterpret_cast<const char *>(doc.instrument(u16(doc.patch(patch_)))));
                ui::copy_caption(views[0].lines[2], ui::i18n::text(ui::i18n::Message::instrument_channel_scope));
            }
            if ((mode_ == Mode::field || mode_ == Mode::value) && !current_field_)
                ui::copy_caption(views[2].lines[0], docs_.selected());
            if ((mode_ == Mode::field || mode_ == Mode::value) && (changed_fields_ & (1u << field_)))
                ui::copy_caption(views[2].lines[2], ui::i18n::text(ui::i18n::Message::changed));
            if (startup_edit_)
                ui::copy_caption(views[0].lines[2], ui::i18n::text(ui::i18n::Message::startup_set_scope));
            if (mode_ == Mode::decision) {
                ui::copy_caption(views[1].lines[0], startup_edit_ ? ui::i18n::text(ui::i18n::Message::pedal) : docs_.selected());
                const auto save = startup_edit_ ? ui::i18n::Message::save : ui::i18n::Message::save_reload_setup;
                const auto action = item_ == 0 ? save : item_ == 1 ? ui::i18n::Message::discard : ui::i18n::Message::keep_editing;
                ui::copy_caption(views[2].lines[1], ui::i18n::text(action));
                if (startup_edit_)
                    ui::copy_caption(views[2].lines[2], startup_draft_[0] ? startup_draft_ : ui::i18n::text(ui::i18n::Message::none));
                else ui::copy_caption(views[0].lines[2], ui::i18n::text(ui::i18n::Message::reload_stops_playback));
            }
        }
        ui::present(hardware_, views, 1);
    }
};
}
