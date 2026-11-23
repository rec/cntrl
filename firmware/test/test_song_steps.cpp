#include <cntrl/ui/input.h>
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
    unsigned windows = 3;
    unsigned display_count() const override { return windows; }
    unsigned input_count() const override { return 6; }
    unsigned switch_capacity() const override { return 5; }
    const char *input_label(unsigned input) const override { static const char *labels[] = {"A", "B", "C", "D", "E", "F"}; return labels[input]; }
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
    void open_switch_editor(const cntrl::Document &d, uint64_t held) override { assert(switches.enter(d, uint32_t(held))); }
    bool shift_switch_editor(bool backwards) override { return switches.shift(backwards); }
    bool state(unsigned operation, uint32_t value) override { return app->state(operation, value, *this); }
    bool song_navigation_active() const override { return app && app->song_navigation_active(); }
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
    bool menu_open() const { return board.app->menu.open(); }
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
    assert(argc == 3 || argc == 4); cntrl::memory_budget = 2 * 1024 * 1024;
    Board board; TestStorage setup_storage, song_storage;
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
    cntrl::ui::Application app(board, documents, primary, board, board, songs, board, true); board.app = &app;
    assert(app.prepare_pages(configuration)); app.finish_pages(true); app.layout = configuration.page_layout(); primary.activate(configuration, false);
    cntrl::ui::ControlBinding bindings[] = {{0, "A"}, {1, "B"}, {2, "C"}, {3, "D"}, {4, "E"}, {5, "F"}};
    cntrl::ui::Controls controls{0, 1, 2, 3, 4, 5, 5, 3, 4, bindings, 6};
    cntrl::ButtonState states[6]; cntrl::ui::InputCoordinator input(states, controls, configuration, app, board);
    Services services{{board}}; uint8_t held = 0; unsigned revision = 0;
    auto sample = [&](uint8_t value) {
        held = value;
        for (unsigned i = 0; i < 64; ++i) {
            input.scan(held, board.now, services); app.tick(board.now); board.entry.tick(board.now); songs.tick();
            board.switches.tick(board.now++, held);
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
    press(5);
    assert((board.page_directions == std::vector<bool>{false}));
    wait(8); release(5);
    assert((board.page_directions == std::vector<bool>{false}));
    board.page_directions.clear();
    songs.choose("Set"); app.song_ui.perform(); sample(0);
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
    for (auto windows : {1u, 2u, 3u}) {
        board.windows = windows; board.switches.refresh(); sample(0);
        assert(board.panes[0].lines[1].text == scene);
        assert(board.panes[0].split_middle == (windows == 1));
        assert(!std::strcmp(windows == 1 ? board.panes[0].lines[3].text : board.panes[1].lines[1].text, "One"));
    }
    assert(board.entry.start(board.document, unsigned(board.document.named_patch("forward")))); sample(0);
    assert(songs.continuation().patch == 1 && board.sent.back().second == 2);
    assert(board.entry.start(board.document, unsigned(board.document.named_patch("backward")))); sample(0);
    assert(songs.continuation().patch == 0 && board.sent.back().second == 1);
    auto count = board.sent.size();
    press(2); wait(8); release(2);
    assert(board.sent.size() == count + 1 && board.sent.back() == std::make_pair(99u, 1u));
    assert(!app.menu.open() && songs.continuation().patch == 0);
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
