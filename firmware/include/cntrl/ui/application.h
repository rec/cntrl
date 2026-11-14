#pragma once
#include <cntrl/controller.h>
#include <cntrl/documents.h>
#include <cntrl/module_menu.h>
#include <cntrl/setup_ui.h>
#include <cntrl/songs_ui.h>
#include <cntrl/switches.h>
#include <cntrl/banked_page.h>
#include "controls.h"
#include "pages.h"
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
#if UINTPTR_MAX == UINT32_MAX
    static_assert(sizeof(Pages::ViewState) <= 320, "Update layout.estimate_ram for larger page state");
    static_assert(sizeof(BankedPage) <= 1024, "Update banked.estimate_ram for larger banked page state");
#endif
    Application(Hardware &hardware, Documents &documents, Controller &primary, PatchOutputs &outputs, PatchSources &sources,
                Songs &songs, SongCapture &capture, bool switches)
        : menu(hardware, tool_names_.data(), switches ? 3 : 2), setups(documents, hardware),
          song_ui(songs, hardware, capture), hardware_(hardware), documents_(documents), primary_(primary),
          outputs_(outputs), sources_(sources), songs_(songs), has_switches_(switches) {}
    ~Application() { destroy_banked(pending_banked_); destroy_banked(banked_); destroy_controllers(pending_controllers_); destroy_controllers(controllers_); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_prepare_layout)
#endif
    bool prepare_layout(PageLayout candidate) {
        prepared_ = false;
        destroy_controllers(pending_controllers_);
        pending_names_.clear();
        pending_labels_.release(); pending_views_.release();
        if (!pending_views_.resize(candidate.pages()) || !pending_labels_.resize(candidate.regions() + candidate.pages()) || !allocation.prepare(candidate)) return false;
        if (candidate.pages()) {
            if (!pending_names_.resize((page_offset() + candidate.pages() + candidate.regions()) * sizeof(const char *))) return false;
            auto **names = reinterpret_cast<const char **>(pending_names_.data);
            for (unsigned i = 0; i < page_offset(); ++i) names[i] = tool_names_[i];
            for (unsigned i = 0; i < candidate.regions() + candidate.pages(); ++i) names[page_offset() + i] = pending_labels_[i].data();
        }
        const unsigned count = candidate.controllers();
        const unsigned extras = count ? count - 1 : 0;
        if (extras == controllers_.size / sizeof(Controller)) return true;
        if (!pending_controllers_.resize(extras * sizeof(Controller))) return false;
        for (unsigned i = 0; i < extras; ++i)
            new (pending_controllers_.data + i * sizeof(Controller)) Controller(hardware_, outputs_, sources_, Switches::capacity + i + 1);
        prepared_ = true;
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_prepare_pages)
#endif
    bool prepare_pages(const Document &document) {
        destroy_banked(pending_banked_);
        if (!prepare_layout(document.valid() ? document.page_layout() : PageLayout{})) return false;
        if (document.valid() && document.banked_pages().count()) {
            if (!pending_banked_.resize(document.page_layout().pages())) return false;
            for (unsigned i = 0; i < pending_banked_.size(); ++i) if (document.page_layout().page(i)[0] == PageLayout::banked) {
                pending_banked_[i] = new (std::nothrow) BankedPage(hardware_, outputs_, sources_, i);
                if (!pending_banked_[i] || !pending_banked_[i]->configure(document, i)) return false;
            }
        }
        if (!primary_.prepare(document)) return false;
        auto &extra = prepared_ ? pending_controllers_ : controllers_;
        for (unsigned i = 0; i < extra.size / sizeof(Controller); ++i)
            if (!std::launder(reinterpret_cast<Controller *>(extra.data + i * sizeof(Controller)))->prepare(document)) return false;
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_finish_pages)
#endif
    void finish_pages(bool commit) {
        if (commit) { destroy_banked(banked_); banked_.swap(pending_banked_); }
        destroy_banked(pending_banked_);
        if (commit) { visible_index = 0; processing_index = PageLayout::max_controllers; }
        else {
            primary_.cancel_preparation();
            for (unsigned i = 0; i < controllers_.size / sizeof(Controller); ++i) at(i + 1).cancel_preparation();
        }
        if (commit && prepared_) {
            destroy_controllers(controllers_); controllers_.swap(pending_controllers_);
        } else destroy_controllers(pending_controllers_);
        if (commit) {
            names_.swap(pending_names_);
            labels_.swap(pending_labels_); views.swap(pending_views_);
            menu.configure(names_.data ? reinterpret_cast<const char *const *>(names_.data) : tool_names_.data(),
                names_.data ? names_.size / sizeof(const char *) : page_offset());
        }
        pending_names_.clear();
        pending_labels_.release(); pending_views_.release(); allocation.finish(commit);
        prepared_ = false;
    }
    Controller &at(unsigned index) { return index ? *std::launder(reinterpret_cast<Controller *>(controllers_.data + (index - 1) * sizeof(Controller))) : primary_; }
    unsigned rendering() const { return processing_index < PageLayout::max_controllers ? processing_index : visible_index; }
    Controller &controller() { return at(rendering()); }
    bool visible() const { return allocation.controller_visible(rendering()); }
    bool focused_controller() const { return allocation.selected() < layout.pages() && layout.page(allocation.selected())[0] == PageLayout::patch_switcher; }
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
        allocation.configure(layout); pending_region = PageLayout::max_pages;
        for (unsigned i = 1; i <= controllers_.size / sizeof(Controller); ++i) {
            processing_index = i;
            if (i < layout.controllers()) at(i).activate(document, false); else at(i).deactivate();
        }
        if (layout.controllers() <= 1) destroy_controllers(controllers_);
        if (!layout.pages()) { names_.clear(); labels_.release(); views.release(); }
        processing_index = PageLayout::max_controllers; services.blank_performance();
        update_menu();
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_refresh)
#endif
    void refresh(ApplicationServices &services) {
        const unsigned previous = processing_index;
        services.blank_performance();
        for (unsigned i = 0; i < layout.controllers(); ++i) if (allocation.controller_visible(i)) { processing_index = i; at(i).refresh(); }
        for (unsigned i = 0; i < banked_.size(); ++i) if (banked_[i]) banked_[i]->refresh();
        processing_index = previous;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_update_menu)
#endif
    void update_menu() {
        for (unsigned r = 0; r < layout.regions(); ++r) {
            std::snprintf(labels_[r].data(),labels_[r].size(),"%s%s: %s",r == allocation.focus() ? "* " : "",layout.region_name(r),layout.name(allocation.region(r).selected));
        }
        for (unsigned p = 0; p < layout.pages(); ++p) {
            unsigned r = layout.page_region(p);
            std::snprintf(labels_[layout.regions() + p].data(),labels_[0].size(),"%s / %s",layout.region_name(r),layout.name(p));
        }
        menu.configure(names_.data ? reinterpret_cast<const char *const *>(names_.data) : tool_names_.data(), page_offset() + layout.regions() + layout.pages());
    }
    void tick(uint32_t now) {
        if (!hardware_.relays.tick(now)) hardware_.error("Relay pulse restore failed");
        for (unsigned i = 0; i < (layout.controllers() ? layout.controllers() : 1); ++i) { processing_index = i; at(i).tick(now); }
        processing_index = PageLayout::max_controllers;
        for (unsigned i = 0; i < banked_.size(); ++i) if (banked_[i]) { processing_banked_ = i; banked_[i]->tick(now, uint32_t(held)); }
        processing_banked_ = PageLayout::max_pages;
        if (next_song_patch_ && !patch_pending()) {
            next_song_patch_ = false;
            if (!songs_.next()) hardware_.error(songs_.last_error());
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_state)
#endif
    bool state(unsigned operation, uint32_t value, ApplicationServices &services) {
        if (operation >= 14 && operation <= 17) {
            const unsigned page = processing_banked_ < banked_.size() ? processing_banked_ : allocation.selected();
            return page < banked_.size() && banked_[page] && banked_[page]->navigate(operation);
        }
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
            if (selected >= int(page_offset()) && unsigned(selected) - page_offset() < layout.regions()) {
                at(visible_index).cancel_gesture(); pending_region = unsigned(selected) - page_offset(); controls_locked = true; return true;
            }
            if (selected >= int(page_offset() + layout.regions()) && unsigned(selected) - page_offset() - layout.regions() < layout.pages()) {
                at(visible_index).cancel_gesture(); pending_page = unsigned(selected) - page_offset() - layout.regions(); controls_locked = true; return true;
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
    PageAllocation allocation;
    unsigned visible_index = 0, processing_index = PageLayout::max_controllers;
    unsigned pending_page = PageLayout::max_pages;
    unsigned pending_region = PageLayout::max_pages;
    unsigned pending_operation = 0;
    bool return_to_switches = false, controls_locked = false;
    bool binding_return_songs = false;
    uint64_t held = 0;
    BankedPage *focused_bank() {
        const unsigned page = allocation.selected();
        return page < banked_.size() ? banked_[page] : nullptr;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_activate_banked)
#endif
    bool activate_banked(const Document &document, unsigned node) {
        for (unsigned i = 0; i < banked_.size(); ++i) if (banked_[i] && banked_[i]->matches(document, node)) {
            if (!banked_[i]->activate(uint32_t(held))) return false;
            pending_page = i; return true;
        }
        return false;
    }
private:
    std::array<const char *, 3> tool_names_{"Setups", "Songs and setlists", "Switches"};
    MemoryBuffer names_, pending_names_;
    CheckedArray<std::array<char,96>> labels_, pending_labels_;
    CheckedArray<Pages::ViewState> pending_views_;
public:
    CheckedArray<Pages::ViewState> views;
    ModuleMenu menu;
    SetupUi setups;
    SongsUi song_ui;
private:
    Hardware &hardware_;
    Documents &documents_;
    Controller &primary_;
    PatchOutputs &outputs_;
    PatchSources &sources_;
    MemoryBuffer controllers_, pending_controllers_;
    CheckedArray<BankedPage *> banked_, pending_banked_;
    unsigned processing_banked_ = PageLayout::max_pages;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_destroy_banked)
#endif
    static void destroy_banked(CheckedArray<BankedPage *> &pages) {
        for (auto *page : pages) delete page;
        pages.release();
    }
    bool prepared_ = false;
    static void destroy_controllers(MemoryBuffer &storage) {
        for (unsigned i = storage.size / sizeof(Controller); i--;)
            std::launder(reinterpret_cast<Controller *>(storage.data + i * sizeof(Controller)))->~Controller();
        storage.clear();
    }
    Songs &songs_;
    bool has_switches_;
    bool next_song_patch_ = false;
    unsigned page_offset() const { return has_switches_ ? 3 : 2; }
};
} // namespace cntrl::ui
