#include <cntrl/ui/input.h>
#include "filesystem.h"
#include "patch_hardware.h"
#include "storage.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

struct Board : TestPatchHardware, cntrl::Replies, cntrl::ui::ApplicationServices, cntrl::SongResolver, cntrl::SongCapture {
    cntrl::ui::Application *app = nullptr;
    cntrl::Songs *songs = nullptr;
    cntrl::Switches switches{*this, *this, *this};
    cntrl::PatchExecutor entry{*this, *this, *this, 8};
    std::vector<uint8_t> source;
    cntrl::Document document;
    std::vector<std::pair<unsigned, unsigned>> sent;
    std::vector<bool> page_directions;
    cntrl::Pane panes[3];
    std::string last_error;
    uint32_t now = 0;
    unsigned displays = 3;
    unsigned display_count() const override { return displays; }
    unsigned input_count() const override { return 6; }
    unsigned switch_capacity() const override { return 5; }
    const char *input_label(unsigned input) const override { static const char *labels[] = {"A", "B", "C", "D", "E", "F"}; return labels[input]; }
    const char *control_label(cntrl::ControlRole role) const override { return input_label(unsigned(role)); }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    bool supports_trs_mode(unsigned, cntrl::midi::TrsMode) const override { return true; }
    void show(unsigned pane, const cntrl::Pane &content) override { panes[pane] = content; }
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *message) override { last_error = message; }
    bool send(const char *, const uint8_t *bytes, unsigned length) override { assert(length == 3); sent.emplace_back(bytes[1], bytes[2]); return true; }
    void reply(const char *) override {}
    void data(const uint8_t *, unsigned) override { assert(false); }
    uint32_t application_time() const override { return now; }
    void blank_performance() override {}
    bool output_idle() const override { return true; }
    bool song_transfer_busy() const override { return false; }
    bool switch_editor_open() const override { return switches.open(); }
    bool switch_performance_active() const override { return switches.performance(); }
    void open_switch_editor(const cntrl::Document &d, uint64_t held) override { assert(switches.enter(d, uint32_t(held))); }
    bool shift_switch_editor(bool backwards) override { return switches.shift(backwards); }
    bool state(unsigned operation, uint32_t value) override { return app->state(operation, value, *this); }
    bool song_navigation_active() const override { return app && app->song_navigation_active(); }
    bool show_switch_performance() const override { return !app || !app->settings_frontmost(); }
    void scene_panels(cntrl::ScenePanel &scene, cntrl::ScenePanel &song, cntrl::ScenePanel &setlist, const cntrl::Document &document, const uint8_t *binding) const override { app->scene_panels(scene, song, setlist, document, binding); }
    bool song_boundary() const override { return app && app->song_boundary(); }
    cntrl::midi::Bridge::Delivery prepare_leave(cntrl::PatchExecutor &invocation) override { return switches.prepare_leave(invocation); }
    cntrl::midi::Bridge::Delivery bind_controls(const cntrl::Document &d, unsigned node, cntrl::PatchExecutor &invocation) override { return switches.bind_controls(d, node, invocation); }
    bool activate(const cntrl::SongSelection &selection, const cntrl::SongContinuation &, bool) override {
        return entry.start(document, selection.node);
    }
    cntrl::SongExecution execution_status() const override { return entry.busy() ? cntrl::SongExecution::pending : cntrl::SongExecution::completed; }
    bool capture_source(const cntrl::Document *&) override { return false; }
    bool revision(const cntrl::SongSelection &, uint32_t &) override { return false; }
    bool capture(cntrl::Song &, bool) override { return false; }
    void recalled() override {}
};

struct Actions {
    Board &board;
    bool switches_open() const { return board.switches.open(); }
    void switch_button(unsigned input, bool down, uint32_t now) { board.switches.button(input, down, now); }
    bool controls_locked() const { return board.app->controls_locked; }
    bool songs_open() const { return board.app->song_ui.open(); }
    bool setup_open() const { return board.app->setups.open(); }
    void invoke(cntrl::ui::Operation operation) { board.state(unsigned(operation), 0); }
    void browse(bool reverse) { board.page_directions.push_back(reverse); }
    bool midi_reserved() const { return false; }
    bool time(unsigned, uint8_t, uint32_t) { return false; }
    bool monitor(unsigned, bool) { return false; }
    bool looper(unsigned, bool) { return false; }
    bool has_controller() const { return false; }
    void controller(unsigned, bool, uint32_t) { assert(false); }
    void tap(uint32_t) {}
    bool leave_switches() { return true; }
    bool abandoned() const { return board.switches.abandoned(); }
    bool patch_pending() const { return board.app->patch_pending(); }
    void error(const char *message) { board.error(message); }
};

struct Services {
    Actions routing;
    std::optional<unsigned> debounce(unsigned) const { return std::nullopt; }
    void keyboard_release(unsigned) {}
    bool panic_open() const { return false; }
    void panic_release(uint32_t) {}
    bool animation_controls(uint8_t, uint32_t, bool) { return false; }
    bool diagnostic_controls(const uint8_t *, uint8_t, uint32_t) { return false; }
    cntrl::ui::FirmwareVisit *visit() { return nullptr; }
    Actions &actions() { return routing; }
};

int main(int argc, char **argv) {
    assert(argc >= 3 && argc <= 5); cntrl::memory_budget = 2 * 1024 * 1024;
    Board board; TestStorage setup_storage, song_storage;
    struct SettingsDisk : TestFileSystem {
        using TestFileSystem::TestFileSystem;
        bool blocked = false;
        struct File : TestFile {
            SettingsDisk &disk;
            explicit File(SettingsDisk &value) : TestFile(value), disk(value) {}
            bool open(const char *path, cntrl::files::Mode mode) override {
                return (mode == cntrl::files::Mode::read || !disk.blocked) && TestFile::open(path, mode);
            }
        };
        std::unique_ptr<cntrl::files::File> file() override {
            return std::unique_ptr<cntrl::files::File>(new (std::nothrow) File(*this));
        }
    } settings_disk(std::filesystem::path(argv[1]).parent_path() / "settings-device");
    cntrl::GlobalSettings preferences(settings_disk);
    cntrl::Controller primary(board, board, board); board.controller = &primary;
    cntrl::Documents documents(setup_storage, board, primary, board);
    cntrl::Songs songs(song_storage, board); board.songs = &songs;
    std::ifstream library_file(argv[1], std::ios::binary);
    std::vector<uint8_t> library{std::istreambuf_iterator<char>(library_file), {}};
    assert(songs.boot() && songs.import_begin(unsigned(library.size())) && songs.import_write(library.data(), unsigned(library.size()))
        && songs.import_finish(unsigned(library.size()), cntrl::checksum(library.data(), unsigned(library.size()))));
    while (songs.busy()) songs.tick(); assert(songs.success());
    board.source.resize(songs.source_size(0));
    assert(songs.read_source(0, 0, board.source.data(), unsigned(board.source.size())) && board.document.load(board.source.data(), unsigned(board.source.size()), board));
    std::ifstream setup_file(argv[2], std::ios::binary);
    std::vector<uint8_t> setup{std::istreambuf_iterator<char>(setup_file), {}};
    cntrl::Document configuration; assert(configuration.load(setup.data(), unsigned(setup.size()), board));
    const auto upload_setup = [&](const char *name) {
        char upload[128];
        std::snprintf(upload, sizeof(upload), "CNTRL SETUP PUT %s %u %08lx 0\n",
            name, unsigned(setup.size()), (unsigned long)cntrl::checksum(setup.data(), unsigned(setup.size())));
        for (char c : std::string(upload)) documents.receive(uint8_t(c), 0);
        for (uint8_t b : setup) documents.receive(b, 0);
        while (documents.busy()) documents.tick(0);
        assert(!documents.last_error()[0]);
    };
    upload_setup("4c697665");
    assert(documents.select("Live"));
    while (documents.busy()) documents.tick(0);
    assert(documents.document().valid());
    cntrl::ui::Application app(board, documents, primary, board, board, songs, board, true); board.app = &app;
    assert(app.prepare_pages(configuration)); app.finish_pages(true); app.layout = configuration.page_layout(); primary.activate(configuration, false);
    cntrl::ui::ControlBinding bindings[] = {{0, "A"}, {1, "B"}, {2, "C"}, {3, "D"}, {4, "E"}, {5, "F"}};
    cntrl::ui::Controls controls{0, 1, 2, 3, 4, 5, 5, 3, 4, bindings, 6};
    controls.settings_shortcut = true;
    cntrl::ButtonState states[6]; cntrl::ui::InputCoordinator input(states, controls, configuration, app, board);
    Services services{{board}}; uint8_t held = 0; unsigned revision = 0;
    auto sample = [&](uint8_t value, unsigned scans = 64) {
        held = value;
        for (unsigned i = 0; i < scans; ++i) {
            documents.tick(board.now);
            input.scan(held, board.now, services); app.tick(board.now); board.entry.tick(board.now); songs.tick();
            board.switches.tick(board.now++, uint32_t(app.held));
            if (revision != board.switches.revision()) { revision = board.switches.revision(); if (board.switches.performance()) app.bindings_enter(); }
            input.finish(services.routing);
        }
    };
    auto press = [&](unsigned button) { sample(held | (1u << button)); };
    auto release = [&](unsigned button) { sample(held & ~(1u << button)); };
    auto click = [&]() { press(5); release(5); };
    auto wait = [&](unsigned samples) { for (unsigned i = 0; i < samples; ++i) sample(held); };
    auto next = [&]() { click(); wait(5); };
    auto previous = [&]() { click(); click(); };
    auto hold = [&]() { press(5); wait(8); release(5); };
    auto tap = [&](unsigned button) { press(button); release(button); };
    auto settings = [&]() {
        sample(1u << 2, 20);
        sample((1u << 2) | (1u << 5));
        release(2); release(5);
        if (app.editing_tool && !app.tool_editor_open(board)) app.finish_tool();
    };
    if (argc == 5 && !std::strcmp(argv[3], "--setup-preferences")) {
        assert(preferences.boot());
        documents.bind_globals(preferences);
        assert(documents.global_change(0, 100, false, false));
        assert(documents.document().brightness() == 100);
        const auto open_setup_preferences = [&] {
            settings(); tap(2);
            assert(!std::strcmp(board.panes[1].lines[1].text, "Brightness"));
        };
        const auto save = [&](const char *action) {
            settings(); tap(1);
            assert(!std::strcmp(board.panes[2].lines[1].text, action));
            tap(2);
            while (documents.busy()) sample(0);
            sample(0);
            if (!app.tool_editor_open(board)) app.finish_tool();
            assert(!app.settings_frontmost());
        };
        open_setup_preferences();
        assert(!std::strcmp(board.panes[2].lines[0].text, "Pedal"));
        tap(2); tap(1); tap(2);
        assert(!std::strcmp(board.panes[2].lines[0].text, "Setup"));
        assert(!std::strcmp(board.panes[2].lines[1].text, "101"));
        assert(documents.document().brightness() == 100);
        save("Save and reload setup");
        assert(documents.document().brightness() == 101 && preferences.values().values[0] == 100);

        open_setup_preferences();
        tap(2);
        assert(std::strstr(board.panes[0].lines[2].text, "Use pedal value"));
        tap(3); // Clear the override and go directly to review.
        save("Save and reload setup");
        assert(documents.document().brightness() == 100 && !(documents.document().explicit_settings() & 1));

        open_setup_preferences();
        for (unsigned i = 0; i < 5; ++i) tap(1);
        assert(!std::strcmp(board.panes[1].lines[1].text, "Target pulse maximum"));
        tap(2); tap(4); tap(4); tap(0); tap(2);
        assert(!std::strcmp(board.panes[2].lines[2].text, "Invalid for this setup"));
        tap(5);
        assert(!std::strcmp(board.panes[2].lines[1].text, "128"));
        settings();
        assert(!app.settings_frontmost());

        upload_setup("5370617265"); // Spare, an inactive setup.
        settings(); tap(1); tap(2); // Setups.
        tap(1);
        while (documents.busy()) sample(0);
        sample(0); tap(2);
        while (documents.busy()) sample(0);
        sample(0); tap(2); // Setup fields.
        tap(4); tap(4); tap(1); // Navigation, horizontal scroll duration.
        assert(!std::strcmp(board.panes[1].lines[1].text, "Horizontal scroll (ms)"));
        const unsigned original = documents.document().scroll_ms();
        tap(2); tap(1); tap(2);
        save("Save");
        assert(!std::strcmp(documents.selected(), "Live") && documents.document().scroll_ms() == original);
        assert(documents.edit_begin("Spare"));
        while (documents.busy()) documents.tick(board.now);
        cntrl::Document draft;
        assert(documents.edit_document(draft));
        assert(draft.scroll_ms() == original + 1 && (draft.explicit_settings() & (1u << 12)));
        documents.edit_cancel();

        const unsigned before_clock = documents.document().length();
        open_setup_preferences();
        tap(4); // MIDI.
        for (unsigned i = 0; i < 4; ++i) tap(1);
        assert(!std::strcmp(board.panes[1].lines[1].text, "MIDI clock outputs"));
        tap(2);
        assert(!std::strcmp(board.panes[2].lines[1].text, "DIN  Off"));
        tap(2); tap(5); // Enable DIN in the draft.
        assert(!documents.document().page_settings()[cntrl::PageSettings::clock_outputs]);
        tap(4); tap(4); tap(4); // Time, clock timeout.
        assert(!std::strcmp(board.panes[1].lines[1].text, "Clock timeout (ms)"));
        tap(2); tap(4); tap(1); tap(2); // 250 -> 260 ms.
        tap(1); tap(1); tap(1); // Auto-select clock.
        assert(!std::strcmp(board.panes[1].lines[1].text, "Auto-select clock"));
        tap(2); tap(0); tap(2);
        tap(4); tap(1); // Tempo presets, Tempo 2.
        assert(!std::strcmp(board.panes[2].lines[1].text, "Off"));
        tap(2); tap(1); tap(2);
        assert(!std::strcmp(board.panes[2].lines[1].text, "20"));
        tap(4); tap(4); // Page fonts, Title font.
        assert(!std::strcmp(board.panes[1].lines[1].text, "Title font"));
        tap(2); tap(1); tap(2);
        assert(!std::strcmp(board.panes[2].lines[1].text, "CourierPrime"));
        tap(4); tap(1); tap(1); // Time colors, Flasher ring color.
        assert(!std::strcmp(board.panes[1].lines[1].text, "Flasher ring color"));
        tap(2); tap(1); tap(3); tap(1);
        assert(!std::strcmp(board.panes[2].lines[1].text, "#080400"));
        assert(board.panes[2].graphics.background == 0x820);
        tap(2);
        save("Save and reload setup");
        const auto &document = documents.document();
        assert(document.length() == before_clock + cntrl::clock_section_size);
        assert(document.explicit_settings() & (1u << 28));
        std::ofstream exported(argv[4], std::ios::binary);
        exported.write(reinterpret_cast<const char *>(document.data()), document.length());
        assert(exported);
        assert(board.sent.empty());
        return 0;
    }
    if (argc == 4 && !std::strcmp(argv[3], "--named-patch-settings")) {
        upload_setup("5370617265"); // Spare, an inactive setup.
        settings(); tap(1); tap(2); // Setups.
        tap(1);
        while (documents.busy()) sample(0);
        sample(0); tap(2);
        while (documents.busy()) sample(0);
        sample(0); tap(1); tap(2); // Browse instruments in a setup with no browser patches.
        assert(!std::strcmp(board.panes[1].lines[1].text, "Instruments"));
        if (std::strcmp(board.panes[2].lines[0].text, "alternate")) tap(1);
        assert(!std::strcmp(board.panes[2].lines[0].text, "alternate"));
        tap(2); tap(2); tap(1); tap(5); // Channel 2 -> 3.
        tap(1); tap(2);
        assert(!std::strcmp(board.panes[1].lines[1].text, "MIDI output"));
        assert(!std::strcmp(board.panes[0].lines[2].text, "All patches using this instrument"));
        tap(1); // DIN -> TRS.
        assert(!std::strcmp(board.panes[2].lines[1].text, "TRS"));
        assert(board.sent.empty());
        settings(); tap(1);
        assert(!std::strcmp(board.panes[2].lines[1].text, "Save"));
        tap(2);
        while (documents.busy()) sample(0);
        sample(0);
        if (!app.tool_editor_open(board)) app.finish_tool();
        assert(!app.settings_frontmost() && !std::strcmp(documents.selected(), "Live") && board.sent.empty());
        assert(documents.edit_begin("Spare"));
        while (documents.busy()) documents.tick(board.now);
        cntrl::Document edited;
        assert(documents.edit_document(edited));
        struct SendProbe : Board {
            std::string output;
            std::vector<uint8_t> bytes;
            bool send(const char *route, const uint8_t *message, unsigned length) override {
                output = route;
                bytes.assign(message, message + length);
                return true;
            }
        } probe;
        auto &executor = probe.entry;
        const auto recall = [&](const cntrl::Document &doc, const char *key) {
            const int node = doc.named_patch(key);
            assert(node >= 0 && executor.start(doc, unsigned(node)));
            for (unsigned i = 0; i < 32 && executor.busy(); ++i) executor.tick(i);
            assert(executor.status() == cntrl::PatchExecutor::Status::completed);
        };
        recall(edited, "Other");
        assert(probe.output == "trs" && (probe.bytes == std::vector<uint8_t>{0xb2, 32, 2, 0xc2, 11}));
        recall(edited, "Lead");
        assert(probe.output == "din" && (probe.bytes == std::vector<uint8_t>{0xb0, 20, 10}));
        recall(documents.document(), "Other");
        assert(probe.output == "din" && (probe.bytes == std::vector<uint8_t>{0xb1, 32, 2, 0xc1, 11}));
        documents.edit_cancel();
        songs.choose("Set"); app.song_ui.perform(); sample(0); next();
        const auto sent = board.sent.size();
        cntrl::SongSelection selection;
        std::strcpy(selection.document, "Spare"); std::strcpy(selection.key, "Lead");
        for (unsigned index : {0u, 1u}) {
            assert(songs.replace_patch("One", index, selection));
            while (songs.busy()) songs.tick();
            assert(songs.success());
        }
        const auto open_patch = [&](unsigned index, const char *heading = "Edit scope") {
            settings();
            tap(0); tap(0); tap(0); tap(2);
            tap(2); tap(1); tap(2); // One, Edit patches.
            if (index) tap(1);
            tap(2); tap(1); tap(1); tap(2); // Edit patch value.
            while (documents.busy()) sample(0);
            sample(0);
            assert(!documents.dirty());
            assert(!std::strcmp(board.panes[1].lines[1].text, heading));
        };
        const auto saved_setup_value = [&](const char *name) {
            assert(documents.edit_begin(name));
            while (documents.busy()) documents.tick(board.now);
            cntrl::Document draft;
            cntrl::PatchValues value;
            assert(documents.edit_document(draft));
            const int root = draft.named_patch("Lead");
            assert(root >= 0 && cntrl::editable_patch_values(draft, unsigned(root), value));
            documents.edit_cancel();
            return value;
        };
        const auto save_and_return = [&] {
            settings(); tap(1); tap(2);
            while (documents.busy() || songs.busy()) sample(0);
            sample(0);
            if (!app.tool_editor_open(board)) app.finish_tool();
            assert(!app.settings_frontmost());
        };
        open_patch(0);
        tap(1); tap(2); tap(1); // Shared, then raise the value.
        tap(4); tap(1); // Controller number.
        tap(4); tap(1); // MIDI channel.
        settings(); tap(1);
        setup_storage.remaining = 0;
        tap(2);
        while (documents.busy()) sample(0);
        sample(0);
        assert(app.settings_frontmost() && !documents.dirty());
        setup_storage.remaining = -1;
        assert(saved_setup_value("Spare").values[0] == 10);
        tap(2); // Retry the same shared edit.
        while (documents.busy()) sample(0);
        sample(0);
        if (!app.tool_editor_open(board)) app.finish_tool();
        assert(!app.settings_frontmost());
        const auto shared = saved_setup_value("Spare");
        assert(shared.values[0] == 11 && shared.values[1] == 21 && shared.values[2] == 2);
        assert(saved_setup_value("Live").values[0] == 10);
        assert(!setup_storage.revisions.at("Spare").empty());
        for (unsigned index : {0u, 1u}) {
            cntrl::SongSelection unchanged;
            assert(songs.patch_at("One", index, unchanged) && !std::strcmp(unchanged.document, "Spare"));
        }
        open_patch(0);
        tap(2); tap(1); // Local, then raise the value.
        save_and_return();
        cntrl::PatchValues copy;
        assert(songs.patch_values("One", 0, board, copy) && copy.values[0] == 12);
        assert(copy.values[1] == 21 && copy.values[2] == 2);
        assert(songs.patch_at("One", 1, selection) && !std::strcmp(selection.document, "Spare"));
        assert(saved_setup_value("Spare").values[0] == 11);
        assert(songs.continuation().patch == 0 && board.sent.size() == sent);
        std::strcpy(selection.document, "Live");
        assert(songs.replace_patch("One", 1, selection));
        while (songs.busy()) songs.tick();
        open_patch(1);
        tap(1); tap(2); tap(1); settings(); tap(1);
        assert(std::strstr(board.panes[1].lines[1].text, "Save and reload"));
        tap(1); tap(2); // Discard the active-setup change.
        if (!app.tool_editor_open(board)) app.finish_tool();
        assert(!app.settings_frontmost() && saved_setup_value("Live").values[0] == 10 && board.sent.size() == sent);
        std::strcpy(selection.document, "Spare"); std::strcpy(selection.key, "Voice");
        assert(songs.replace_patch("One", 1, selection));
        while (songs.busy()) songs.tick();
        open_patch(1); tap(2);
        tap(4); tap(1); // Add an optional volume at zero.
        tap(4); tap(1); // Add optional pan at -64.
        tap(4); tap(1); // Select bank 3. Bank 2 belongs to another instrument.
        save_and_return();
        assert(songs.patch_values("One", 1, board, copy) && copy.kind == cntrl::PatchKind::instrument_program);
        assert(copy.values[0] == 12 && copy.values[1] == 0 && copy.values[2] == -64);
        assert(copy.bank == 3);
        open_patch(1); tap(1); tap(2); // Edit the embedded source in place.
        tap(3); tap(0); tap(0); // From bank 3 past bank 1 to None.
        save_and_return();
        assert(songs.patch_values("One", 1, board, copy) && copy.bank == UINT32_MAX);
        std::strcpy(selection.key, "Direct");
        assert(songs.replace_patch("One", 1, selection));
        while (songs.busy()) songs.tick();
        open_patch(1); tap(2);
        tap(4); tap(1); // Change the channel of the direct Program Change.
        save_and_return();
        assert(songs.patch_values("One", 1, board, copy) && copy.kind == cntrl::PatchKind::channel_program);
        assert(copy.values[0] == 12 && copy.values[1] == 4);
        std::strcpy(selection.key, "Scene");
        assert(songs.replace_patch("One", 1, selection));
        while (songs.busy()) songs.tick();
        open_patch(1, "Edit action");
        tap(1);
        assert(!std::strcmp(board.panes[2].lines[0].text, "A On"));
        assert(!std::strcmp(board.panes[2].lines[1].text, "CC 64: 127"));
        tap(2);
        while (documents.busy()) sample(0);
        sample(0);
        tap(2); tap(0); // Lower A On in a private copy of the scene.
        save_and_return();
        cntrl::CheckedList<cntrl::EditableAction> actions;
        assert(songs.patch_actions("One", 1, board, actions) && actions.size() >= 3);
        assert(songs.patch_values("One", 1, board, copy, nullptr, actions[1].node)
            && copy.values[0] == 126 && copy.values[1] == 64);
        assert(documents.edit_begin("Spare"));
        while (documents.busy()) documents.tick(board.now);
        cntrl::Document draft;
        assert(documents.edit_document(draft));
        assert(cntrl::read_patch_actions(draft, unsigned(draft.named_patch("Scene")), actions));
        assert(cntrl::editable_patch_values(draft, actions[1].node, copy) && copy.values[0] == 127);
        documents.edit_cancel();
        assert(board.sent.size() == sent);
        return 0;
    }
    if (argc == 4 && !std::strcmp(argv[3], "--song-settings")) {
        songs.choose("Set"); app.song_ui.perform(); sample(0); next();
        const auto sent = board.sent.size();
        const auto open_songs = [&] {
            settings();
            tap(0); tap(0); tap(0); tap(2);
            assert(app.settings_frontmost() && app.song_ui.open());
            assert(!std::strcmp(board.panes[1].lines[0].text, "Songs"));
            tap(2);
        };
        open_songs();
        tap(2); tap(1); // Change the first character of One to P.
        settings();
        assert(!std::strcmp(board.panes[0].lines[1].text, "Pne"));
        assert(songs.song("One") && !songs.song("Pne"));
        tap(1); tap(2); // Save the name and return.
        while (songs.busy()) sample(0);
        sample(0);
        if (!app.tool_editor_open(board)) app.finish_tool();
        assert(songs.success() && !app.settings_frontmost());
        assert(!std::strcmp(songs.current_name(), "Pne") && songs.continuation().patch == 0);
        cntrl::SongEntry entry;
        assert(songs.entry("Set", 0, entry) && !std::strcmp(entry.song, "Pne"));
        assert(songs.entry("Set", 2, entry) && !std::strcmp(entry.song, "Pne"));
        cntrl::SongSelection original, second;
        assert(songs.patch_at("Pne", 0, original) && songs.patch_at("Pne", 1, second));
        open_songs();
        tap(1); tap(2); // Edit patches.
        tap(2); tap(0); tap(2); // Move the first patch later.
        cntrl::SongSelection first;
        assert(songs.patch_at("Pne", 0, first) && first.node == original.node);
        tap(1); tap(2); // Save.
        while (songs.busy()) sample(0);
        sample(0);
        assert(songs.patch_at("Pne", 0, first) && first.node == second.node);
        assert(songs.continuation().patch == 1 && board.sent.size() == sent);
        tap(0); tap(2); tap(2); // Review removal of the preceding patch.
        assert(songs.song("Pne")->count == 3);
        tap(1); tap(2);
        while (songs.busy()) sample(0);
        sample(0);
        assert(songs.song("Pne")->count == 2 && songs.patch_at("Pne", 0, first) && first.node == original.node);
        assert(songs.continuation().patch == 0 && board.sent.size() == sent);
        settings();
        assert(!app.settings_frontmost());
        assert(songs.insert_patch("Pne", 0, first)); // A second use of the same source node.
        while (songs.busy()) sample(0);
        assert(songs.success());
        const auto open_patch_action = [&] {
            open_songs();
            tap(1); tap(2); // Edit patches.
            tap(2); tap(1); tap(1); tap(2); // Browse the first patch's actions.
            assert(!std::strcmp(board.panes[1].lines[1].text, "Edit action"));
        };
        const auto open_patch_value = [&] {
            open_patch_action();
            assert(!std::strcmp(board.panes[2].lines[0].text, "Activation"));
            tap(2);
        };
        const auto expect_values = [&](int first_value, int second_value) {
            cntrl::PatchValues value;
            assert(songs.patch_values("Pne", 0, board, value) && value.kind == cntrl::PatchKind::control_change
                && value.values[0] == first_value);
            assert(songs.patch_values("Pne", 1, board, value) && value.values[0] == second_value);
        };
        open_patch_value();
        tap(2); tap(1); // Only this song step. Draft +1.
        settings();
        expect_values(1, 1);
        tap(0); tap(2); // Discard and return.
        if (!app.tool_editor_open(board)) app.finish_tool();
        expect_values(1, 1);
        open_patch_value();
        tap(1); tap(2); // Every use of this patch.
        tap(1); settings();
        tap(1); tap(2); // Save and return.
        while (songs.busy()) sample(0);
        sample(0);
        if (!app.tool_editor_open(board)) app.finish_tool();
        assert(songs.success());
        expect_values(2, 2);
        open_patch_value();
        tap(2); tap(1); settings();
        song_storage.remaining = 0;
        tap(1); tap(2);
        while (songs.busy()) sample(0);
        sample(0);
        assert(!songs.success() && app.settings_frontmost());
        expect_values(2, 2);
        song_storage.remaining = -1;
        tap(2); // Retry the local draft.
        while (songs.busy()) sample(0);
        sample(0);
        if (!app.tool_editor_open(board)) app.finish_tool();
        assert(songs.success());
        expect_values(3, 2);
        open_patch_action();
        tap(1); tap(1);
        assert(!std::strcmp(board.panes[2].lines[0].text, "A Off"));
        tap(2); tap(2); tap(1); settings(); // Change the release value for this step.
        tap(1); tap(2);
        while (songs.busy()) sample(0);
        sample(0);
        if (!app.tool_editor_open(board)) app.finish_tool();
        assert(songs.success());
        assert(songs.continuation().patch == 0 && board.sent.size() == sent);
        cntrl::Songs restored(song_storage, board);
        cntrl::PatchValues value;
        assert(restored.boot());
        assert(restored.patch_values("Pne", 0, board, value) && value.values[0] == 3);
        assert(restored.patch_values("Pne", 1, board, value) && value.values[0] == 2);
        for (unsigned step = 0; step < 2; ++step) {
            cntrl::CheckedList<cntrl::EditableAction> actions;
            assert(restored.patch_actions("Pne", step, board, actions) && actions.size() >= 3);
            assert(restored.patch_values("Pne", step, board, value, nullptr, actions[1].node)
                && value.values[0] == 127);
            assert(restored.patch_values("Pne", step, board, value, nullptr, actions[2].node)
                && value.values[0] == (step ? 0 : 1));
        }
        return 0;
    }
    if (argc == 4 && !std::strcmp(argv[3], "--setlist-settings")) {
        songs.choose("Set"); app.song_ui.perform(); sample(0);
        next();
        assert(!songs.boundary() && !std::strcmp(songs.current_name(), "One"));
        const auto before = songs.continuation();
        const auto sent = board.sent.size();
        const auto open_list = [&] {
            settings();
            tap(0); tap(0); tap(2);
            assert(app.settings_frontmost() && !app.setups.open() && app.song_ui.open());
            cntrl::Setlist first;
            assert(songs.set_at(0, first));
            if (std::strcmp(first.name, "Friday")) tap(1);
            tap(2);
        };
        const auto begin_move = [&] {
            open_list();
            tap(0); // Last entry, the second occurrence of One.
            assert(!std::strcmp(board.panes[1].lines[2].text, "3/3"));
            tap(2); tap(1); // Move from position three to position one.
            assert(!std::strcmp(board.panes[0].lines[2].text, "3/3"));
            assert(!std::strcmp(board.panes[2].lines[2].text, "1/3"));
        };
        begin_move();
        settings(); // Review before returning to performance.
        tap(0); tap(2); // Discard.
        if (!app.tool_editor_open(board)) app.finish_tool();
        cntrl::SongEntry entry;
        assert(songs.entry("Friday", 1, entry) && !std::strcmp(entry.song, "Two"));
        begin_move();
        settings();
        tap(2); // Keep editing.
        settings();
        tap(1); // Save and return.
        song_storage.remaining = 0;
        tap(2);
        while (songs.busy()) sample(0);
        sample(0);
        assert(app.settings_frontmost() && !songs.success());
        assert(songs.entry("Friday", 1, entry) && !std::strcmp(entry.song, "Two"));
        assert(!std::strcmp(board.panes[2].lines[2].text, "1/3"));
        song_storage.remaining = -1;
        tap(2); // Retry the same draft.
        while (songs.busy()) sample(0);
        sample(0);
        if (!app.tool_editor_open(board)) app.finish_tool();
        assert(songs.success() && !app.settings_frontmost());
        assert(songs.entry("Friday", 1, entry) && !std::strcmp(entry.song, "One"));
        assert(songs.entry("Friday", 2, entry) && !std::strcmp(entry.song, "Two"));
        open_list();
        tap(3); tap(1); tap(2); // Add to end.
        tap(2); // Pick the first song and review.
        assert(!songs.entry("Friday", 3, entry));
        tap(1); tap(2);
        while (songs.busy()) sample(0);
        sample(0);
        assert(songs.entry("Friday", 3, entry) && !std::strcmp(entry.song, "One"));
        tap(3); tap(0); tap(2); // Remove the just-added occurrence.
        assert(songs.entry("Friday", 3, entry));
        tap(1); tap(2);
        while (songs.busy()) sample(0);
        sample(0);
        assert(!songs.entry("Friday", 3, entry));
        settings();
        assert(!app.settings_frontmost());
        assert(!std::strcmp(songs.active_set(), "Set") && board.sent.size() == sent);
        assert(songs.continuation().entry == before.entry && songs.continuation().patch == before.patch);
        assert(preferences.boot());
        documents.bind_globals(preferences);
        assert(documents.startup_set("Set"));
        settings();
        tap(0); tap(0); tap(2);
        cntrl::Setlist first;
        assert(songs.set_at(0, first));
        if (std::strcmp(first.name, "Set")) tap(1);
        tap(2); tap(3); tap(1); tap(1); tap(2); // Rename the active startup set.
        tap(1); tap(5); // Tet, then review.
        assert(songs.setlist("Set") && !songs.setlist("Tet"));
        tap(1); tap(2); // Save.
        while (songs.busy()) sample(0);
        sample(0);
        assert(songs.success() && !std::strcmp(songs.active_set(), "Tet") && !std::strcmp(documents.startup_set(), "Tet"));
        settings();
        assert(!app.settings_frontmost());
        assert(songs.continuation().entry == before.entry && songs.continuation().patch == before.patch);
        const auto recover = [&](const char *name) {
            cntrl::GlobalSettings reopened(settings_disk);
            assert(reopened.boot());
            cntrl::Documents rebooted_documents(setup_storage, board, primary, board, &reopened);
            cntrl::Songs rebooted_songs(song_storage, board);
            rebooted_songs.bind_documents(rebooted_documents);
            assert(rebooted_songs.boot());
            rebooted_songs.recover_startup_set();
            assert(!std::strcmp(rebooted_documents.startup_set(), name) && rebooted_songs.setlist(name));
        };
        assert(songs.rename_setlist("Tet", "Show"));
        song_storage.remaining = 0;
        while (songs.busy()) songs.tick();
        song_storage.remaining = -1;
        assert(!songs.success());
        recover("Tet"); // The rename record was saved, but the library write was interrupted.
        assert(songs.rename_setlist("Tet", "Show"));
        settings_disk.blocked = true;
        while (songs.busy()) songs.tick();
        assert(songs.success() && !std::strcmp(documents.startup_set(), "Show"));
        settings_disk.blocked = false;
        recover("Show"); // The new library was saved, but the startup preference write failed.
        assert(preferences.boot() && !std::strcmp(documents.startup_set(), "Show"));
        assert(songs.rename_setlist("Show", "Other"));
        song_storage.remaining = 0;
        while (songs.busy()) songs.tick();
        song_storage.remaining = -1;
        assert(!songs.success());
        assert(songs.undo());
        while (songs.busy()) songs.tick();
        assert(songs.success() && !std::strcmp(documents.startup_set(), "Tet"));
        recover("Tet");
        assert(board.sent.size() == sent);
        return 0;
    }
    if (argc == 4 && !std::strcmp(argv[3], "--shortcut-conflict")) {
        songs.choose("Set"); app.song_ui.perform(); sample(0);
        settings();
        assert(!app.setups.open() && !songs.boundary() && board.sent.back().second == 1);
        return 0;
    }
    settings(); assert(app.settings_frontmost());
    settings(); assert(!app.setups.open() && board.page_directions.empty() && board.sent.empty());
    press(5);
    assert((board.page_directions == std::vector<bool>{false}));
    wait(8); release(5);
    assert((board.page_directions == std::vector<bool>{false}));
    board.page_directions.clear();
    songs.choose("Set"); app.song_ui.perform(); sample(0);
    settings();
    assert(app.settings_frontmost() && songs.boundary() && board.sent.empty());
    assert(std::strstr(board.panes[0].lines[3].text, "C+F"));
    tap(1); tap(2); tap(2); tap(1); // Adjust loaded setup brightness.
    assert(documents.dirty());
    settings();
    assert(app.settings_frontmost() && documents.dirty());
    tap(2); // Keep editing.
    assert(app.settings_frontmost() && documents.dirty());
    settings();
    tap(0); tap(2); // Discard and return.
    app.finish_tool();
    assert(!app.setups.open() && !documents.dirty() && songs.boundary() && board.sent.empty());
    if (argc == 3) {
        press(2); wait(8); release(2);
        assert(songs.boundary() && board.sent.empty() && app.song_ui.performing());
        hold();
        click(); hold();
        wait(5);
        assert((board.page_directions == std::vector<bool>{false, true}));
        assert(songs.boundary() && board.sent.empty());
        click();
        assert(songs.boundary() && board.sent.empty());
        wait(5);
        assert(!songs.boundary() && board.sent.back().second == 1);
        click();
        assert(board.sent.back().second == 1);
        click();
        assert(songs.boundary() && board.sent.back().second == 1);
        hold();
        click(); hold();
        wait(5);
        assert((board.page_directions == std::vector<bool>{false, true, false, true}));
        assert(songs.boundary() && board.sent.size() == 1);
    }
    if (argc == 4) {
        assert(songs.boundary() && board.sent.empty());
        next(); assert(board.sent == (std::vector<std::pair<unsigned, unsigned>>{{21, 11}, {20, 1}}));
        next(); assert(board.sent.back().second == 2);
        previous(); assert(board.sent.back().second == 1);
        previous(); assert(songs.boundary() && board.sent.back() == std::make_pair(21u, 12u));
        const auto before = board.sent.size();
        next(); assert(board.sent.size() == before + 2 && board.sent.back().second == 1);
        next(); next(); next(); assert(songs.boundary() && songs.next_position() == 1 && board.sent.back().second == 12);
        const auto gap_count = board.sent.size();
        next(); assert(board.sent.size() == gap_count + 2 && board.sent[gap_count].second == 21 && board.sent.back().second == 4);
        previous(); assert(songs.boundary() && board.sent.back().second == 22);
        previous(); assert(!std::strcmp(songs.current_name(), "One") && board.sent.back().second == 3);
        previous(); previous(); previous(); assert(songs.boundary() && board.sent.back().second == 12);
        const auto solo_count = board.sent.size();
        songs.choose_song("Two"); app.song_ui.perform(); sample(0);
        assert(songs.boundary() && board.sent.size() == solo_count);
        next(); assert(board.sent[solo_count].second == 21 && board.sent.back().second == 4);
        next(); next(); assert(songs.boundary() && board.sent.back().second == 22);
        const auto end_count = board.sent.size(); next(); assert(songs.boundary() && board.sent.size() == end_count);
        previous(); assert(!std::strcmp(songs.current_name(), "Two") && board.sent.back().second == 5);
        songs.choose_song("Two"); app.song_ui.perform(); sample(0);
        assert(songs.rename_song("Two", "Renamed")); while (songs.busy()) songs.tick();
        assert(songs.success() && songs.boundary() && !std::strcmp(songs.performance_name(), "Renamed"));
        assert(songs.undo()); while (songs.busy()) songs.tick();
        assert(songs.success() && songs.boundary() && !std::strcmp(songs.performance_name(), "Two"));
        next(); assert(!std::strcmp(songs.current_name(), "Two") && board.sent.back().second == 4);
        return 0;
    }
    next(); assert(songs.continuation().patch == 0 && board.sent.back().second == 1);
    assert(!std::strcmp(board.panes[1].lines[1].text, "One"));
    assert(!std::strcmp(board.panes[2].lines[1].text, "Set"));
    assert(!std::strcmp(board.panes[2].lines[0].text, "Next"));
    assert(!std::strcmp(board.panes[2].lines[2].text, "Accent"));
    const std::string scene = board.panes[0].lines[1].text;
    const auto before_settings = board.sent.size();
    settings(); assert(app.settings_frontmost());
    settings(); assert(!app.setups.open() && !songs.boundary());
    assert(board.sent.size() == before_settings && songs.continuation().patch == 0);
    board.switches.refresh();
    for (auto displays : {1u, 2u, 3u}) {
        board.displays = displays; board.switches.refresh(); sample(0);
        assert(board.panes[0].lines[1].text == scene);
        assert(board.panes[0].split_middle == (displays == 1));
        assert(!std::strcmp(displays == 1 ? board.panes[0].lines[3].text : board.panes[1].lines[1].text, "One"));
    }
    assert(board.entry.start(board.document, unsigned(board.document.named_patch("forward")))); sample(0);
    assert(songs.continuation().patch == 1 && board.sent.back().second == 2);
    assert(board.entry.start(board.document, unsigned(board.document.named_patch("backward")))); sample(0);
    assert(songs.continuation().patch == 0 && board.sent.back().second == 1);
    auto count = board.sent.size();
    press(2); wait(8); release(2);
    assert(board.sent.size() == count + 1 && board.sent.back() == std::make_pair(99u, 1u));
    sample(1u << 2, 20); sample(0);
    assert(board.sent.size() == count + 2 && board.sent.back() == std::make_pair(99u, 1u));
    assert(songs.continuation().patch == 0);
    next(); next(); assert(songs.continuation().patch == 2 && board.sent.back().second == 3);
    previous(); assert(songs.continuation().patch == 1 && board.sent.back().second == 2);
    previous(); assert(songs.continuation().patch == 0 && board.sent.back().second == 1);
    count = board.sent.size(); previous(); assert(songs.boundary() && board.sent.size() == count);
    previous(); assert(songs.boundary() && board.sent.size() == count && !board.last_error.empty());
    next(); assert(board.sent.back().second == 1);
    next(); next(); press(0); assert(board.sent.back() == std::make_pair(64u, 127u));
    count = board.sent.size(); next();
    assert(songs.boundary() && board.sent.size() == count);
    assert(!std::strcmp(board.panes[0].lines[2].text, "Empty position") && !std::strcmp(board.panes[2].lines[1].text, "Two"));
    press(1); release(1); assert(board.sent.size() == count); // Hidden map cannot trigger an action.
    release(0); assert(board.sent.back() == std::make_pair(64u, 0u));
    next(); assert(!std::strcmp(songs.current_name(), "Two") && board.sent.back().second == 4);
    assert(!board.panes[2].lines[2].text[0]);
    count = board.sent.size(); press(2); release(2); assert(board.sent.size() == count);
    count = board.sent.size(); previous(); assert(songs.boundary() && board.sent.size() == count);
    previous(); assert(songs.continuation().patch == 2 && board.sent.back().second == 3);
    next(); next(); next(); assert(board.sent.back().second == 5);
    next(); assert(songs.boundary() && songs.continuation().entry == 2);
    next(); assert(songs.continuation().entry == 2 && songs.continuation().patch == 0 && board.sent.back().second == 1);
    previous(); assert(songs.boundary());
    previous(); assert(!songs.boundary() && !std::strcmp(songs.current_name(), "Two") && board.sent.back().second == 5);
    next(); next(); next(); next(); next(); assert(songs.boundary());
    count = board.sent.size(); next(); assert(songs.boundary() && board.sent.size() == count);
    assert(songs.rename_song("Two", "Renamed")); while (songs.busy()) songs.tick();
    assert(songs.success() && songs.song("Renamed")->count == 2);
    assert(songs.undo()); while (songs.busy()) songs.tick();
    assert(songs.success() && songs.song("Two")->count == 2);
}
