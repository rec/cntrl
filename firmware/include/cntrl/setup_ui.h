#pragma once
#include <algorithm>
#include "ui/messages.h"
#include "ui/fonts.h"
#include "ui/presentation.h"
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
        mode_ = Mode::catalogue; locked_ = true;
        std::snprintf(name_, sizeof(name_), "%s", docs_.selected());
        if (!name_[0]) browse(false);
        render();
    }
    bool enter_settings(const Songs &songs, bool performing, bool shortcut_hint = false) {
        if (open() || docs_.busy() || docs_.dirty() || !docs_.document().valid()) return false;
        settings_ = true;
        shortcut_hint_ = shortcut_hint;
        songs_ = &songs;
        std::snprintf(current_set_, sizeof(current_set_), "%s", performing ? songs.active_set() : "");
        shortcut_ = has_performance_shortcut() ? Shortcut::performance : Shortcut::setup;
        device_editor_ = Mode::closed;
        resume_editor_ = Mode::closed;
        setup_brightness_ = false;
        draft_changed_ = false;
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
        if (setup_brightness_ && mode_ == Mode::brightness && !stage_brightness()) return true;
        const bool changed = device_editor_ == Mode::contact || device_editor_ == Mode::calibration_name ? text_changed()
            : device_editor_ == Mode::startup_set ? std::strcmp(startup_draft_, docs_.startup_set()) != 0
            : device_editor_ == Mode::brightness ? brightness_changed()
            : device_editor_ == Mode::boot_firmware ? boot_draft_ != hardware_.boot_firmware()
            : device_editor_ == Mode::language ? language_changed()
            : device_editor_ == Mode::calibration ? hardware_.expression_calibration()->captured()
            : device_editor_ == Mode::calibration_delay ? calibration_delay_ != hardware_.expression_calibration()->settle_ms()
            : docs_.dirty() && draft_changed_;
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
        if (!down || locked_ || browsing_ || mode_ == Mode::selecting
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
                    draft_changed_ = false;
                    if (instruments) {
                        setup_brightness_ = false;
                        instrument_ = 0;
                        instrument_feedback_ = nullptr;
                        mode_ = Mode::instruments;
                    }
                    else begin_brightness(true);
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
            else if (button == 2 && item_ == 2) begin_brightness(false);
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
        } else if (mode_ == Mode::brightness) {
            brightness_button(button);
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
                mode_ = Mode::action; item_ = 0;
            } else if (button == 5) {
                if (settings_) mode_ = Mode::current;
                else { close(); return; }
            }
        } else if (mode_ == Mode::action) {
            if (button < 2) item_ = 1 - item_;
            else if (button == 5) mode_ = Mode::catalogue;
            else if (button == 2) {
                if (item_) {
                    revision_ = Revision(); mode_ = Mode::history; render(); return;
                }
                if (!docs_.select(name_)) { hardware_.error(ui::i18n::text(ui::i18n::Message::selection_failed)); return; }
                if (docs_.busy()) { mode_ = Mode::selecting; render(); }
                else close();
                return;
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
        } else if (mode_ == Mode::instruments || mode_ == Mode::instrument_value) {
            instrument_button(button);
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
                if (!item_ && !save_device_editor()) {
                    if (device_editor_ != Mode::boot_firmware) {
                        const char *reason = device_editor_ == Mode::brightness ? docs_.last_error() : "";
                        hardware_.error(*reason ? reason : ui::i18n::text(ui::i18n::Message::save_failed));
                    }
                    return;
                }
                leave_device_editor();
            }
        } else if (mode_ == Mode::decision) {
            if (button < 2) item_ = (item_ + 3 + (button == 0 ? -1 : 1)) % 3;
            else if (button == 5) keep_editing(draft_editor());
            else if (button == 2) {
                if (item_ == 0) { if (docs_.edit_save(now)) mode_ = Mode::saving; else { hardware_.error(ui::i18n::text(ui::i18n::Message::save_failed)); return; } }
                else if (item_ == 1) { docs_.edit_cancel(); close(); return; }
                else keep_editing(draft_editor());
            }
        }
        render();
    }
private:
    enum class Mode { closed, current, pedal, brightness, startup_set, boot_firmware, language, language_saving,
                      calibration, contact, contact_action, catalogue, action, selecting, decision, saving,
                      failed, history, comparison, history_work, instruments, instrument_value,
                      calibration_options, calibration_delay, calibration_name };
    Documents &docs_; Hardware &hardware_;
    Mode mode_ = Mode::closed, history_destination_ = Mode::history;
    Revision revision_;
    unsigned changed_patches_ = 0, changed_routes_ = 0;
    bool changed_settings_ = false, changed_formatting_ = false;
    bool locked_ = false, browsing_ = false, settings_ = false, draft_changed_ = false;
    enum class Shortcut { performance, setup, setups, songs, setlists, pedal, count };
    Shortcut shortcut_ = Shortcut::setup;
    const Songs *songs_ = nullptr;
    Mode device_editor_ = Mode::closed;
    Mode resume_editor_ = Mode::closed;
    bool return_from_shortcut_ = false;
    bool shortcut_hint_ = false;
    unsigned brightness_ = 0, compiled_brightness_ = 0, brightness_step_ = 10;
    bool brightness_known_ = false, setup_brightness_ = false;
    BootFirmware boot_draft_ = BootFirmware::unavailable;
    char language_draft_[sizeof(GlobalSettings::Values::language)] = {};
    Library library_request_ = Library::none;
    unsigned startup_choice_ = 0;
    char startup_draft_[64] = {};
    char current_set_[64] = {};
    char contact_draft_[sizeof(GlobalSettings::Values::name_and_contact)] = {};
    unsigned contact_cursor_ = 0;
    bool contact_shortcut_ = false;
    unsigned calibration_choice_ = 0, calibration_delay_ = 0, calibration_step_ = 100;
    bool has_performance_shortcut() const { return current_set_[0]; }
    char name_[64] = {};
    unsigned item_ = 0, instrument_ = 0;
    const char *instrument_feedback_ = nullptr;
    Mode draft_editor() const { return setup_brightness_ ? Mode::brightness : Mode::instruments; }
    void finish_draft() {
        if (draft_changed_) { mode_ = Mode::decision; item_ = 2; }
        else { docs_.edit_cancel(); mode_ = Mode::current; }
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
                instrument_ = (instrument_ + (button ? 1 : count - 1)) % count;
            } else if (button == 2) mode_ = Mode::instrument_value;
            else if (button == 5) finish_draft();
        } else if (button == 2 || button == 5) mode_ = Mode::instruments;
        else if (button < 2) {
            const unsigned channel = draft.instrument(instrument_)[64] + 1;
            if ((button == 0 && channel == 1) || (button == 1 && channel == 16)) return;
            if (docs_.edit_instrument_channel(instrument_, channel + (button ? 1 : -1))) draft_changed_ = true;
            else instrument_feedback_ = "Could not change instrument channel";
        }
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
        if (device_editor_ == Mode::brightness) {
            mode_ = Mode::pedal; item_ = 2; device_editor_ = Mode::closed; return;
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
        if (device_editor_ == Mode::brightness)
            return docs_.global_change(0, brightness_, !brightness_known_, hardware_.global_settings_busy());
        return (!startup_draft_[0] || songs_->setlist(startup_draft_)) && docs_.startup_set(startup_draft_);
    }
    bool brightness_changed() const {
        const auto *values = docs_.global_preferences();
        const bool known = values && (values->known & 1);
        return known != brightness_known_ || (known && values->values[0] != brightness_);
    }
    void begin_brightness(bool setup) {
        setup_brightness_ = setup;
        const auto *values = docs_.global_preferences();
        Document source = docs_.document();
        source.bind_globals(nullptr);
        compiled_brightness_ = source.brightness();
        brightness_known_ = setup ? bool(source.explicit_settings() & 1) : values && (values->known & 1);
        brightness_ = !setup && brightness_known_ ? values->values[0] : docs_.document().brightness();
        brightness_step_ = 10;
        mode_ = Mode::brightness;
        device_editor_ = setup ? Mode::closed : Mode::brightness;
    }
    bool stage_brightness() {
        Document draft;
        if (!docs_.edit_document(draft)) return false;
        if (brightness_known_ == bool(draft.explicit_settings() & 1)
            && (!brightness_known_ || draft.brightness() == brightness_)) return true;
        if (!docs_.edit_brightness(brightness_, !brightness_known_)) {
            hardware_.error(ui::i18n::text(ui::i18n::Message::operation_failed));
            return false;
        }
        draft_changed_ = true;
        return true;
    }
    void brightness_button(unsigned button) {
        if (button < 2) {
            const unsigned next = unsigned(std::clamp(int(brightness_) + (button ? 1 : -1) * int(brightness_step_), 0, 255));
            brightness_known_ |= next != brightness_;
            brightness_ = next;
        } else if (button == 3) {
            if (setup_brightness_ && !docs_.document().global_mask_offset()) return;
            brightness_known_ = false;
            const auto *values = docs_.global_preferences();
            brightness_ = setup_brightness_ && values && (values->known & 1) ? values->values[0] : compiled_brightness_;
        } else if (button == 4) brightness_step_ = brightness_step_ == 1 ? 10 : 1;
        else if (setup_brightness_) {
            if (stage_brightness()) finish_draft();
        } else if (brightness_changed()) { mode_ = Mode::decision; item_ = 2; }
        else leave_device_editor();
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
                calibration_step_ = 100;
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
            const int step = int(calibration_step_);
            calibration_delay_ = unsigned(std::clamp(int(calibration_delay_) + (button ? step : -step),
                1, int(CalibrationFile::maximum_settle_ms)));
        } else if (button == 4) {
            calibration_step_ = calibration_step_ == 1000 ? 1 : calibration_step_ * 10;
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
    void begin_contact() {
        std::strcpy(contact_draft_, docs_.name_and_contact());
        contact_cursor_ = 0;
        mode_ = device_editor_ = Mode::contact;
    }
    void close() { contact_shortcut_ = false; mode_ = Mode::closed; docs_.redraw(); }
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
        char number[64] = {}, calibration_title[32] = {};
        const bool brightness_view = mode_ == Mode::brightness;
        const bool instrument_view = mode_ == Mode::instruments || mode_ == Mode::instrument_value;
        const bool draft_view = instrument_view || (setup_brightness_ && brightness_view);
        Document draft;
        if (draft_view && !docs_.edit_document(draft)) return;
        if (mode_ == Mode::current) {
            title = ui::i18n::text(ui::i18n::Message::current);
            if (shortcut_ == Shortcut::performance && current_set_[0]) {
                value_label = ui::i18n::text(ui::i18n::Message::setlist);
                value = current_set_;
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
                : item_ == 3 ? "Boot firmware" : item_ == 2 ? ui::i18n::text(ui::i18n::Message::brightness)
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
        else if (brightness_view) {
            title = ui::i18n::text(ui::i18n::Message::brightness);
            value_label = ui::i18n::text(setup_brightness_ ? ui::i18n::Message::setup : ui::i18n::Message::pedal);
            std::snprintf(number, sizeof(number), "%u", brightness_);
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
        else if (mode_ == Mode::action) {
            title = name_;
            value = ui::i18n::text(item_ ? ui::i18n::Message::history : ui::i18n::Message::select);
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
                title = ui::i18n::text(ui::i18n::Message::instrument_channel);
                value_label = alias;
                std::snprintf(number, sizeof(number), "%u", unsigned(selected[64]) + 1);
                value = number;
            }
        } else if (mode_ == Mode::decision) {
            title = ui::i18n::text(ui::i18n::Message::unsaved_edits); value = item_ == 0 ? ui::i18n::text(ui::i18n::Message::save) : item_ == 1 ? ui::i18n::text(ui::i18n::Message::discard) : ui::i18n::text(ui::i18n::Message::cancel);
        } else if (mode_ == Mode::history_work) { title = ui::i18n::text(ui::i18n::Message::history); value = ui::i18n::text(ui::i18n::Message::working); }
        else if (mode_ == Mode::saving || mode_ == Mode::language_saving) { title = ui::i18n::text(ui::i18n::Message::saving); value = ui::i18n::text(ui::i18n::Message::please_wait); }
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
        if (brightness_view || mode_ == Mode::instrument_value)
            std::snprintf(views[0].lines[0].text, 64, "%s", prompt.clear().pair(Role::previous, Role::next, ui::i18n::text(ui::i18n::Message::adjust)).text());
        views[0].lines[2].graphics.line_height = views[0].lines[2].graphics.size = 16;
        if (mode_ == Mode::catalogue) {
            ui::copy_caption(views[1].lines[0], ui::i18n::text(ui::i18n::Message::setups));
            std::snprintf(views[1].lines[1].text, 64, "%s", value);
            ui::copy_caption(views[2].lines[0], ui::i18n::text(ui::i18n::Message::setup_menu));
            ui::copy_caption(views[2].lines[1], ui::i18n::text(ui::i18n::Message::no_midi_sent));
        } else {
            ui::copy_caption(views[1].lines[0], ui::i18n::text(mode_ == Mode::action ? ui::i18n::Message::setup : ui::i18n::Message::field));
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
            if (mode_ == Mode::current && shortcut_ == Shortcut::setup) {
                prompt.clear().add(Role::confirm, ui::i18n::text(ui::i18n::Message::brightness));
                if (docs_.document().instruments()) prompt.add(Role::previous_section, "Instruments");
                ui::copy_caption(views[0].lines[2], prompt.text());
            }
            if (instrument_view && mode_ != Mode::instruments)
                ui::copy_caption(views[0].lines[2], ui::i18n::text(ui::i18n::Message::instrument_channel_scope));
            if (instrument_view && instrument_feedback_)
                ui::copy_caption(views[2].lines[2], instrument_feedback_);
            if (draft_view) ui::copy_caption(views[1].lines[0], docs_.edit_name());
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
                char step[24]; std::snprintf(step, sizeof(step), "Step %u ms", calibration_step_);
                ui::copy_caption(views[0].lines[2], prompt.clear().add(Role::next_section, step).text());
            }
            if (mode_ == Mode::contact) {
                ui::copy_caption(views[0].lines[0], prompt.clear().pair(Role::previous, Role::next, ui::i18n::text(ui::i18n::Message::adjust)).text());
                ui::copy_caption(views[0].lines[1], prompt.clear().add(Role::confirm, ui::i18n::text(ui::i18n::Message::action)).add(Role::back, ui::i18n::text(ui::i18n::Message::back), ", ").text());
                ui::copy_caption(views[0].lines[2], prompt.clear().pair(Role::previous_section, Role::next_section, ui::i18n::text(ui::i18n::Message::position_action)).text());
            }
            if (brightness_view) {
                prompt.clear();
                if (!setup_brightness_ || docs_.document().global_mask_offset())
                    prompt.add(Role::previous_section, setup_brightness_ ? "Use pedal value" : "Use setup value");
                char step[16]; std::snprintf(step, sizeof(step), "Step %u", brightness_step_);
                ui::copy_caption(views[0].lines[2], prompt.add(Role::next_section, step).text());
                if (!setup_brightness_ && (docs_.document().explicit_settings() & 1))
                    std::snprintf(views[2].lines[2].text, 64, "%s  %u", docs_.selected(), unsigned(docs_.document().brightness()));
            }
            if (mode_ == Mode::decision) {
                const bool pedal = device_editor_ != Mode::closed;
                ui::copy_caption(views[1].lines[0], pedal ? ui::i18n::text(ui::i18n::Message::pedal) : docs_.edit_name());
                const bool reload = !pedal && docs_.edit_reloads();
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
                else if (device_editor_ == Mode::brightness) {
                    ui::copy_caption(views[2].lines[0], ui::i18n::text(ui::i18n::Message::brightness));
                    std::snprintf(views[2].lines[2].text, 64, "%u", brightness_);
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
