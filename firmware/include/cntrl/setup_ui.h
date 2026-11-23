#pragma once
#include "ui/messages.h"
#include "ui/fonts.h"
#include "ui/presentation.h"
#include "ui/prompts.h"
#include "documents.h"

namespace cntrl {
// Foot-operated management. A/B browse or adjust, C confirms, F goes back.
class SetupUi {
public:
    SetupUi(Documents &documents, Hardware &hardware) : docs_(documents), hardware_(hardware) {}
    bool open() const { return mode_ != Mode::closed; }
    void refresh() { if (open()) render(); }
    void enter() {
        if (docs_.busy()) return;
        mode_ = Mode::catalogue; locked_ = true;
        std::snprintf(name_, sizeof(name_), "%s", docs_.selected());
        if (!name_[0]) browse(false);
        render();
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
        if (mode_ == Mode::catalogue) {
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
        } else if (mode_ == Mode::patch) {
            if (button < 2) {
                patch_ = (patch_ + docs_.document().patches() + (button == 0 ? -1 : 1)) % docs_.document().patches();
                original_ = false;
            }
            else if (button == 2) { mode_ = Mode::field; field_ = 0; original_ = false; }
            else if (button == 5) { mode_ = Mode::decision; item_ = 2; }
        } else if (mode_ == Mode::field) {
            if (button < 2) {
                unsigned first = docs_.document().patches() ? 0 : 5;
                unsigned count = 8 - first + (first == 0 && docs_.edit_has_original(patch_));
                field_ = first + (field_ - first + count + (button == 0 ? -1 : 1)) % count;
            }
            else if (button == 2 && field_ == 8) { original_ = !original_; field_ = 0; }
            else if (button == 2) mode_ = Mode::value;
            else if (button == 5) { mode_ = docs_.document().patches() ? Mode::patch : Mode::decision; item_ = 2; }
        } else if (mode_ == Mode::value) {
            if (button < 2) docs_.edit_adjust(patch_, field_, button == 0 ? -1 : 1, original_ && field_ < 5);
            else if (button == 2 || button == 5) mode_ = Mode::field;
        } else if (mode_ == Mode::decision) {
            if (button < 2) item_ = (item_ + 3 + (button == 0 ? -1 : 1)) % 3;
            else if (button == 5) mode_ = docs_.document().patches() ? Mode::patch : Mode::field;
            else if (button == 2) {
                if (item_ == 0) { if (docs_.edit_save(now)) mode_ = Mode::saving; else { hardware_.error(ui::i18n::text(ui::i18n::Message::save_failed)); return; } }
                else if (item_ == 1) { docs_.edit_cancel(); close(); return; }
                else mode_ = docs_.document().patches() ? Mode::patch : Mode::field;
            }
        }
        render();
    }
private:
    enum class Mode { closed, catalogue, action, selecting, patch, field, value, decision, saving, failed, history, comparison, history_work };
    Documents &docs_; Hardware &hardware_;
    Mode mode_ = Mode::closed, history_destination_ = Mode::history;
    Revision revision_;
    unsigned changed_patches_ = 0, changed_routes_ = 0;
    bool changed_settings_ = false, changed_formatting_ = false;
    bool locked_ = false, browsing_ = false, select_edit_ = false, original_ = false;
    char name_[64] = {};
    unsigned item_ = 0, patch_ = 0, field_ = 0;
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
        if (mode_ == Mode::action) { title = name_; value = item_ == 2 ? ui::i18n::text(ui::i18n::Message::history) : item_ ? ui::i18n::text(ui::i18n::Message::edit) : ui::i18n::text(ui::i18n::Message::select); }
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
        if (mode_ == Mode::catalogue) {
            ui::copy_caption(views[1].lines[0], ui::i18n::text(ui::i18n::Message::setups));
            std::snprintf(views[1].lines[1].text, 64, "%s", value);
            ui::copy_caption(views[2].lines[0], ui::i18n::text(ui::i18n::Message::setup_menu));
            ui::copy_caption(views[2].lines[1], ui::i18n::text(ui::i18n::Message::no_midi_sent));
        } else {
            const bool shared_edit = original_ && (mode_ == Mode::field || mode_ == Mode::value) && field_ < 5;
            ui::copy_caption(views[1].lines[0], ui::i18n::text(mode_ == Mode::action ? ui::i18n::Message::setup : mode_ == Mode::patch ? ui::i18n::Message::patch_cycle : shared_edit ? ui::i18n::Message::edit_original_patch : ui::i18n::Message::field));
            ui::copy_caption(views[1].lines[1], title);
            ui::copy_caption(views[2].lines[0], value_label);
            ui::copy_caption(views[2].lines[1], value);
        }
        ui::present(hardware_, views, 1);
    }
};
}
