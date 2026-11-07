#include <cntrl/ui/application.h>
#include "patch_hardware.h"
#include "storage.h"
#include <cassert>

struct Hardware : TestPatchHardware, cntrl::Replies, cntrl::ui::ApplicationServices, cntrl::SongResolver, cntrl::SongCapture {
    unsigned blanks = 0, editors = 0;
    bool idle = true, transfer_busy = false, editor = false;
    uint8_t brightness = 0;
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 200; }
    unsigned display_height() const override { return 120; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t value, bool = false) override { brightness = value; }
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
    void reply(const char *) override {}
    uint32_t application_time() const override { return 123; }
    void blank_performance() override { ++blanks; }
    bool output_idle() const override { return idle; }
    bool song_transfer_busy() const override { return transfer_busy; }
    bool switch_editor_open() const override { return editor; }
    void open_switch_editor(const cntrl::Document &, uint64_t) override { ++editors; editor = true; }
    bool shift_switch_editor(bool) override { return editor; }
    bool activate(const cntrl::SongSelection &, const cntrl::SongContinuation &, bool) override { return true; }
    cntrl::SongExecution execution_status() const override { return cntrl::SongExecution::completed; }
    bool capture_source(const cntrl::Document *&) override { return false; }
    bool revision(const cntrl::SongSelection &, uint32_t &) override { return false; }
    bool capture(cntrl::Song &, bool) override { return false; }
    void recalled() override {}
};
int main() {
    Hardware hardware; TestStorage setup_storage, song_storage;
    cntrl::Controller primary(hardware, hardware, hardware);
    cntrl::Documents documents(setup_storage, hardware, primary, hardware);
    alignas(cntrl::Controller) unsigned char extra[(cntrl::AppLayout::max_apps - 1) * sizeof(cntrl::Controller)];
    for (unsigned i = 0; i + 1 < cntrl::AppLayout::max_apps; ++i) new (extra + i * sizeof(cntrl::Controller)) cntrl::Controller(hardware, hardware, hardware, i + 6);
    {
        cntrl::Songs songs(song_storage, hardware);
        cntrl::ui::Application app(hardware, documents, primary, extra, songs, hardware, true);
        assert(&app.controller() == &primary && !app.feature_open(hardware));
        assert(app.state(1, 91, hardware) && hardware.brightness == 91);
        assert(app.state(9, 0, hardware) && app.pending_operation == 9);
        assert(!app.state(11, 0, hardware)); hardware.editor = true; assert(app.state(11, 0, hardware)); hardware.editor = false;
        using Op = cntrl::ui::Operation;
        assert(app.operation_ui(Op::menu_open, hardware)); assert(app.menu.open() && app.feature_open(hardware));
        app.menu.poll(0); assert(app.operation_ui(Op::menu_activate, hardware)); assert(!app.menu.open() && app.setups.open());
        while (documents.busy()) documents.tick(0);
        app.setups.poll(0); app.setups.button(5, true, 0);
        assert(app.operation_ui(Op::menu_open, hardware)); app.menu.poll(0);
        assert(app.operation_ui(Op::menu_next, hardware)); assert(app.operation_ui(Op::menu_activate, hardware));
        assert(app.song_ui.open() && !app.menu.open());
        assert(app.operation_ui(Op::performance_show, hardware)); assert(!app.song_ui.open());
        assert(app.operation_ui(Op::menu_open, hardware)); app.menu.poll(0); app.operation_ui(Op::menu_next, hardware);
        hardware.transfer_busy = true; app.operation_ui(Op::menu_activate, hardware); assert(app.menu.open() && !app.song_ui.open());
        hardware.transfer_busy = false; cntrl::Document empty; app.configure(empty, hardware); assert(hardware.blanks == 1);
        app.processing_index = 1; assert(&app.controller() == &app.at(1)); app.tick(0); assert(app.processing_index == cntrl::AppLayout::max_apps);
    }
    for (unsigned i = 0; i + 1 < cntrl::AppLayout::max_apps; ++i) std::launder(reinterpret_cast<cntrl::Controller *>(extra + i * sizeof(cntrl::Controller)))->~Controller();
}
