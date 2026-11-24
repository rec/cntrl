#pragma once
#include "ui/messages.h"
#include "ui/fonts.h"
#include "ui/presentation.h"
#include "ui/preferences.h"
#include "ui/prompts.h"
#include "ui/raster.h"
#include "documents.h"
#include "expression_pedals.h"
#include "songs.h"

namespace cntrl {
// Foot-operated management. A/B browse or adjust, C confirms, F goes back.
class SetupUi {
public:
    SetupUi(Documents &documents, Hardware &hardware) : docs_(documents), hardware_(hardware) {}
    bool open() const { return mode_ != Mode::closed; }
    ExpressionPedals *calibration() const { return mode_ == Mode::calibration ? hardware_.expression_calibration() : nullptr; }
    enum class Library { none, songs, setlists };
    Library take_library_request() {
        const auto requested = library_request_;
        if (requested == Library::none) return requested;
        library_request_ = Library::none;
        mode_ = Mode::closed;
        return requested;
    }
    void resume_settings() { mode_ = Mode::current; locked_ = true; render(); }
    void refresh() { if (open()) render(); }
    bool editing_text() const {
        return mode_ == Mode::contact || (mode_ == Mode::decision
            && (device_editor_ == Mode::contact || device_editor_ == Mode::calibration_name));
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_ui_enter_contact)
#endif
    bool enter_contact() {
        if (open() || docs_.busy() || docs_.dirty() || !docs_.global_preferences()) return false;
        settings_ = true;
        contact_shortcut_ = true;
        shortcut_hint_ = return_from_shortcut_ = false;
        locked_ = true;
        begin_contact();
        contact_cursor_ = unsigned(std::strlen(contact_draft_));
        render();
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_ui_text_key)
#endif
    void text_key(uint8_t key, uint32_t now) {
        if (locked_ || !editing_text()) return;
        if (key == 27) { button(5, true, now); return; }
        if (mode_ == Mode::decision) {
            if (key == '\n') button(2, true, now);
            return;
        }
        if (key == '\n' && device_editor_ == Mode::calibration_name) { button(5, true, now); return; }
        char *position = contact_draft_ + contact_cursor_;
        if (key == '\b') {
            if (!contact_cursor_) return;
            do { --contact_cursor_; } while ((uint8_t(contact_draft_[contact_cursor_]) & 0xc0) == 0x80);
            std::memmove(contact_draft_ + contact_cursor_, position, std::strlen(position) + 1);
        } else {
            if (key != '\n' && (key < 32 || key > 126)) return;
            if (std::strlen(contact_draft_) + 1 >= sizeof(contact_draft_)) {
                hardware_.error(ui::i18n::text(ui::i18n::Message::capacity_exceeded)); return;
            }
            std::memmove(position + 1, position, std::strlen(position) + 1);
            *position = char(key);
            ++contact_cursor_;
        }
        render();
    }
    void enter() {
        if (docs_.busy() || docs_.dirty()) return;
        settings_ = false;
        setup_preferences_ = false;
        setup_changes_ = 0;
        mode_ = Mode::catalogue; locked_ = true;
        std::snprintf(name_, sizeof(name_), "%s", docs_.selected());
        if (!name_[0]) browse(false);
        render();
    }
    bool enter_settings(unsigned patch, const Songs &songs, bool performing, bool shortcut_hint = false) {
        if (open() || docs_.busy() || docs_.dirty() || !docs_.document().valid()) return false;
        settings_ = true;
        shortcut_hint_ = shortcut_hint;
        const auto &doc = docs_.document();
        current_patch_ = patch < doc.patches() && u16(doc.patch(patch)) != UINT16_MAX
            && patch_edit_node(doc, doc.browser_root(patch)) >= 0 ? patch : UINT16_MAX;
        songs_ = &songs;
        std::snprintf(current_set_, sizeof(current_set_), "%s", performing ? songs.active_set() : "");
        shortcut_ = has_performance_shortcut() ? Shortcut::performance : Shortcut::setup;
        device_editor_ = Mode::closed;
        resume_editor_ = Mode::closed;
        setup_preferences_ = false;
        setup_changes_ = 0;
        return_from_shortcut_ = false;
        library_request_ = Library::none;
        locked_ = true;
        mode_ = Mode::current;
        render();
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_ui_return_to_performance)
#endif
    bool return_to_performance() {
        if (!settings_ || !open() || docs_.busy() || browsing_ || mode_ == Mode::language_saving) return false;
        if (mode_ == Mode::decision) { return_from_shortcut_ = true; return true; }
        if ((mode_ == Mode::neutral_fields || mode_ == Mode::neutral_value) && !stage_neutral()) {
            render();
            return true;
        }
        if (setup_preferences_ && mode_ == Mode::preference_value && !stage_setup_preference()) {
            render();
            return true;
        }
        const bool changed = device_editor_ == Mode::contact || device_editor_ == Mode::calibration_name ? text_changed()
            : device_editor_ == Mode::startup_set ? std::strcmp(startup_draft_, docs_.startup_set()) != 0
            : device_editor_ == Mode::preference_value ? preference_changed()
            : device_editor_ == Mode::boot_firmware ? boot_draft_ != hardware_.boot_firmware()
            : device_editor_ == Mode::language ? language_changed()
            : device_editor_ == Mode::calibration ? hardware_.expression_calibration()->captured()
            : device_editor_ == Mode::calibration_delay ? calibration_delay_ != hardware_.expression_calibration()->settle_ms()
            : docs_.dirty() && (changed_fields_ || setup_changes_);
        if (!changed) {
            if (device_editor_ == Mode::calibration) hardware_.expression_calibration()->cancel();
            if (docs_.dirty()) docs_.edit_cancel();
            close();
        } else {
            if (device_editor_ == Mode::calibration) hardware_.expression_calibration()->pause_capture(true);
            return_from_shortcut_ = true;
            resume_editor_ = mode_;
            mode_ = Mode::decision;
            item_ = 2;
            render();
        }
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_ui_poll)
#endif
    void poll(uint8_t held) {
        if (locked_ && !held) locked_ = false;
        if (mode_ == Mode::language_saving && !hardware_.language_change_pending()) {
            if (language_changed()) mode_ = Mode::decision;
            else leave_device_editor();
            if (open()) render();
        }
        if (mode_ == Mode::selecting && !docs_.busy()) {
            if (docs_.last_error()[0]) { mode_ = Mode::failed; render(); }
            else close();
        }
        if (mode_ == Mode::loading_draft && !docs_.busy()) open_draft();
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
        if (!down || locked_ || browsing_ || mode_ == Mode::selecting || mode_ == Mode::loading_draft
            || mode_ == Mode::saving || mode_ == Mode::language_saving || mode_ == Mode::history_work) return;
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
                const unsigned count = unsigned(Shortcut::count) - first;
                shortcut_ = Shortcut(first + (unsigned(shortcut_) - first + count + (button == 0 ? -1 : 1)) % count);
            }
            const bool instruments = button == 3 && shortcut_ == Shortcut::setup && docs_.document().instruments();
            if (button == 2 || instruments) {
                if (shortcut_ == Shortcut::pedal) { mode_ = Mode::pedal; item_ = 0; }
                else if (shortcut_ == Shortcut::songs || shortcut_ == Shortcut::setlists) {
                    library_request_ = shortcut_ == Shortcut::songs ? Library::songs : Library::setlists;
                    return;
                }
                else if (shortcut_ == Shortcut::performance && current_set_[0]) begin_startup_set(current_set_);
                else if (shortcut_ == Shortcut::setups) {
                    std::strcpy(name_, docs_.selected());
                    mode_ = Mode::catalogue;
                    if (!name_[0]) browse(false);
                }
                else {
                    if (!docs_.edit_begin()) { hardware_.error(ui::i18n::text(ui::i18n::Message::operation_failed)); return; }
                    original_ = false;
                    changed_fields_ = 0;
                    current_field_ = shortcut_ == Shortcut::performance;
                    patch_ = current_field_ ? current_patch_ : 0;
                    field_ = current_field_ ? 2 : 5;
                    mode_ = draft_return_ = Mode::field;
                    if (instruments) begin_instruments(0, Mode::closed);
                    else if (!current_field_) begin_setup_preferences(Mode::closed);
                }
            }
        } else if (mode_ == Mode::pedal) {
            const bool preferences_available = docs_.global_preferences();
            const bool boot_available = hardware_.boot_firmware() != BootFirmware::unavailable;
            const bool language_available = preferences_available && hardware_.language_count();
            const auto *pedals = hardware_.expression_calibration();
            const bool calibration_available = pedals && pedals->count();
            if (button < 2) {
                do { item_ = (item_ + (button ? 1 : 5)) % 6; }
                while ((item_ == 2 && !preferences_available) || (item_ == 3 && !boot_available)
                    || (item_ == 4 && !language_available) || (item_ == 5 && !calibration_available));
            }
            else if (button == 5) mode_ = Mode::current;
            else if (button == 2 && !item_) begin_startup_set(docs_.startup_set());
            else if (button == 2 && item_ == 1) begin_contact();
            else if (button == 2 && item_ == 2) { preference_index_ = 0; mode_ = Mode::preferences; }
            else if (button == 2 && item_ == 3) {
                boot_draft_ = hardware_.boot_firmware();
                mode_ = device_editor_ = Mode::boot_firmware;
            } else if (button == 2 && item_ == 4 && language_available) {
                std::snprintf(language_draft_, sizeof(language_draft_), "%s", hardware_.current_language());
                mode_ = device_editor_ = Mode::language;
            } else if (button == 2 && item_ == 5 && calibration_available) mode_ = device_editor_ = Mode::calibration;
        } else if (mode_ == Mode::calibration) {
            auto &pedals = *hardware_.expression_calibration();
            if ((button == 2 || button == 5) && pedals.captured()) {
                pedals.pause_capture(true);
                mode_ = Mode::decision; item_ = 2;
            }
            else if (button == 5) leave_device_editor();
            else if (button == 3 && !pedals.calibrating()) {
                calibration_choice_ = 0;
                device_editor_ = Mode::closed;
                mode_ = Mode::calibration_options;
            }
            else if (button == 2 && !pedals.calibrating()) {
                if (docs_.busy() || hardware_.configuration_locked()) hardware_.error(ui::i18n::text(ui::i18n::Message::operation_failed));
                else pedals.calibrate();
            }
        } else if (mode_ == Mode::calibration_options || mode_ == Mode::calibration_delay) {
            calibration_button(button);
        } else if (mode_ == Mode::language) {
            if (button < 2) {
                const unsigned count = hardware_.language_count();
                if (count) {
                    const unsigned selected = language_index();
                    const unsigned index = selected < count ? (selected + (button ? 1 : count - 1)) % count : 0;
                    std::snprintf(language_draft_, sizeof(language_draft_), "%s", hardware_.language_tag(index));
                }
            } else if (button == 2 || button == 5) {
                if (language_changed()) { mode_ = Mode::decision; item_ = 2; }
                else leave_device_editor();
            }
        } else if (mode_ == Mode::boot_firmware) {
            if (button < 2) boot_draft_ = boot_draft_ == BootFirmware::stock ? BootFirmware::cntrl : BootFirmware::stock;
            else if (button == 2 || button == 5) {
                if (boot_draft_ == hardware_.boot_firmware()) leave_device_editor();
                else { mode_ = Mode::decision; item_ = 2; }
            }
        } else if (mode_ == Mode::preferences || mode_ == Mode::preference_value) {
            preference_button(button);
        } else if (mode_ == Mode::neutrals || mode_ == Mode::neutral_fields || mode_ == Mode::neutral_value) {
            neutral_button(button);
        } else if (mode_ == Mode::contact || mode_ == Mode::contact_action) {
            contact_button(button);
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
            } else if (button == 2 || button == 5) leave_device_editor();
        } else if (mode_ == Mode::catalogue) {
            if (button < 2) browse(button == 0);
            else if (button == 2 && name_[0]) {
                if (settings_) { begin_edit(); return; }
                mode_ = Mode::action; item_ = 0;
            } else if (button == 5) {
                if (settings_) mode_ = Mode::current;
                else { close(); return; }
            }
        } else if (mode_ == Mode::action) {
            if (button < 2) item_ = (item_ + 3 + (button == 0 ? -1 : 1)) % 3;
            else if (button == 5) mode_ = Mode::catalogue;
            else if (button == 2) {
                if (item_ == 2) {
                    revision_ = Revision(); mode_ = Mode::history; render(); return;
                }
                if (item_ == 0) {
                    if (!docs_.select(name_)) { hardware_.error(ui::i18n::text(ui::i18n::Message::selection_failed)); return; }
                    if (docs_.busy()) { mode_ = Mode::selecting; render(); }
                    else close();
                    return;
                }
                begin_edit(); return;
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
        } else if (mode_ == Mode::instruments || mode_ == Mode::instrument || mode_ == Mode::instrument_value) {
            instrument_button(button);
        } else if (mode_ == Mode::patch) {
            Document draft;
            if (!docs_.edit_document(draft)) return;
            if (button < 2) {
                patch_ = (patch_ + draft.patches() + (button == 0 ? -1 : 1)) % draft.patches();
                original_ = false;
            }
            else if (button == 2) { mode_ = Mode::field; field_ = 0; original_ = false; }
            else if (button == 3 && settings_) begin_setup_preferences(Mode::patch);
            else if (button == 4) begin_cycle(draft);
            else if (button == 5) finish_draft();
        } else if (mode_ == Mode::field) {
            Document draft;
            if (!docs_.edit_document(draft)) return;
            if (button < 2) {
                if (settings_ && current_field_) return;
                const unsigned first = draft_return_ == Mode::field || !draft.patches() ? 5 : 0;
                const unsigned end = settings_ ? 6 + bool(draft.instruments()) : 8;
                const unsigned count = end - first + (first == 0 && docs_.edit_has_original(patch_));
                const unsigned cursor = field_ == 8 ? end : field_;
                field_ = first + (cursor - first + count + (button == 0 ? -1 : 1)) % count;
                if (field_ == end) field_ = 8;
            }
            else if (button == 2 && field_ == 8) { original_ = !original_; field_ = 0; }
            else if (button == 2 && settings_ && field_ == 5) begin_setup_preferences(Mode::field);
            else if (button == 2 && settings_ && field_ == 6) begin_instruments(0, Mode::field);
            else if (button == 2) mode_ = Mode::value;
            else if (button == 3 && settings_ && current_field_) begin_instruments(u16(draft.patch(patch_)), Mode::field);
            else if (button == 4) begin_cycle(draft);
            else if (button == 5) {
                if (draft_return_ == Mode::patch) mode_ = Mode::patch;
                else finish_draft();
            }
        } else if (mode_ == Mode::value) {
            if (button < 2 && docs_.edit_adjust(patch_, field_, button == 0 ? -1 : 1, original_ && field_ < 5))
                changed_fields_ |= 1u << field_;
            else if (button == 2 || button == 5) mode_ = Mode::field;
        } else if (mode_ == Mode::decision && device_editor_ != Mode::closed) {
            if (button < 2) item_ = (item_ + 3 + (button == 0 ? -1 : 1)) % 3;
            else if (button == 5 || (button == 2 && item_ == 2))
                keep_editing(device_editor_ == Mode::calibration_name ? Mode::contact : device_editor_);
            else if (button == 2) {
                if (!item_ && device_editor_ == Mode::language) {
                    if (hardware_.request_language(language_draft_)) { mode_ = Mode::language_saving; render(); }
                    else hardware_.error(ui::i18n::text(ui::i18n::Message::language_change_rejected));
                    return;
                }
                const bool reload = device_editor_ == Mode::preference_value && preference_reloads();
                if (!item_ && !save_device_editor()) {
                    if (device_editor_ != Mode::boot_firmware) {
                        const char *reason = device_editor_ == Mode::preference_value ? docs_.last_error() : "";
                        hardware_.error(*reason ? reason : ui::i18n::text(ui::i18n::Message::save_failed));
                    }
                    return;
                }
                if (!item_ && reload) { close(); return; }
                leave_device_editor();
            }
        } else if (mode_ == Mode::decision) {
            if (button < 2) item_ = (item_ + 3 + (button == 0 ? -1 : 1)) % 3;
            else if (button == 5) keep_editing(setup_preferences_ ? Mode::preferences : draft_return_);
            else if (button == 2) {
                if (item_ == 0) { if (docs_.edit_save(now)) mode_ = Mode::saving; else { hardware_.error(ui::i18n::text(ui::i18n::Message::save_failed)); return; } }
                else if (item_ == 1) { docs_.edit_cancel(); close(); return; }
                else keep_editing(setup_preferences_ ? Mode::preferences : draft_return_);
            }
        }
        render();
    }
private:
    enum class Mode { closed, current, pedal, preferences, preference_value, startup_set, boot_firmware, language, language_saving, calibration, contact, contact_action, catalogue, action, selecting, loading_draft, patch, field, value, decision, saving, failed, history, comparison, history_work,
                      cycle_instrument, cycle_tag, cycle_confirm, cycle_name, instruments, instrument, instrument_value,
                      calibration_options, calibration_delay, calibration_name, neutrals, neutral_fields, neutral_value };
    Documents &docs_; Hardware &hardware_;
    Mode mode_ = Mode::closed, history_destination_ = Mode::history;
    Revision revision_;
    unsigned changed_patches_ = 0, changed_routes_ = 0;
    bool changed_settings_ = false, changed_formatting_ = false;
    bool locked_ = false, browsing_ = false, original_ = false;
    bool settings_ = false, current_field_ = false;
    unsigned current_patch_ = UINT16_MAX, changed_fields_ = 0;
    enum class Shortcut { performance, setup, setups, songs, setlists, pedal, count };
    Shortcut shortcut_ = Shortcut::setup;
    const Songs *songs_ = nullptr;
    Mode device_editor_ = Mode::closed;
    Mode resume_editor_ = Mode::closed;
    bool return_from_shortcut_ = false;
    bool shortcut_hint_ = false;
    unsigned preference_index_ = 0, preference_route_ = 0, preference_component_ = 0;
    uint32_t preference_value_ = 0, setup_preference_ = 0, preference_step_ = 1;
    bool preference_known_ = false;
    Mode setup_preference_return_ = Mode::closed;
    uint64_t setup_changes_ = 0;
    midi::Parameter neutral_{};
    unsigned neutral_index_ = 0, neutral_output_ = 0, neutral_field_ = 0;
    bool neutral_changed_ = false, neutral_rejected_ = false;
    static constexpr uint8_t neutral_kinds[] = {midi::Parameter::cc7, midi::Parameter::cc14,
        midi::Parameter::nrpn7, midi::Parameter::nrpn14, midi::Parameter::rpn14};
    static constexpr const char *neutral_types[] = {"CC 7-bit", "CC 14-bit", "NRPN 7-bit", "NRPN 14-bit", "RPN 14-bit"};
    uint32_t preference_initial_ = 0;
    bool setup_preferences_ = false, preference_initial_known_ = false, preference_rejected_ = false;
    bool preference_can_inherit_ = false;
    BootFirmware boot_draft_ = BootFirmware::unavailable;
    char language_draft_[sizeof(GlobalSettings::Values::language)] = {};
    Library library_request_ = Library::none;
    unsigned startup_choice_ = 0;
    char startup_draft_[64] = {};
    char current_set_[64] = {};
    char contact_draft_[sizeof(GlobalSettings::Values::name_and_contact)] = {};
    unsigned contact_cursor_ = 0;
    bool contact_shortcut_ = false;
    unsigned calibration_choice_ = 0, calibration_delay_ = 0;
    bool has_performance_shortcut() const { return current_patch_ != UINT16_MAX || current_set_[0]; }
    char name_[64] = {};
    unsigned item_ = 0, patch_ = 0, field_ = 0;
    Mode cycle_return_ = Mode::closed;
    Mode draft_return_ = Mode::field;
    Mode instrument_return_ = Mode::closed;
    unsigned instrument_ = 0;
    unsigned instrument_changed_ = 0;
    bool instrument_output_ = false;
    const char *instrument_feedback_ = nullptr;
    unsigned alias_ = 0, tag_ = CycleCatalogue::all, character_ = 0;
    char cycle_name_[64] = {};
    char suggested_[64] = {};
    void finish_draft() {
        if (settings_ && !changed_fields_ && !setup_changes_) {
            docs_.edit_cancel();
            setup_preferences_ = false;
            mode_ = shortcut_ == Shortcut::setups ? Mode::catalogue : Mode::current;
        } else { mode_ = Mode::decision; item_ = 2; }
    }
    void begin_instruments(unsigned index, Mode back) {
        instrument_ = index;
        instrument_changed_ = 0;
        instrument_return_ = back;
        mode_ = Mode::instruments;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_ui_instrument_button)
#endif
    void instrument_button(unsigned button) {
        Document draft;
        if (!docs_.edit_document(draft) || instrument_ >= draft.instruments()) return;
        instrument_feedback_ = nullptr;
        if (mode_ == Mode::instruments) {
            if (button < 2) {
                const unsigned count = draft.instruments();
                if (count > 1) instrument_changed_ = 0;
                instrument_ = (instrument_ + (button ? 1 : count - 1)) % count;
            } else if (button == 2) { instrument_output_ = false; mode_ = Mode::instrument; }
            else if (button == 5) {
                if (instrument_return_ == Mode::closed) {
                    draft_return_ = Mode::instruments;
                    finish_draft();
                } else mode_ = instrument_return_;
            }
        } else if (mode_ == Mode::instrument) {
            if (button < 2) instrument_output_ = !instrument_output_;
            else if (button == 2) mode_ = Mode::instrument_value;
            else if (button == 5) mode_ = Mode::instruments;
        } else if (button == 2 || button == 5) mode_ = Mode::instrument;
        else if (button < 2) {
            const auto *selected = draft.instrument(instrument_);
            const auto *output = reinterpret_cast<const char *>(selected + 32);
            unsigned channel = selected[64] + 1;
            if (instrument_output_) {
                const auto outputs = hardware_.midi_outputs();
                const auto *current = outputs.find(output);
                if (!current) return;
                const unsigned start = unsigned(current - outputs.data);
                Document fallback = draft;
                fallback.bind_globals(nullptr);
                for (unsigned i = 1; i < outputs.count; ++i) {
                    const auto &candidate = outputs.data[(start + (button ? i : outputs.count - i)) % outputs.count];
                    if (draft.has_output(hardware_, candidate.name) && fallback.has_output(hardware_, candidate.name)) {
                        output = candidate.name;
                        break;
                    }
                }
                if (!std::strcmp(output, reinterpret_cast<const char *>(selected + 32))) {
                    instrument_feedback_ = "No other enabled output";
                    return;
                }
            } else {
                if ((button == 0 && channel == 1) || (button == 1 && channel == 16)) return;
                channel += button ? 1 : -1;
            }
            if (docs_.edit_instrument(instrument_, output, channel)) {
                instrument_changed_ |= 1u << unsigned(instrument_output_);
                changed_fields_ |= 1u << 10;
            } else instrument_feedback_ = "Could not change instrument";
        }
    }
    void begin_cycle(const Document &draft) {
        if (!draft.cycle_catalogue().size()) return;
        cycle_return_ = mode_;
        alias_ = 0;
        while (!draft.cycle_catalogue().count(alias_)) ++alias_;
        mode_ = Mode::cycle_instrument;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_ui_begin_startup_set)
#endif
    void begin_startup_set(const char *initial) {
        startup_choice_ = 0;
        Setlist set;
        for (unsigned i = 0; i < songs_->counts().sets && songs_->set_at(i, set); ++i)
            if (!std::strcmp(initial, set.name)) { startup_choice_ = i + 1; break; }
        std::snprintf(startup_draft_, sizeof(startup_draft_), "%s", initial);
        mode_ = device_editor_ = Mode::startup_set;
    }
    void leave_device_editor() {
        if (device_editor_ == Mode::calibration) hardware_.expression_calibration()->cancel();
        if (return_from_shortcut_ || contact_shortcut_) { close(); return; }
        if (device_editor_ == Mode::calibration_name || device_editor_ == Mode::calibration_delay) {
            mode_ = Mode::calibration_options; device_editor_ = Mode::closed; return;
        }
        if (device_editor_ == Mode::preference_value) {
            mode_ = Mode::preferences; device_editor_ = Mode::closed; return;
        }
        mode_ = shortcut_ == Shortcut::pedal ? Mode::pedal : Mode::current;
        item_ = device_editor_ == Mode::calibration ? 5 : device_editor_ == Mode::language ? 4
            : device_editor_ == Mode::boot_firmware ? 3 : device_editor_ == Mode::contact ? 1 : 0;
        device_editor_ = Mode::closed;
    }
    bool language_changed() const { return std::strcmp(language_draft_, hardware_.current_language()) != 0; }
    unsigned language_index() const {
        const unsigned count = hardware_.language_count();
        for (unsigned i = 0; i < count; ++i)
            if (!std::strcmp(language_draft_, hardware_.language_tag(i))) return i;
        return count;
    }
    const char *language_name() const {
        const unsigned index = language_index();
        return index < hardware_.language_count() ? hardware_.language_name(index) : language_draft_;
    }
    bool save_device_editor() {
        if (device_editor_ == Mode::calibration) {
            auto &pedals = *hardware_.expression_calibration();
            return !docs_.busy() && !hardware_.configuration_locked() && pedals.captured() && pedals.calibrate();
        }
        if (device_editor_ == Mode::calibration_name || device_editor_ == Mode::calibration_delay) {
            auto &pedals = *hardware_.expression_calibration();
            const bool naming = device_editor_ == Mode::calibration_name;
            return !docs_.busy() && !hardware_.configuration_locked()
                && pedals.save_calibration_settings(naming ? pedals.settle_ms() : calibration_delay_,
                    calibration_choice_, naming ? contact_draft_ : nullptr);
        }
        if (device_editor_ == Mode::boot_firmware) {
            if (const char *error = hardware_.save_boot_firmware(boot_draft_)) {
                hardware_.error(!std::strcmp(error, "busy") ? "Pedal busy. Try again."
                    : !std::strcmp(error, "boot-default-journal-unavailable") ? "Boot settings storage unavailable"
                    : "Could not save boot firmware");
                return false;
            }
            return true;
        }
        if (device_editor_ == Mode::contact) return docs_.name_and_contact(contact_draft_);
        if (device_editor_ == Mode::preference_value)
            return docs_.global_change(preference().field, preference_value_, !preference_known_, hardware_.global_settings_busy());
        return (!startup_draft_[0] || songs_->setlist(startup_draft_)) && docs_.startup_set(startup_draft_);
    }
    const ui::Preference &preference() const { return ui::preference_at(preference_index_); }
    bool preference_overridden() const {
        return docs_.document().explicit_settings() & (uint64_t(1) << preference().field);
    }
    bool preference_reloads() const {
        return GlobalSettings::reload(preference().field) && !preference_overridden();
    }
    bool preference_changed() const {
        const auto *values = docs_.global_preferences();
        if (!values) return false;
        const unsigned field = preference().field;
        const bool known = values->known & (uint64_t(1) << field);
        return known != preference_known_ || (known && values->values[field] != preference_value_);
    }
    bool preference_supported(unsigned index) const {
        const unsigned field = ui::preference_at(index).field;
        if (setup_preferences_ && !Documents::setup_preference_editable(field)) return false;
        if (field >= 23 && field <= 26) return hardware_.supports_trs_mode(field - 23, midi::TrsMode::aux);
        if (field >= 19 && field <= 22) {
            for (unsigned mode = 1; mode <= unsigned(midi::TrsMode::aux); ++mode)
                if (hardware_.supports_trs_mode(field - 19, midi::TrsMode(mode))) return true;
            return false;
        }
        return true;
    }
    void begin_setup_preferences(Mode back) {
        setup_preferences_ = true;
        setup_preference_return_ = back;
        preference_index_ = 0;
        mode_ = Mode::preferences;
    }
    bool stage_setup_preference() {
        if (preference_known_ == preference_initial_known_ && preference_value_ == preference_initial_) return true;
        const unsigned field = preference().field;
        preference_rejected_ = !docs_.edit_setup_preference(field, preference_value_, !preference_known_);
        if (preference_rejected_) return false;
        setup_changes_ |= uint64_t(1) << field;
        preference_initial_ = preference_value_;
        preference_initial_known_ = preference_known_;
        return true;
    }
    bool stage_neutral() {
        if (!neutral_changed_) return true;
        neutral_rejected_ = !docs_.edit_loop_neutral(neutral_index_, neutral_output_, &neutral_);
        if (neutral_rejected_) return false;
        setup_changes_ |= uint64_t(1) << 33;
        neutral_changed_ = false;
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_ui_neutral_button)
#endif
    void neutral_button(unsigned button) {
        Document draft;
        if (!docs_.edit_document(draft)) return;
        const auto *record = draft.loop_stop_settings();
        const unsigned count = record ? u16(record + 5) : 0;
        const auto outputs = hardware_.midi_outputs();
        const auto browse_output = [&](unsigned current, bool forward) {
            Document fallback = draft;
            fallback.bind_globals(nullptr);
            for (unsigned i = 1; i <= 16; ++i) {
                const unsigned route = (current + (forward ? i : 16 - i)) % 16;
                const auto *output = outputs.route(route);
                if (output && draft.has_output(hardware_, output->name) && fallback.has_output(hardware_, output->name))
                    return route;
            }
            return 16u;
        };
        neutral_rejected_ = false;
        if (mode_ == Mode::neutrals) {
            if (button < 2) neutral_index_ = (neutral_index_ + (button ? 1 : count)) % (count + 1);
            else if (button == 5) mode_ = Mode::preferences;
            else if (button == 4 && neutral_index_ < count) {
                if (docs_.edit_loop_neutral(neutral_index_, 0, nullptr)) setup_changes_ |= uint64_t(1) << 33;
                else neutral_rejected_ = true;
            } else if (button == 2) {
                neutral_changed_ = neutral_index_ == count;
                if (neutral_changed_) {
                    neutral_output_ = browse_output(15, true);
                    if (neutral_output_ == 16) { neutral_rejected_ = true; return; }
                    neutral_ = {midi::Parameter::cc7, 0, 1, 0};
                } else {
                    const auto *row = record + 7 + 7 * neutral_index_;
                    neutral_output_ = row[0];
                    neutral_ = {row[1], row[2], uint16_t(u16(row + 3)), int(u16(row + 5))};
                }
                neutral_field_ = 0;
                mode_ = Mode::neutral_fields;
            }
        } else if (mode_ == Mode::neutral_fields) {
            if (button < 2) neutral_field_ = (neutral_field_ + (button ? 1 : 4)) % 5;
            else if (button == 2) { preference_step_ = 1; mode_ = Mode::neutral_value; }
            else if (button == 3 || (button == 5 && stage_neutral())) mode_ = Mode::neutrals;
        } else if (button == 2 || button == 5) mode_ = Mode::neutral_fields;
        else if (button == 4 && neutral_field_ >= 3)
            preference_step_ = preference_step_ < 1000 ? preference_step_ * 10 : 1;
        else if (button < 2) {
            const int direction = button ? 1 : -1;
            const int delta = direction * int(preference_step_);
            if (neutral_field_ == 0) {
                const unsigned route = browse_output(neutral_output_, button == 1);
                if (route == 16) { neutral_rejected_ = true; return; }
                neutral_output_ = route;
            } else if (neutral_field_ == 1) {
                unsigned i = 0;
                while (neutral_kinds[i] != neutral_.kind) ++i;
                neutral_.kind = neutral_kinds[(i + (button ? 1 : 4)) % 5];
                neutral_.address = 1;
                neutral_.value = std::min(int(neutral_.value), neutral_.maximum());
            } else if (neutral_field_ == 2) neutral_.channel = uint8_t(std::clamp(int(neutral_.channel) + direction, 0, 15));
            else if (neutral_field_ == 4) neutral_.value = std::clamp(int(neutral_.value) + delta, 0, neutral_.maximum());
            else if (neutral_.kind != midi::Parameter::rpn14) {
                const int maximum = neutral_.kind == midi::Parameter::cc7 ? 119 : neutral_.kind == midi::Parameter::cc14 ? 31 : 16383;
                auto proposed = neutral_;
                for (int n = std::clamp(int(neutral_.address) + delta, 0, maximum); n >= 0 && n <= maximum; n += direction) {
                    proposed.address = uint16_t(n);
                    if (proposed.valid()) { neutral_.address = proposed.address; break; }
                }
            }
            neutral_changed_ = true;
        }
    }
    void browse_preference(bool backward, bool group) {
        const unsigned count = setup_preferences_ ? ui::setup_preferences : ui::pedal_preferences;
        const char *original_group = preference().group;
        for (unsigned i = 0; i < count; ++i) {
            preference_index_ = (preference_index_ + (backward ? count - 1 : 1)) % count;
            if ((!group || std::strcmp(preference().group, original_group)) && preference_supported(preference_index_)) return;
        }
    }
    const midi::OutputConnection *preference_output() const {
        const auto outputs = hardware_.midi_outputs();
        if (preference_route_ >= outputs.count) return nullptr;
        const auto &output = outputs.data[preference_route_];
        if (output.output || (output.port < 4 && (hardware_.supports_trs_mode(output.port, midi::TrsMode::midi_a)
            || hardware_.supports_trs_mode(output.port, midi::TrsMode::midi_b)))) return &output;
        return nullptr;
    }
    void browse_preference_output(bool backward) {
        const unsigned count = hardware_.midi_outputs().count;
        for (unsigned i = 0; i < count; ++i) {
            preference_route_ = (preference_route_ + (backward ? count - 1 : 1)) % count;
            if (preference_output()) return;
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_ui_preference_text)
#endif
    void preference_text(char *text, unsigned capacity, uint32_t value, bool by_output = false) const {
        const auto &field = preference();
        using Kind = ui::PreferenceKind;
        using Message = ui::i18n::Message;
        if (field.kind == Kind::boolean || (!value && ui::optional_tempo_preference(field.field)))
            ui::utf8::copy(text, capacity, ui::i18n::text(value ? Message::on : Message::off));
        else if (field.kind == Kind::choice && value <= field.maximum) ui::utf8::copy(text, capacity, field.choices[value]);
        else if (field.kind == Kind::font)
            ui::utf8::copy(text, capacity, ui::preference_face_names[ui::preference_face_index(value)]);
        else if (field.kind == Kind::color)
            ui::color_caption(text, capacity, uint16_t(value));
        else if (field.kind == Kind::outputs) {
            if (const auto *output = by_output ? preference_output() : nullptr) {
                std::snprintf(text, capacity, "%s  %s", output->label, ui::i18n::text(value & (1u << output->route) ? Message::on : Message::off));
            } else {
                unsigned length = 0;
                for (const auto &output : hardware_.midi_outputs()) {
                    if (!(value & (1u << output.route))) continue;
                    if (length && length + 2 < capacity) { text[length++] = ','; text[length++] = ' '; }
                    ui::utf8::copy(text + length, capacity - length, output.label);
                    length += unsigned(std::strlen(text + length));
                }
                if (!length) ui::utf8::copy(text, capacity, ui::i18n::text(Message::none));
            }
        } else std::snprintf(text, capacity, "%lu", (unsigned long)value);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_ui_preference_button)
#endif
    void preference_button(unsigned button) {
        const auto &field = preference();
        const auto *values = docs_.global_preferences();
        if (!values && !setup_preferences_) return;
        if (mode_ == Mode::preferences) {
            if (button < 2 || button == 3 || button == 4) browse_preference(button == 0 || button == 3, button >= 3);
            else if (button == 5) {
                if (!setup_preferences_) { mode_ = Mode::pedal; item_ = 2; }
                else if (setup_preference_return_ == Mode::closed) finish_draft();
                else { mode_ = setup_preference_return_; setup_preferences_ = false; }
            }
            else if (button == 2) {
                const uint64_t bit = uint64_t(1) << field.field;
                const bool page_field = field.field >= ui::page_preference;
                const bool pedal_known = !page_field && values && (values->known & bit);
                Document draft;
                if (setup_preferences_) {
                    if (!docs_.edit_document(draft)) return;
                    const uint32_t fallback = ui::setup_preference(draft, field.field);
                    preference_initial_known_ = preference_known_ = page_field || (draft.explicit_settings() & bit);
                    preference_can_inherit_ = draft.global_mask_offset() && pedal_known;
                    setup_preference_ = pedal_known ? values->values[field.field] : fallback;
                    preference_initial_ = preference_value_ = preference_known_ ? fallback : setup_preference_;
                    preference_rejected_ = false;
                } else {
                    preference_known_ = pedal_known;
                    setup_preference_ = ui::setup_preference(docs_.document(), field.field);
                    preference_value_ = preference_known_ ? values->values[field.field] : setup_preference_;
                    device_editor_ = Mode::preference_value;
                }
                preference_step_ = 1;
                preference_route_ = 0;
                preference_component_ = 0;
                if (!preference_output()) browse_preference_output(false);
                mode_ = Mode::preference_value;
            }
        } else if (button == 4 && setup_preferences_ && field.field == 33) {
            if (stage_setup_preference()) {
                neutral_index_ = 0;
                neutral_rejected_ = false;
                mode_ = Mode::neutrals;
            }
        } else if (button == 3) {
            if (field.kind == ui::PreferenceKind::color) preference_component_ = (preference_component_ + 1) % 3;
            else if (!setup_preferences_ || preference_can_inherit_) {
                preference_known_ = false; preference_value_ = setup_preference_;
                preference_rejected_ = false;
            }
        } else if (field.kind == ui::PreferenceKind::outputs && button < 3) {
            if (button < 2) browse_preference_output(button == 0);
            else if (const auto *output = preference_output()) {
                preference_value_ ^= uint32_t(1u << output->route); preference_known_ = true;
                preference_rejected_ = false;
            }
        } else if (button == 2 || button == 5) {
            if (setup_preferences_) {
                if ((button == 5 && preference_rejected_) || stage_setup_preference()) mode_ = Mode::preferences;
            } else if (preference_changed()) { mode_ = Mode::decision; item_ = 2; }
            else leave_device_editor();
        } else if (button == 4 && field.kind != ui::PreferenceKind::outputs) {
            const unsigned span = field.kind == ui::PreferenceKind::color
                ? ui::preference_color_max[preference_component_] : field.maximum - field.minimum;
            preference_step_ = preference_step_ * 10 <= span ? preference_step_ * 10 : 1;
        } else if (button < 2) {
            const uint32_t previous = preference_value_;
            if (field.field >= 19 && field.field <= 22) {
                const unsigned count = field.maximum + 1;
                for (unsigned i = 0; i < count; ++i) {
                    preference_value_ = (preference_value_ + (button == 0 ? count - 1 : 1)) % count;
                    if (hardware_.supports_trs_mode(field.field - 19, midi::TrsMode(preference_value_))) break;
                }
            } else {
                const int delta = (button == 0 ? -1 : 1) * int(preference_step_);
                if (field.kind == ui::PreferenceKind::font) {
                    const int index = int(ui::preference_face_index(previous)) + delta;
                    preference_value_ = ui::preference_faces[std::clamp(index, int(field.minimum), int(field.maximum))];
                } else if (field.kind == ui::PreferenceKind::color)
                    preference_value_ = ui::adjust_color(uint16_t(previous), preference_component_, delta);
                else preference_value_ = uint32_t(std::clamp(int64_t(preference_value_) + delta, int64_t(field.minimum), int64_t(field.maximum)));
                if (ui::optional_tempo_preference(field.field)) {
                    if (!previous && button == 1) preference_value_ = 20;
                    else if (preference_value_ < 20) preference_value_ = 0;
                }
            }
            preference_known_ |= preference_value_ != previous;
            preference_rejected_ = false;
        }
    }
    void keep_editing(Mode fallback) {
        if (device_editor_ == Mode::calibration) hardware_.expression_calibration()->pause_capture(false);
        mode_ = return_from_shortcut_ && resume_editor_ != Mode::closed && resume_editor_ != Mode::contact_action && resume_editor_ != Mode::failed
            ? resume_editor_ : fallback;
        return_from_shortcut_ = false;
        resume_editor_ = Mode::closed;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_ui_calibration_button)
#endif
    void calibration_button(unsigned button) {
        const auto &pedals = *hardware_.expression_calibration();
        if (mode_ == Mode::calibration_options) {
            const unsigned count = pedals.count() + 1;
            if (button < 2) calibration_choice_ = (calibration_choice_ + (button ? 1 : count - 1)) % count;
            else if (button == 5) mode_ = device_editor_ = Mode::calibration;
            else if (button == 2 && calibration_choice_ == pedals.count()) {
                calibration_delay_ = pedals.settle_ms();
                preference_step_ = 100;
                mode_ = device_editor_ = Mode::calibration_delay;
            } else if (button == 2) {
                const char *label = pedals.name(calibration_choice_);
                if (std::strlen(label) >= sizeof(contact_draft_)) {
                    hardware_.error(ui::i18n::text(ui::i18n::Message::capacity_exceeded));
                    return;
                }
                std::strcpy(contact_draft_, label);
                contact_cursor_ = 0;
                device_editor_ = Mode::calibration_name;
                mode_ = Mode::contact;
            }
        } else if (button < 2) {
            const int step = int(preference_step_);
            calibration_delay_ = unsigned(std::clamp(int(calibration_delay_) + (button ? step : -step),
                1, int(CalibrationFile::maximum_settle_ms)));
        } else if (button == 4) {
            preference_step_ = preference_step_ == 1000 ? 1 : preference_step_ * 10;
        } else if (button == 2 || button == 5) {
            if (calibration_delay_ != pedals.settle_ms()) { mode_ = Mode::decision; item_ = 2; }
            else leave_device_editor();
        }
    }
    bool text_changed() const {
        const char *original = device_editor_ == Mode::calibration_name ? hardware_.expression_calibration()->name(calibration_choice_)
            : docs_.name_and_contact();
        return std::strcmp(contact_draft_, original) != 0;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_ui_contact_button)
#endif
    void contact_button(unsigned button) {
        char *const position = contact_draft_ + contact_cursor_;
        const char *next = position;
        uint32_t code = 0;
        ui::utf8::next(next, code);
        const unsigned length = unsigned(std::strlen(contact_draft_));
        const bool naming = device_editor_ == Mode::calibration_name;
        if (mode_ == Mode::contact_action) {
            if (button < 2) item_ = naming ? (item_ == 0 ? 2 : 0) : (item_ + (button ? 1 : 2)) % 3;
            else if (button == 5) mode_ = Mode::contact;
            else if (button == 2) {
                if (item_ == 2) std::memmove(position, next, std::strlen(next) + 1);
                else if (length + 1 < sizeof(contact_draft_)) {
                    std::memmove(position + 1, position, std::strlen(position) + 1);
                    *position = item_ ? '\n' : ' ';
                } else { hardware_.error(ui::i18n::text(ui::i18n::Message::capacity_exceeded)); return; }
                mode_ = Mode::contact;
            }
            return;
        }
        if (button == 5) {
            if (text_changed()) { mode_ = Mode::decision; item_ = contact_shortcut_ ? 0 : 2; }
            else leave_device_editor();
        } else if (button == 2) { mode_ = Mode::contact_action; item_ = 0; }
        else if (button == 3 && contact_cursor_) {
            do { --contact_cursor_; } while ((uint8_t(contact_draft_[contact_cursor_]) & 0xc0) == 0x80);
        } else if (button == 4) contact_cursor_ = unsigned(next - contact_draft_);
        else if (button < 2) {
            if (!code && length + 1 >= sizeof(contact_draft_)) {
                hardware_.error(ui::i18n::text(ui::i18n::Message::capacity_exceeded)); return;
            }
            std::memmove(position + 1, next, std::strlen(next) + 1);
            *position = code >= 32 && code <= 126 ? char(32 + (code - 32 + (button ? 1 : 94)) % 95)
                : button ? 'a' : 'A';
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_cycle_name)
#endif
    void propose_cycle_name(const Document &doc) {
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
        Document doc;
        if (!docs_.edit_document(doc)) return;
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
            } else if (button == 2) { propose_cycle_name(doc); mode_ = Mode::cycle_confirm; }
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
                changed_fields_ |= 1u << 9;
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
    void begin_contact() {
        std::strcpy(contact_draft_, docs_.name_and_contact());
        contact_cursor_ = 0;
        mode_ = device_editor_ = Mode::contact;
    }
    void close() { contact_shortcut_ = false; mode_ = Mode::closed; docs_.redraw(); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_ui_begin_edit)
#endif
    void begin_edit() {
        if (!docs_.edit_begin(name_)) { mode_ = Mode::failed; render(); return; }
        if (docs_.busy()) { mode_ = Mode::loading_draft; render(); return; }
        open_draft();
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_ui_open_draft)
#endif
    void open_draft() {
        Document draft;
        if (!docs_.edit_document(draft)) { docs_.edit_cancel(); mode_ = Mode::failed; render(); return; }
        original_ = false;
        current_field_ = false;
        changed_fields_ = 0;
        patch_ = 0;
        field_ = draft.patches() ? 0 : 5;
        mode_ = draft_return_ = draft.patches() ? Mode::patch : Mode::field;
        render();
    }
    void browse(bool backward) {
        if (docs_.browse(name_, backward)) browsing_ = true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_ui_render_neutral)
#endif
    void render_neutral(Pane (&views)[3], const Document &draft, ui::Prompt &prompt) const {
        using Role = ControlRole;
        const auto *record = draft.loop_stop_settings();
        const unsigned count = record ? u16(record + 5) : 0;
        auto &center = views[1].lines;
        auto &right = views[2].lines;
        const auto outputs = hardware_.midi_outputs();
        const char *feedback = neutral_rejected_ ? "Invalid or overlapping value"
            : neutral_changed_ && mode_ != Mode::neutrals ? "Changed" : "";
        ui::copy_caption(right[2], feedback);
        const bool list = mode_ == Mode::neutrals, adjusting = mode_ == Mode::neutral_value;
        if (list && neutral_index_ == count) {
            ui::copy_caption(center[1], "Loop-stop neutral values");
            ui::copy_caption(right[1], "Add neutral value");
            ui::copy_caption(views[0].lines[1], prompt.clear().add(Role::confirm, "Add")
                .add(Role::back, "Back", ", ").text());
            return;
        }
        const auto *row = list ? record + 7 + 7 * neutral_index_ : nullptr;
        const auto *output = outputs.route(row ? row[0] : neutral_output_);
        const char *output_name = output ? output->label : "Unavailable";
        const unsigned kind = row ? row[1] : neutral_.kind;
        unsigned kind_index = 0;
        while (neutral_kinds[kind_index] != kind) ++kind_index;
        if (list) {
            ui::copy_caption(center[1], "Loop-stop neutral values");
            std::snprintf(center[2].text, 64, "%u/%u", neutral_index_ + 1, count);
            ui::copy_caption(right[0], output_name);
            std::snprintf(right[1].text, 64, "%s %u Ch %u = %u", neutral_types[kind_index],
                u16(row + 3), row[2] + 1, u16(row + 5));
            ui::copy_caption(views[0].lines[1], prompt.clear().add(Role::confirm, "Edit")
                .add(Role::back, "Back", ", ").text());
            ui::copy_caption(views[0].lines[2], prompt.clear().add(Role::next_section, "Remove").text());
            return;
        }
        const char *fields[] = {"Output", "Control type", "MIDI channel",
            neutral_.kind == midi::Parameter::cc7 || neutral_.kind == midi::Parameter::cc14 ? "Controller" : "Parameter",
            "Neutral value"};
        ui::copy_caption(center[1], fields[neutral_field_]);
        ui::copy_caption(right[0], "Loop stop");
        if (neutral_field_ < 2) ui::copy_caption(right[1], neutral_field_ ? neutral_types[kind_index] : output_name);
        else std::snprintf(right[1].text, 64, "%d", neutral_field_ == 2 ? neutral_.channel + 1
            : neutral_field_ == 3 ? neutral_.address : int(neutral_.value));
        if (adjusting) {
            ui::copy_caption(views[0].lines[0], prompt.clear().pair(Role::previous, Role::next, "Adjust").text());
            if (neutral_field_ >= 3) {
                char step[16]; std::snprintf(step, sizeof(step), "Step %lu", (unsigned long)preference_step_);
                ui::copy_caption(views[0].lines[2], prompt.clear().add(Role::next_section, step).text());
            }
        } else {
            ui::copy_caption(views[0].lines[1], prompt.clear().add(Role::confirm, "Edit").add(Role::back, "Accept entry", ", ").text());
            ui::copy_caption(views[0].lines[2], prompt.clear().add(Role::previous_section, "Cancel entry").text());
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_ui_render)
#endif
    void render() {
        ui::Prompt prompt(hardware_);
        using Role = ControlRole;
        const char *title = ui::i18n::text(ui::i18n::Message::setups), *value = browsing_ ? ui::i18n::text(ui::i18n::Message::working) : name_[0] ? name_ : ui::i18n::text(ui::i18n::Message::no_setups);
        const char *value_label = ui::i18n::text(ui::i18n::Message::action);
        char number[64] = {}, calibration_title[32] = {};
        const bool adding_cycle = mode_ == Mode::cycle_instrument || mode_ == Mode::cycle_tag
            || mode_ == Mode::cycle_confirm || mode_ == Mode::cycle_name;
        const bool preference_view = mode_ == Mode::preferences || mode_ == Mode::preference_value;
        const bool neutral_view = mode_ == Mode::neutrals || mode_ == Mode::neutral_fields || mode_ == Mode::neutral_value;
        const bool instrument_view = mode_ == Mode::instruments || mode_ == Mode::instrument || mode_ == Mode::instrument_value;
        const bool draft_view = mode_ == Mode::patch || mode_ == Mode::field || mode_ == Mode::value || adding_cycle
            || instrument_view || neutral_view || (setup_preferences_ && preference_view);
        Document draft;
        if (draft_view && !docs_.edit_document(draft)) return;
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
            } else if (shortcut_ == Shortcut::setups) {
                value_label = ui::i18n::text(ui::i18n::Message::setups);
                value = ui::i18n::text(ui::i18n::Message::browse);
            } else if (shortcut_ == Shortcut::songs || shortcut_ == Shortcut::setlists) {
                value_label = ui::i18n::text(shortcut_ == Shortcut::songs ? ui::i18n::Message::songs : ui::i18n::Message::setlists);
                value = ui::i18n::text(ui::i18n::Message::browse);
            } else {
                value_label = ui::i18n::text(ui::i18n::Message::pedal);
                value = ui::i18n::text(ui::i18n::Message::startup_set);
            }
        }
        else if (mode_ == Mode::pedal) {
            title = ui::i18n::text(ui::i18n::Message::pedal);
            value = item_ == 5 ? ui::i18n::text(ui::i18n::Message::calibrate) : item_ == 4 ? ui::i18n::text(ui::i18n::Message::language)
                : item_ == 3 ? "Boot firmware" : item_ == 2 ? "Preferences"
                : ui::i18n::text(item_ ? ui::i18n::Message::owner_contact : ui::i18n::Message::startup_set);
        }
        else if (mode_ == Mode::boot_firmware) {
            title = "Boot firmware";
            value = boot_draft_ == BootFirmware::stock ? "Stock firmware" : "CNTRL";
        }
        else if (mode_ == Mode::language) {
            title = ui::i18n::text(ui::i18n::Message::language);
            value = language_name();
        }
        else if (mode_ == Mode::calibration) title = ui::i18n::text(ui::i18n::Message::calibrate);
        else if (mode_ == Mode::calibration_options || mode_ == Mode::calibration_delay) {
            const auto &pedals = *hardware_.expression_calibration();
            if (calibration_choice_ < pedals.count()) {
                std::snprintf(calibration_title, sizeof(calibration_title), "Jack %u name", calibration_choice_ + 1);
                title = calibration_title;
                value = pedals.name(calibration_choice_);
                if (!*value) value = ui::i18n::text(ui::i18n::Message::none);
            } else {
                title = "Settling time";
                std::snprintf(number, sizeof(number), "%u ms",
                    mode_ == Mode::calibration_delay ? calibration_delay_ : pedals.settle_ms());
                value = number;
            }
            value_label = ui::i18n::text(ui::i18n::Message::pedal);
        }
        else if (preference_view) {
            const auto &field = preference();
            const auto *values = docs_.global_preferences();
            const uint64_t bit = uint64_t(1) << field.field;
            title = field.label;
            const bool editing = mode_ == Mode::preference_value;
            const bool page_field = field.field >= ui::page_preference;
            const bool pedal_known = !page_field && values && (values->known & bit);
            const bool known = editing ? preference_known_
                : setup_preferences_ ? page_field || (draft.explicit_settings() & bit) : pedal_known;
            const auto &owner = setup_preferences_ ? draft : docs_.document();
            const bool use_pedal = setup_preferences_ ? !known && pedal_known : known;
            value_label = use_pedal ? ui::i18n::text(ui::i18n::Message::pedal)
                : setup_preferences_ && !known ? "Compiled value" : ui::i18n::text(ui::i18n::Message::setup);
            const uint32_t display = editing ? preference_value_
                : use_pedal ? values->values[field.field] : ui::setup_preference(owner, field.field);
            preference_text(number, sizeof(number), display, editing);
            value = number;
        }
        else if (mode_ == Mode::contact || mode_ == Mode::contact_action) {
            title = ui::i18n::text(ui::i18n::Message::owner_contact);
            if (device_editor_ == Mode::calibration_name) {
                std::snprintf(calibration_title, sizeof(calibration_title), "Jack %u name", calibration_choice_ + 1);
                title = calibration_title;
            }
            const char *selected = contact_draft_ + contact_cursor_;
            const char *start = selected, *next = selected;
            uint32_t code = 0;
            ui::utf8::next(next, code);
            for (unsigned i = 0; i < 6 && start > contact_draft_ && start[-1] != '\n'; ++i)
                do { --start; } while ((uint8_t(*start) & 0xc0) == 0x80);
            const char *end = next;
            for (unsigned i = 0; i < 6 && *end && *end != '\n' && code != '\n'; ++i) {
                uint32_t following;
                ui::utf8::next(end, following);
            }
            std::snprintf(number, sizeof(number), "%.*s[%.*s]%.*s", int(selected - start), start,
                code == '\n' ? 0 : int(next - selected), selected, int(end - next), next);
            value = number;
            if (mode_ == Mode::contact_action) {
                const ui::i18n::Message actions[] = {ui::i18n::Message::insert_space, ui::i18n::Message::insert_line_break, ui::i18n::Message::delete_character};
                value_label = ui::i18n::text(actions[item_]);
            } else if (code == '\n') value_label = ui::i18n::text(ui::i18n::Message::line_break);
            else value_label = ui::i18n::text(ui::i18n::Message::value);
        }
        else if (mode_ == Mode::startup_set) {
            title = ui::i18n::text(ui::i18n::Message::startup_set);
            value_label = ui::i18n::text(ui::i18n::Message::pedal);
            value = startup_draft_[0] ? startup_draft_ : ui::i18n::text(ui::i18n::Message::none);
        }
        else if (mode_ == Mode::action) { title = name_; value = item_ == 2 ? ui::i18n::text(ui::i18n::Message::history) : item_ ? ui::i18n::text(ui::i18n::Message::edit) : ui::i18n::text(ui::i18n::Message::select); }
        else if (mode_ == Mode::patch) {
            title = reinterpret_cast<const char *>(draft.patch_cycle(u16(draft.patch(patch_) + 2)));
            value = reinterpret_cast<const char *>(draft.patch(patch_) + 12);
            value_label = ui::i18n::text(ui::i18n::Message::patch);
        }
        else if (mode_ == Mode::field && field_ == 8) {
            title = ui::i18n::text(original_ ? ui::i18n::Message::edit_patch_entry : ui::i18n::Message::edit_original_patch);
            value = "";
        } else if (instrument_view) {
            const auto *selected = draft.instrument(instrument_);
            const auto *alias = reinterpret_cast<const char *>(selected);
            const auto *output = reinterpret_cast<const char *>(selected + 32);
            if (const auto *endpoint = hardware_.midi_outputs().find(output)) output = endpoint->label;
            if (mode_ == Mode::instruments) {
                title = "Instruments";
                value_label = alias;
                std::snprintf(number, sizeof(number), "%s, ch %u", output, unsigned(selected[64]) + 1);
                value = number;
            } else {
                title = instrument_output_ ? "MIDI output" : ui::i18n::text(ui::i18n::Message::instrument_channel);
                value_label = alias;
                std::snprintf(number, sizeof(number), "%u", unsigned(selected[64]) + 1);
                value = instrument_output_ ? output : number;
            }
        } else if (mode_ == Mode::field && settings_ && (field_ == 5 || field_ == 6)) {
            title = field_ == 5 ? "Setup fields" : "Instruments";
            value = "";
        } else if (mode_ == Mode::field || mode_ == Mode::value) {
            const char *fields[] = {ui::i18n::text(ui::i18n::Message::program), ui::i18n::text(ui::i18n::Message::bank), ui::i18n::text(ui::i18n::Message::instrument_channel), ui::i18n::text(ui::i18n::Message::volume), ui::i18n::text(ui::i18n::Message::pan), ui::i18n::text(ui::i18n::Message::brightness), ui::i18n::text(ui::i18n::Message::idle_seconds), ui::i18n::text(ui::i18n::Message::usb_midi)};
            title = fields[field_];
            int64_t v = Documents::edit_value(draft, patch_, field_);
            if ((field_ == 1 && v == -1) || (field_ == 3 && v == -1) || (field_ == 4 && v == -65)) ui::i18n::format(number, sizeof(number), ui::i18n::Message::none);
            else if (field_ == 7) std::snprintf(number, sizeof(number), "%s", v ? ui::i18n::text(ui::i18n::Message::on) : ui::i18n::text(ui::i18n::Message::off));
            else if (v >= 0) std::snprintf(number, sizeof(number), "%lu", (unsigned long)v);
            else std::snprintf(number, sizeof(number), "%ld", (long)v);
            value = number;
            value_label = ui::i18n::text(ui::i18n::Message::value);
        } else if (mode_ == Mode::decision) {
            title = ui::i18n::text(ui::i18n::Message::unsaved_edits); value = item_ == 0 ? ui::i18n::text(ui::i18n::Message::save) : item_ == 1 ? ui::i18n::text(ui::i18n::Message::discard) : ui::i18n::text(ui::i18n::Message::cancel);
        } else if (mode_ == Mode::history_work) { title = ui::i18n::text(ui::i18n::Message::history); value = ui::i18n::text(ui::i18n::Message::working); }
        else if (mode_ == Mode::saving || mode_ == Mode::language_saving) { title = ui::i18n::text(ui::i18n::Message::saving); value = ui::i18n::text(ui::i18n::Message::please_wait); }
        else if (mode_ == Mode::selecting) { title = ui::i18n::text(ui::i18n::Message::changing_setup); value = ui::i18n::text(ui::i18n::Message::please_wait); }
        else if (mode_ == Mode::loading_draft) { title = name_; value = ui::i18n::text(ui::i18n::Message::working); }
        else if (mode_ == Mode::failed) { title = ui::i18n::text(ui::i18n::Message::operation_failed); value = docs_.last_error(); }
        if (adding_cycle) {
            const auto &doc = draft;
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
        if (mode_ == Mode::value || mode_ == Mode::instrument_value)
            std::snprintf(views[0].lines[0].text, 64, "%s", prompt.clear().pair(Role::previous, Role::next, ui::i18n::text(ui::i18n::Message::adjust)).text());
        views[0].lines[2].graphics.line_height = views[0].lines[2].graphics.size = 16;
        if (mode_ == Mode::patch || mode_ == Mode::field) {
            prompt.clear();
            if (settings_ && mode_ == Mode::patch) prompt.add(Role::previous_section, "Setup fields");
            if (draft.cycle_catalogue().size()) prompt.add(Role::next_section, "Add cycle");
            ui::copy_caption(views[0].lines[2], prompt.text());
        }
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
            if ((mode_ == Mode::field || mode_ == Mode::value) && field_ == 2 && u16(draft.patch(patch_)) < draft.instruments()) {
                ui::copy_caption(views[2].lines[0], reinterpret_cast<const char *>(draft.instrument(u16(draft.patch(patch_)))));
                ui::copy_caption(views[0].lines[2], ui::i18n::text(ui::i18n::Message::instrument_channel_scope));
            }
            if (mode_ == Mode::current && shortcut_ == Shortcut::setup && docs_.document().instruments())
                ui::copy_caption(views[0].lines[2], prompt.clear().add(Role::previous_section, "Instruments").text());
            if (mode_ == Mode::field && current_field_)
                ui::copy_caption(views[2].lines[2], prompt.clear().add(Role::previous_section, "Instruments").text());
            if (instrument_view && mode_ != Mode::instruments)
                ui::copy_caption(views[0].lines[2], ui::i18n::text(ui::i18n::Message::instrument_channel_scope));
            if (instrument_view && instrument_feedback_)
                ui::copy_caption(views[2].lines[2], instrument_feedback_);
            else if (instrument_view && mode_ != Mode::instruments && (instrument_changed_ & (1u << unsigned(instrument_output_))))
                ui::copy_caption(views[2].lines[2], ui::i18n::text(ui::i18n::Message::changed));
            if (draft_view && (!current_field_ || instrument_view))
                ui::copy_caption(views[1].lines[0], docs_.edit_name());
            if ((mode_ == Mode::field || mode_ == Mode::value) && shortcut_ != Shortcut::setups && (changed_fields_ & (1u << field_)))
                ui::copy_caption(views[2].lines[2], ui::i18n::text(ui::i18n::Message::changed));
            if (device_editor_ == Mode::startup_set)
                ui::copy_caption(views[0].lines[2], ui::i18n::text(ui::i18n::Message::startup_set_scope));
            if (device_editor_ == Mode::boot_firmware) {
                ui::copy_caption(views[0].lines[2], "Applies at next boot");
                if (mode_ == Mode::boot_firmware)
                    ui::copy_caption(views[0].lines[0], prompt.clear().pair(Role::previous, Role::next, ui::i18n::text(ui::i18n::Message::adjust)).text());
            }
            if (mode_ == Mode::language)
                ui::copy_caption(views[0].lines[0], prompt.clear().pair(Role::previous, Role::next, ui::i18n::text(ui::i18n::Message::adjust)).text());
            if (mode_ == Mode::language_saving) {
                views[0].lines[0].text[0] = 0;
                views[0].lines[1].text[0] = 0;
            }
            if (mode_ == Mode::calibration) {
                views[0].lines[0].text[0] = 0;
                views[0].lines[1].text[0] = 0;
                if (!hardware_.expression_calibration()->calibrating())
                    ui::copy_caption(views[0].lines[0], prompt.clear().add(Role::previous_section, "Options").text());
            }
            if (mode_ == Mode::calibration_delay) {
                ui::copy_caption(views[0].lines[0], prompt.clear().pair(Role::previous, Role::next, ui::i18n::text(ui::i18n::Message::adjust)).text());
                char step[24]; std::snprintf(step, sizeof(step), "Step %lu ms", (unsigned long)preference_step_);
                ui::copy_caption(views[0].lines[2], prompt.clear().add(Role::next_section, step).text());
            }
            if (mode_ == Mode::contact) {
                ui::copy_caption(views[0].lines[0], prompt.clear().pair(Role::previous, Role::next, ui::i18n::text(ui::i18n::Message::adjust)).text());
                ui::copy_caption(views[0].lines[1], prompt.clear().add(Role::confirm, ui::i18n::text(ui::i18n::Message::action)).add(Role::back, ui::i18n::text(ui::i18n::Message::back), ", ").text());
                ui::copy_caption(views[0].lines[2], prompt.clear().pair(Role::previous_section, Role::next_section, ui::i18n::text(ui::i18n::Message::position_action)).text());
            }
            if (preference_view) {
                if (!setup_preferences_) ui::copy_caption(views[1].lines[0], preference().group);
                if (!setup_preferences_ && preference_overridden()) {
                    char effective[32];
                    preference_text(effective, sizeof(effective), ui::setup_preference(docs_.document(), preference().field));
                    std::snprintf(views[2].lines[2].text, 64, "%s  %s", docs_.selected(), effective);
                } else if (setup_preferences_) {
                    if (mode_ == Mode::preference_value && preference_rejected_)
                        ui::copy_caption(views[2].lines[2], "Invalid for this setup");
                    else if ((setup_changes_ & (uint64_t(1) << preference().field))
                        || (mode_ == Mode::preference_value && (preference_value_ != preference_initial_
                            || preference_known_ != preference_initial_known_)))
                        ui::copy_caption(views[2].lines[2], ui::i18n::text(ui::i18n::Message::changed));
                    else ui::copy_caption(views[2].lines[2], preference().group);
                }
                if (mode_ == Mode::preferences) {
                    ui::copy_caption(views[0].lines[2], prompt.clear().pair(Role::previous_section, Role::next_section, "Group").text());
                } else {
                    const auto kind = preference().kind;
                    const bool routes = kind == ui::PreferenceKind::outputs;
                    ui::copy_caption(views[0].lines[0], prompt.clear().pair(Role::previous, Role::next,
                        ui::i18n::text(routes ? ui::i18n::Message::browse : ui::i18n::Message::adjust)).text());
                    ui::copy_caption(views[0].lines[1], prompt.clear().add(Role::confirm, routes ? "Toggle output" : ui::i18n::text(ui::i18n::Message::ok))
                        .add(Role::back, setup_preferences_ && preference_rejected_ ? "Cancel value"
                            : ui::i18n::text(ui::i18n::Message::back), ", ").text());
                    prompt.clear();
                    if (kind == ui::PreferenceKind::color) {
                        prompt.add(Role::previous_section, ui::preference_color_channel[preference_component_]);
                        if (hardware_.display_count() >= 3) {
                            const auto color = uint16_t(preference_value_);
                            const uint16_t ink = ui::Raster::luminance(color) < 1790 ? 0xffff : 0;
                            views[2].graphics.background = color;
                            for (auto &line : views[2].lines) line.graphics.foreground = ink;
                        }
                    } else if (!setup_preferences_ || preference_can_inherit_)
                        prompt.add(Role::previous_section, setup_preferences_ ? "Use pedal value" : "Use setup value");
                    if (!routes && preference().maximum - preference().minimum >= 10) {
                        char step[24]; std::snprintf(step, sizeof(step), "Step %lu", (unsigned long)preference_step_);
                        prompt.add(Role::next_section, step);
                    }
                    if (setup_preferences_ && preference().field == 33)
                        prompt.add(Role::next_section, "Neutral values");
                    ui::copy_caption(views[0].lines[2], prompt.text());
                }
            }
            if (neutral_view) render_neutral(views, draft, prompt);
            if (mode_ == Mode::decision) {
                const bool pedal = device_editor_ != Mode::closed;
                ui::copy_caption(views[1].lines[0], pedal ? ui::i18n::text(ui::i18n::Message::pedal) : docs_.edit_name());
                const bool reload = device_editor_ == Mode::preference_value ? preference_reloads() : !pedal && docs_.edit_reloads();
                const auto save = reload ? ui::i18n::Message::save_reload_setup : ui::i18n::Message::save;
                const auto action = item_ == 0 ? save : item_ == 1 ? ui::i18n::Message::discard : ui::i18n::Message::keep_editing;
                ui::copy_caption(views[2].lines[1], ui::i18n::text(action));
                if (device_editor_ == Mode::startup_set)
                    ui::copy_caption(views[2].lines[2], startup_draft_[0] ? startup_draft_ : ui::i18n::text(ui::i18n::Message::none));
                else if (device_editor_ == Mode::contact)
                    ui::copy_caption(views[2].lines[2], ui::i18n::text(ui::i18n::Message::owner_contact));
                else if (device_editor_ == Mode::boot_firmware) {
                    ui::copy_caption(views[2].lines[0], "Boot firmware");
                    ui::copy_caption(views[2].lines[2], boot_draft_ == BootFirmware::stock ? "Stock firmware" : "CNTRL");
                }
                else if (device_editor_ == Mode::language) {
                    ui::copy_caption(views[2].lines[0], ui::i18n::text(ui::i18n::Message::language));
                    ui::copy_caption(views[2].lines[2], language_name());
                }
                else if (device_editor_ == Mode::calibration)
                    ui::copy_caption(views[2].lines[2], ui::i18n::text(ui::i18n::Message::calibrate));
                else if (device_editor_ == Mode::calibration_name) {
                    std::snprintf(views[2].lines[0].text, 64, "Jack %u name", calibration_choice_ + 1);
                    ui::copy_caption(views[2].lines[2], contact_draft_[0] ? contact_draft_ : ui::i18n::text(ui::i18n::Message::none));
                } else if (device_editor_ == Mode::calibration_delay) {
                    ui::copy_caption(views[2].lines[0], "Settling time");
                    std::snprintf(views[2].lines[2].text, 64, "%u ms", calibration_delay_);
                }
                else if (device_editor_ == Mode::preference_value) {
                    ui::copy_caption(views[2].lines[0], preference().label);
                    preference_text(views[2].lines[2].text, sizeof(views[2].lines[2].text), preference_value_);
                }
                if (reload) ui::copy_caption(views[0].lines[2], ui::i18n::text(ui::i18n::Message::reload_stops_playback));
            }
        }
        if (settings_ && shortcut_hint_) {
            auto &line = views[0].lines[3];
            line.graphics.line_height = line.graphics.size = 16;
            line.wrap = true;
            std::snprintf(line.text, sizeof(line.text), "%s+%s %s", hardware_.control_label(Role::confirm),
                hardware_.control_label(Role::back), ui::i18n::text(ui::i18n::Message::back));
        }
        ui::present(hardware_, views, 1);
    }
};
}
