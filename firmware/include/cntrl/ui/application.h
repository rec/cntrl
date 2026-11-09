#pragma once
#include <cntrl/controller.h>
#include <cntrl/documents.h>
#include <cntrl/module_menu.h>
#include <cntrl/setup_ui.h>
#include <cntrl/songs_ui.h>
#include "controls.h"
#include <array>
#include <new>

namespace cntrl::ui {
struct ApplicationServices {
    virtual ~ApplicationServices() = default;
    virtual uint32_t application_time() const = 0;
    virtual void blank_performance() = 0;
    virtual bool output_idle() const = 0;
    virtual bool song_transfer_busy() const = 0;
    virtual bool switch_editor_open() const = 0;
    virtual void open_switch_editor(const Document &, uint64_t held) = 0;
    virtual bool shift_switch_editor(bool backwards) = 0;
};
// Coordinates configured pages and menus independently of controller
// storage placement and the platform's physical controls.
class Application {
public:
    Application(Hardware &hardware, Documents &documents, Controller &primary, void *extra,
                Songs &songs, SongCapture &capture, bool switches)
        : menu(hardware, names_.data(), switches ? 3 : 2), setups(documents, hardware),
          song_ui(songs, hardware, capture), hardware_(hardware), documents_(documents), primary_(primary),
          extra_(static_cast<unsigned char *>(extra)), songs_(songs), has_switches_(switches) {
        names_[0] = "Setups"; names_[1] = "Songs and setlists";
        if (switches) names_[2] = "Switches";
    }
    Controller &at(unsigned index) { return index ? *std::launder(reinterpret_cast<Controller *>(extra_ + (index - 1) * sizeof(Controller))) : primary_; }
    unsigned rendering() const { return processing_index < PageLayout::max_controllers ? processing_index : visible_index; }
    Controller &controller() { return at(rendering()); }
    bool visible() const { return layout.controllers() && rendering() == visible_index; }
    bool busy() {
        for (unsigned i = 0; i < layout.controllers(); ++i) if (at(i).busy()) return true;
        return primary_.busy();
    }
    bool patch_pending() { return busy() || songs_.executing() || songs_.busy(); }
    bool feature_open(const ApplicationServices &services) const { return menu.open() || setups.open() || song_ui.open() || services.switch_editor_open(); }
    void bindings_enter() {
        binding_return_songs = binding_return_songs || song_ui.open();
        controller().cancel_gesture(); menu.leave(); song_ui.leave();
        controls_locked = false;
    }
    void brightness(uint8_t value) { for (unsigned i = 0; i < (layout.controllers() ? layout.controllers() : 1); ++i) at(i).runtime_brightness(value); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_configure)
#endif
    void configure(const Document &document, ApplicationServices &services) {
        layout = document.valid() ? document.page_layout() : PageLayout{}; visible_index = 0; pending_page = PageLayout::max_pages;
        for (unsigned i = 1; i < PageLayout::max_controllers; ++i) {
            processing_index = i;
            if (i < layout.controllers()) at(i).activate(document, false); else at(i).deactivate();
        }
        processing_index = PageLayout::max_controllers; services.blank_performance();
        for (unsigned i = 0; i < layout.pages(); ++i) names_[page_offset() + i] = layout.name(i);
        menu.configure(page_offset() + layout.pages());
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_refresh)
#endif
    void refresh(ApplicationServices &services) {
        const unsigned previous = processing_index; processing_index = visible_index;
        services.blank_performance(); if (layout.controllers()) at(visible_index).refresh(); processing_index = previous;
    }
    void tick(uint32_t now) {
        if (!hardware_.relays.tick(now)) hardware_.error("Relay pulse restore failed");
        for (unsigned i = 0; i < (layout.controllers() ? layout.controllers() : 1); ++i) { processing_index = i; at(i).tick(now); }
        processing_index = PageLayout::max_controllers;
        if (next_song_patch_ && !patch_pending()) {
            next_song_patch_ = false;
            if (!songs_.next()) hardware_.error(songs_.last_error());
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_state)
#endif
    bool state(unsigned operation, uint32_t value, ApplicationServices &services) {
        if (operation == 1) { hardware_.set_brightness(uint8_t(value)); return true; }
        if (operation == 2) { hardware_.set_midi_interface(value != 0); return true; }
        if (operation == 11 || operation == 12) return services.shift_switch_editor(operation == 11);
        if (operation >= 3 && operation <= 8) {
            if (!layout.controllers()) return false;
            if ((operation == 7 || operation == 8) && (songs_.executing() || documents_.busy() || documents_.dirty())) return false;
            return controller().navigate(operation);
        }
        if (operation == 9 || operation == 10) { pending_operation = operation; return true; }
        if (operation == 13) {
            if (next_song_patch_) return false;
            next_song_patch_ = true; return true;
        }
        return operation_ui(Operation(operation), services);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_operation_ui)
#endif
    bool operation_ui(Operation operation, ApplicationServices &services) {
        switch (operation) {
        case Operation::menu_previous:
        case Operation::menu_next:
            if (!menu.open()) return false;
            menu.button(operation == Operation::menu_previous ? 0 : 1, true); return true;
        case Operation::menu_back:
            if (!menu.open()) return false;
            if (menu.button(5, true) == -2) { documents_.redraw(); controls_locked = true; } return true;
        case Operation::menu_open:
            binding_return_songs = false;
            controller().cancel_gesture(); song_ui.leave(); menu.enter(); controls_locked = true; return true;
        case Operation::performance_show:
            controller().cancel_gesture(); menu.leave(); song_ui.leave();
            if (binding_return_songs) { binding_return_songs = false; song_ui.perform(); }
            else documents_.redraw();
            controls_locked = true; return true;
        case Operation::menu_activate: {
            if (!menu.open()) return false;
            const int selected = menu.button(2, true);
            if (selected == -1) return true;
            if (selected >= int(page_offset()) && unsigned(selected) - page_offset() < layout.pages()) {
                at(visible_index).cancel_gesture(); pending_page = unsigned(selected) - page_offset(); controls_locked = true; return true;
            }
            if (selected == 1) {
                if (!documents_.busy() && !services.song_transfer_busy() && services.output_idle()) { song_ui.enter(); controls_locked = true; }
                else menu.enter();
            } else if (selected == 2 && has_switches_) {
                if (!busy() && !documents_.busy() && !documents_.dirty() && !services.song_transfer_busy() && services.output_idle() && documents_.document().valid())
                    services.open_switch_editor(documents_.document(), held);
                else menu.enter();
            } else if (!selected) {
                if (documents_.busy()) menu.enter(); else { setups.enter(); controls_locked = true; }
            }
            return true;
        }
        case Operation::setup_previous:
        case Operation::setup_next:
        case Operation::setup_confirm:
        case Operation::setup_back: {
            if (!setups.open() || documents_.busy()) return false;
            const unsigned button = operation == Operation::setup_previous ? 0 : operation == Operation::setup_next ? 1 : operation == Operation::setup_confirm ? 2 : 5;
            setups.button(button, true, services.application_time()); return true;
        }
        case Operation::songs_previous:
        case Operation::songs_next:
        case Operation::songs_confirm:
        case Operation::songs_previous_section:
        case Operation::songs_next_section:
        case Operation::songs_back: {
            if (!song_ui.open()) return false;
            const unsigned button = operation == Operation::songs_previous ? 0 : operation == Operation::songs_next ? 1 : operation == Operation::songs_confirm ? 2 : operation == Operation::songs_previous_section ? 3 : operation == Operation::songs_next_section ? 4 : 5;
            song_ui.button(button, true); if (!song_ui.open()) { documents_.redraw(); controls_locked = true; } return true;
        }
        case Operation::switches_return:
            if (!has_switches_) return false;
            services.open_switch_editor(documents_.document(), 0); controls_locked = true; return true;
        default: return false;
        }
    }
    PageLayout layout;
    unsigned visible_index = 0, processing_index = PageLayout::max_controllers;
    unsigned pending_page = PageLayout::max_pages;
    unsigned pending_operation = 0;
    bool return_to_switches = false, controls_locked = false;
    bool binding_return_songs = false;
    uint64_t held = 0;
private:
    std::array<const char *, 3 + PageLayout::max_pages> names_{};
public:
    ModuleMenu menu;
    SetupUi setups;
    SongsUi song_ui;
private:
    Hardware &hardware_;
    Documents &documents_;
    Controller &primary_;
    unsigned char *extra_;
    Songs &songs_;
    bool has_switches_;
    bool next_song_patch_ = false;
    unsigned page_offset() const { return has_switches_ ? 3 : 2; }
};
} // namespace cntrl::ui
