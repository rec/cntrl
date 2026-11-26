#pragma once
#include "messages.h"
#include <cntrl/controller.h>
#include <cntrl/documents.h>
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
    virtual bool tap_clock() { return false; }
    virtual void blank_performance() = 0;
    virtual bool output_idle() const = 0;
    virtual bool song_transfer_busy() const = 0;
    virtual bool switch_editor_open() const = 0;
    virtual bool switch_performance_active() const { return false; }
    virtual uint8_t scene_page_chord(unsigned) const { return 0; }
    virtual void refresh_switches() {}
    virtual void open_switch_editor(const Document &, uint64_t held) = 0;
    virtual bool shift_switch_editor(bool backwards) = 0;
};
// Coordinates configured pages and menus independently of controller
// storage placement and the platform's physical controls.
class Application : public BitmapLookup {
public:
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_bitmap_for)
#endif
    Bitmap bitmap_for(uint32_t generation, unsigned index) const override {
        const auto *document = artwork_document(generation);
        return document ? document->bitmap_for(this, index) : Bitmap{};
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_bitmap_read)
#endif
    bool read_bitmap(uint32_t generation, unsigned offset, uint16_t *pixels, unsigned count) const override {
        const auto *document = artwork_document(generation);
        if (!document || offset > document->length() || count > (document->length() - offset) / 2) return false;
        std::memcpy(pixels, document->data() + offset, count * 2);
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_artwork_document)
#endif
    const Document *artwork_document(uint32_t generation) const {
        const auto &document = documents_.document();
        if (document.valid() && generation == document.artwork_generation()) return &document;
        return outputs_.artwork_document(generation);
    }
#if UINTPTR_MAX == UINT32_MAX
    static_assert(sizeof(Pages::ViewState) <= 320, "Update layout.estimate_ram for larger page state");
    static_assert(sizeof(BankedPage) <= 1024, "Update banked.estimate_ram for larger banked page state");
#endif
    Application(Hardware &hardware, Documents &documents, Controller &primary, PatchOutputs &outputs, PatchSources &sources,
                Songs &songs, SongCapture &capture, bool switches)
        : setups(documents, hardware),
          song_ui(songs, hardware, capture, &documents), hardware_(hardware), documents_(documents), primary_(primary),
          outputs_(outputs), sources_(sources), songs_(songs), has_switches_(switches) { songs_.bind_documents(documents); }
    ~Application() { destroy_banked(pending_banked_); destroy_banked(banked_); destroy_controllers(pending_controllers_); destroy_controllers(controllers_); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_prepare_layout)
#endif
    bool prepare_layout(PageLayout candidate) {
        prepared_ = false;
        destroy_controllers(pending_controllers_);
        pending_views_.release();
        if (!pending_views_.resize(candidate.pages()) || !allocation.prepare(candidate)) return false;
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
        if (commit) views.swap(pending_views_);
        pending_views_.release(); allocation.finish(commit);
        prepared_ = false;
    }
    Controller &at(unsigned index) { return index ? *std::launder(reinterpret_cast<Controller *>(controllers_.data + (index - 1) * sizeof(Controller))) : primary_; }
    unsigned rendering() const { return processing_index < PageLayout::max_controllers ? processing_index : visible_index; }
    Controller &controller() { return at(rendering()); }
    bool visible() const { return allocation.controller_visible(rendering()); }
    bool focused_controller() const { return allocation.selected() < layout.pages() && layout.page(allocation.selected())[0] == PageLayout::patch_chooser; }
    bool busy() {
        for (unsigned i = 0; i < layout.controllers(); ++i) if (at(i).busy()) return true;
        return primary_.busy();
    }
    bool patch_pending() { return busy() || songs_.executing() || songs_.busy(); }
    bool song_navigation_active() const {
        return songs_.performing() && !editing_tool && !setups.open() && (song_ui.performing() || binding_return_songs);
    }
    bool song_boundary() const { return song_navigation_active() && songs_.boundary(); }
    void scene_panels(ScenePanel &scene, ScenePanel &song, ScenePanel &setlist, const Document &document, const uint8_t *binding) const {
        songs_.presentation(scene, song, setlist, document, binding);
    }
    void startup() {
        songs_.recover_startup_set();
        const char *name = documents_.startup_set();
        if (!name[0]) return;
        if (!songs_.setlist(name)) { hardware_.error("Startup set missing"); return; }
        songs_.choose(name); song_ui.perform(); controls_locked = true;
    }
    bool song_style(ScreenGraphics &style) const {
        if (!song_navigation_active()) return false;
        const auto *song = songs_.song(songs_.current_name());
        if (!song) return false;
        style.foreground = song->foreground; style.background = song->background; style.font = song->font;
        return true;
    }
    bool request_song_step(bool previous) {
        if (pending_song_step_) return false;
        pending_song_step_ = previous ? -1 : 1;
        return true;
    }
    bool stopping_setup() const { return stopping_setup_; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_stop_setup)
#endif
    void stop_setup() {
        stopping_setup_ = true;
        primary_.stop();
        for (unsigned i = 1; i < layout.controllers(); ++i) at(i).stop();
        for (auto &bank : banked_) if (bank) bank->stop();
        songs_.stop_execution(); pending_song_step_ = 0; pending_operation = 0;
        song_ui.patch_chooser_requested = false;
        song_ui.leave();
        settings_library_ = resume_settings_performance_ = false;
        pending_page = PageLayout::max_pages;
        utility_page = PageLayout::max_pages;
        editing_tool = 0;
        binding_return_songs = false; controls_locked = true;
    }
    bool utility_visible() const { return !editing_tool && utility_page < layout.pages() && layout.page(utility_page)[0] != 10; }
    bool feature_open(const ApplicationServices &services) const { return !utility_visible() && (setups.open() || song_ui.open() || services.switch_editor_open()); }
    bool settings_frontmost() const { return editing_tool == PageLayout::settings && (setups.open() || settings_library_); }
    bool editing_text() const { return setups.editing_text() || song_ui.editing_name(); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_text_key)
#endif
    void text_key(uint8_t key, uint32_t now) {
        if (setups.open()) setups.text_key(key, now);
        else song_ui.key(key);
    }
    bool settings_shortcut_available(const ApplicationServices &services) const {
        return settings_frontmost() || (!editing_tool && !documents_.busy() && !documents_.dirty()
            && documents_.document().valid() && !pending_song_step_ && !songs_.executing()
            && (!services.switch_editor_open() || services.switch_performance_active()));
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_settings_chord_available)
#endif
    bool settings_chord_available(const ApplicationServices &services, uint8_t chord) const {
        if (!chord || !settings_shortcut_available(services)) return false;
        if (settings_frontmost()) return true;
        if (utility_visible() || (!song_navigation_active() && !services.switch_performance_active())) {
            const unsigned page = utility_visible() ? utility_page : allocation.selected();
            if (page < layout.pages()) {
                const auto type = layout.page(page)[0];
                // These tools use press or release timing for their own controls.
                if (type == PageLayout::time || type == PageLayout::midi_looper || type == PageLayout::arpeggiator
                    || type == unsigned(Pages::Page::panic) || type == PageLayout::pong || type == PageLayout::memon
                    || type == PageLayout::calibrate) return false;
            }
        }
        if (const auto *navigation = layout.navigation())
            for (unsigned i = 0; i < 4; ++i) if (navigation[i] == chord) return false;
        if (song_navigation_active() && !song_boundary())
            for (unsigned i = 0; i < 2; ++i) if (services.scene_page_chord(i) == chord) return false;
        return true;
    }
    bool settings_shortcut(ApplicationServices &services) {
        if (settings_library_) {
            if (!song_ui.return_from_library()) return false;
            finish_settings_library();
            return true;
        }
        if (settings_frontmost()) return setups.return_to_performance();
        return open_tool(PageLayout::settings, services, true);
    }
    bool tool_editor_open(const ApplicationServices &services) const {
        if (song_ui.patch_chooser_requested) return true;
        if (editing_tool == PageLayout::settings) return setups.open() || settings_library_;
        if (editing_tool == PageLayout::setups) return setups.open();
        if (editing_tool == PageLayout::songs) return song_ui.open() && !song_ui.performing();
        return editing_tool == PageLayout::switches && services.switch_editor_open();
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_open_tool)
#endif
    bool open_tool(unsigned page, ApplicationServices &services, bool shortcut_hint = false) {
        if (documents_.busy()) return false;
        if (page == PageLayout::settings) {
            if (editing_tool || !settings_shortcut_available(services)) return false;
            const bool performing = song_navigation_active();
            editing_tool = page;
            settings_shortcut_hint_ = shortcut_hint;
            if (!setups.enter_settings(songs_, performing, shortcut_hint)) { editing_tool = 0; return false; }
        } else if (page == PageLayout::setups) {
            if (documents_.dirty()) return false;
            setups.enter();
        } else if (page == PageLayout::songs) {
            if (services.song_transfer_busy() || !services.output_idle()) return false;
            resume_song_ui_ = song_ui.performing();
            song_ui.enter();
        } else if (page == PageLayout::switches) {
            if (!has_switches_ || services.switch_editor_open() || busy() || documents_.dirty() || services.song_transfer_busy()
                || !services.output_idle() || !documents_.document().valid()) return false;
            services.open_switch_editor(documents_.document(), held);
        } else return false;
        controller().cancel_gesture(); editing_tool = page; controls_locked = true; return true;
    }
    void finish_tool() {
        if (editing_tool == PageLayout::settings && resume_settings_performance_) {
            resume_settings_performance_ = false;
            song_ui.perform();
        }
        if (editing_tool == PageLayout::songs) {
            if (song_ui.performing()) utility_page = PageLayout::max_pages;
            else if (resume_song_ui_) song_ui.perform();
        }
        editing_tool = 0;
    }
    void bindings_enter() {
        binding_return_songs = binding_return_songs || song_ui.open();
        controller().cancel_gesture(); song_ui.leave();
        controls_locked = false;
    }
    void brightness(uint8_t value) { for (unsigned i = 0; i < (layout.controllers() ? layout.controllers() : 1); ++i) at(i).runtime_brightness(value); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_configure)
#endif
    void configure(const Document &document, ApplicationServices &services) {
        stopping_setup_ = false;
        layout = document.valid() ? document.page_layout() : PageLayout{}; visible_index = 0; pending_page = PageLayout::max_pages;
        allocation.configure(layout); utility_page = PageLayout::max_pages; editing_tool = 0;
        for (unsigned i = 1; i <= controllers_.size / sizeof(Controller); ++i) {
            processing_index = i;
            if (i < layout.controllers()) at(i).activate(document, false); else at(i).deactivate();
        }
        if (layout.controllers() <= 1) destroy_controllers(controllers_);
        if (!layout.pages()) views.release();
        processing_index = PageLayout::max_controllers; services.blank_performance();
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
    CNTRL_FLASH_ROUTINE(application_language_changed)
#endif
    void language_changed() {
        if (setups.open()) setups.refresh();
        else song_ui.refresh();
        for (auto &view : views) { view.dirty = true; view.refreshed = 0; }
    }
    void tick(uint32_t now) {
        if (stopping_setup_) return;
        finish_settings_library();
        if (!hardware_.relays.tick(now)) hardware_.error(i18n::text(i18n::Message::relay_pulse_restore_failed));
        for (unsigned i = 0; i < (layout.controllers() ? layout.controllers() : 1); ++i) { processing_index = i; at(i).tick(now); }
        processing_index = PageLayout::max_controllers;
        for (unsigned i = 0; i < banked_.size(); ++i) if (banked_[i]) { processing_banked_ = i; banked_[i]->tick(now, uint32_t(held)); }
        processing_banked_ = PageLayout::max_pages;
        if (song_ui.patch_chooser_requested && !patch_pending()) {
            const auto cleanup = outputs_.prepare_leave(controller().executor());
            if (cleanup == midi::Bridge::Delivery::complete) pending_operation = 10;
            else if (cleanup == midi::Bridge::Delivery::cancelled) {
                song_ui.patch_chooser_requested = false;
                pending_operation = 0;
                hardware_.error("Control cleanup failed");
            }
        }
        if (pending_song_step_ && !patch_pending()) {
            bool previous = pending_song_step_ < 0;
            pending_song_step_ = 0;
            if (!(previous ? songs_.previous() : songs_.next())) hardware_.error(songs_.last_error());
            if (songs_.boundary()) song_ui.perform();
            else if (binding_return_songs) song_ui.leave();
            else song_ui.refresh();
        }
        if (song_navigation_active() && songs_.boundary() && !song_ui.performing()) song_ui.perform();
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
        if (operation == 10) { pending_operation = operation; return true; }
        if (operation == 13 || operation == 18) return request_song_step(operation == 18);
        if (operation == 19) return services.tap_clock();
        return operation_ui(Operation(operation), services);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(application_operation_ui)
#endif
    bool operation_ui(Operation operation, ApplicationServices &services) {
        switch (operation) {
        case Operation::performance_show:
            if (editing_tool == PageLayout::switches && song_ui.performing()) return true;
            controller().cancel_gesture(); song_ui.leave();
            if (song_ui.patch_chooser_requested) {
                song_ui.patch_chooser_requested = false;
                editing_tool = 0;
                utility_page = PageLayout::max_pages;
                binding_return_songs = false;
                if (!visible()) pending_page = layout.controller_page(visible_index);
            }
            if (binding_return_songs) { binding_return_songs = false; song_ui.perform(); }
            else documents_.redraw();
            controls_locked = true; return true;
        case Operation::setup_previous:
        case Operation::setup_next:
        case Operation::setup_confirm:
        case Operation::setup_previous_section:
        case Operation::setup_next_section:
        case Operation::setup_back: {
            if (!setups.open() || documents_.busy()) return false;
            const unsigned button = operation == Operation::setup_previous ? 0 : operation == Operation::setup_next ? 1 : operation == Operation::setup_confirm ? 2
                : operation == Operation::setup_previous_section ? 3 : operation == Operation::setup_next_section ? 4 : 5;
            setups.button(button, true, services.application_time());
            if (const auto library = setups.take_library_request(); library != SetupUi::Library::none) {
                if (songs_.busy() || services.song_transfer_busy()) {
                    setups.resume_settings();
                    hardware_.error(i18n::text(i18n::Message::working));
                } else {
                    resume_settings_performance_ |= song_ui.performing();
                    settings_library_ = true;
                    song_ui.edit_library(library == SetupUi::Library::setlists, settings_shortcut_hint_);
                }
                controls_locked = true;
            }
            return true;
        }
        case Operation::songs_previous:
        case Operation::songs_next:
        case Operation::songs_confirm:
        case Operation::songs_previous_section:
        case Operation::songs_next_section:
        case Operation::songs_back: {
            if (!song_ui.open()) return false;
            const unsigned button = operation == Operation::songs_previous ? 0 : operation == Operation::songs_next ? 1 : operation == Operation::songs_confirm ? 2 : operation == Operation::songs_previous_section ? 3 : operation == Operation::songs_next_section ? 4 : 5;
            song_ui.button(button, true);
            if (settings_library_) { finish_settings_library(); return true; }
            if (!song_ui.open() && !song_ui.patch_chooser_requested) documents_.redraw();
            if (!song_ui.open()) controls_locked = true;
            return true;
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
    unsigned utility_page = PageLayout::max_pages;
    unsigned pending_operation = 0;
    unsigned editing_tool = 0;
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
    void finish_settings_library() {
        if (!settings_library_ || song_ui.open()) return;
        settings_library_ = false;
        if (!song_ui.library_returning_to_performance()) setups.resume_settings();
        controls_locked = true;
    }
    CheckedArray<Pages::ViewState> pending_views_;
public:
    CheckedArray<Pages::ViewState> views;
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
    bool resume_song_ui_ = false;
    bool settings_library_ = false, resume_settings_performance_ = false, settings_shortcut_hint_ = false;
    int pending_song_step_ = 0;
    bool stopping_setup_ = false;
};
} // namespace cntrl::ui
