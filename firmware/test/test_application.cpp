#include <cntrl/ui/application.h>
#include <cntrl/ui/song_sources.h>
#include <cntrl/ui/firmware_visit.h>
#include <cntrl/ui/page_host.h>
#include "patch_hardware.h"
#include "storage.h"
#include <cassert>
#include <fstream>
#include <iterator>

struct Hardware : TestPatchHardware, cntrl::Replies, cntrl::ui::ApplicationServices, cntrl::SongResolver, cntrl::SongCapture, cntrl::ui::FirmwareVisitServices {
    unsigned blanks = 0, editors = 0;
    bool idle = true, transfer_busy = false, editor = false;
    uint8_t brightness = 0;
    cntrl::ui::FirmwareSelection selection{7, 0, true, false};
    unsigned configurations = 0, selections = 0, handoffs = 0, consumed = 0;
    std::string last_error;
    cntrl::ui::FirmwareSelection firmware_selection() const override { return selection; }
    bool valid_exit(uint8_t pair) const override { return pair == 33; }
    bool storage_available() const override { return true; }
    bool stop_clock() override { return true; }
    bool configure_return(uint8_t pair, uint32_t token) override { assert(pair == 33 && (token == 0 || token == 7)); ++configurations; return true; }
    bool choose_external_firmware() override { ++selections; return true; }
    bool consume_return(uint32_t token) override { consumed = token; return true; }
    void handoff_firmware() override { ++handoffs; }
    bool switch_invocation(cntrl::PatchExecutor &) const override { return false; }
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 200; }
    unsigned display_height() const override { return 120; }
    unsigned display_count() const override { return 3; }
    const char *control_label(cntrl::ControlRole role) const override {
        const char *labels[] = {"A", "B", "C", "D", "E", "F"}; return labels[unsigned(role)];
    }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t value, bool = false) override { brightness = value; }
    void set_splash(bool, uint32_t) override {}
    void error(const char *message) override { last_error = message; }
    void reply(const char *) override {}
    void data(const uint8_t *, unsigned) override { assert(false); }
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
struct VisitFile : cntrl::files::File {
    std::vector<uint8_t> bytes;
    bool active = false, bad_write = false;
    unsigned opened_count = 0, maximum_io = 0;
    bool open(const char *, cntrl::files::Mode mode) override {
        ++opened_count; active = mode != cntrl::files::Mode::read || !bytes.empty();
        if (mode == cntrl::files::Mode::truncate) bytes.clear();
        return active;
    }
    void close() override { active = false; }
    bool opened() const override { return active; }
    uint32_t size() const override { return uint32_t(bytes.size()); }
    bool read(uint32_t offset, uint8_t *data, unsigned length) override {
        maximum_io = std::max(maximum_io, length);
        if (offset + length > bytes.size()) return false;
        std::memcpy(data, bytes.data() + offset, length); return true;
    }
    bool write(uint32_t offset, const uint8_t *data, unsigned length) override {
        maximum_io = std::max(maximum_io, length); if (bad_write) return false;
        bytes.resize(offset + length); std::memcpy(bytes.data() + offset, data, length); return true;
    }
    bool sync() override { return true; }
};
struct PagePlatform : Hardware {
    cntrl::midi::Monitor monitor;
    cntrl::midi::Monitor &midi_monitor() { return monitor; }
    uint32_t microseconds() const { return 0; }
    bool switch_firmware() { return false; }
    struct Expression {
        unsigned index = 0;
        void configure(const cntrl::ExpressionAssignment *) {}
        void cancel() {}
        void select(unsigned value) { index = value; }
        unsigned selected() const { return index; }
        void calibrate(bool) {}
    } expression;
    struct Bridge { void clock_owner(unsigned) {} } bridge;
    cntrl::midi::Outputs endpoints;
    cntrl::midi::Outputs midi_outputs() const override { return endpoints; }
    cntrl::midi::TrsMode port_mode(unsigned) const { return cntrl::midi::TrsMode::off; }
    bool supports_trs_mode(unsigned port, cntrl::midi::TrsMode mode) const override {
        return mode == cntrl::midi::TrsMode::off || (port == 1 && mode == cntrl::midi::TrsMode::midi_a) || (port == 3 && mode == cntrl::midi::TrsMode::expression);
    }
    Expression &expression_controls() { return expression; }
    Bridge &midi_bridge() { return bridge; }
    unsigned midi_file_clock_outputs() const { return 0; }
    bool expression_available(unsigned) const { return true; }
};
struct PageFonts : cntrl::ui::Fonts {
    bool supports(unsigned, unsigned, uint32_t = 0) const override { return true; }
    cntrl::ui::Bounds bounds(unsigned, unsigned, uint32_t, const char *s) const override { return {0, 0, int(std::strlen(s) * 3), 5}; }
    bool glyph(unsigned, unsigned, uint32_t, const char *, unsigned, cntrl::ui::Glyph &) const override { return false; }
};
struct PageDisplay : cntrl::ui::Display {
    unsigned windows = 1;
    std::vector<std::vector<uint16_t>> pixels = std::vector<std::vector<uint16_t>>(3, std::vector<uint16_t>(64 * 64));
    unsigned count() const override { return windows; }
    cntrl::ui::Viewport viewport(unsigned) const override { return {64, 64, 0, 0}; }
    void poll() override {}
    bool ready() const override { return true; }
    bool failed() override { return false; }
    bool priority_due() const override { return false; }
    bool submit(unsigned screen, unsigned x, unsigned y, unsigned width, unsigned height, const uint8_t *data) override {
        for (unsigned col = 0; col < width; ++col) for (unsigned row = 0; row < height; ++row) {
            const unsigned offset = 2 * (col * height + row);
            pixels[screen][(y + row) * 64 + x + col] = uint16_t(data[offset]) << 8 | data[offset + 1];
        }
        return true;
    }
};
static void page_ports_follow_declared_sparse_ids_and_supported_modes(cntrl::Documents &documents, cntrl::ui::Application &app) {
    PagePlatform platform; PageFonts fonts; PageDisplay display;
    uint16_t work[64 * 64] = {}, cache[64 * 64] = {}, previous[64 * 64] = {};
    cntrl::ui::Renderer renderer(display, fonts, work, 64 * 64, cache, previous, 64 * 64);
    constexpr cntrl::ui::ControlBinding bindings[] = {{0, "Previous"}, {1, "Next"}, {2, "Confirm"}};
    cntrl::ui::PageHost<PagePlatform> host(platform, app, documents, display, fonts, renderer, {0, 1, 2, 0, 1, 2, 2, 0, 2, bindings, 3});
    cntrl::midi::OutputConnection endpoints[] = {
        {"north", "North", 0, cntrl::midi::OutputTransport::physical, false, true, 1},
        {"south", "South", 1, cntrl::midi::OutputTransport::physical, false, true, 3}};
    platform.endpoints = {endpoints, 2}; cntrl::PageSettings settings; settings.words[cntrl::PageSettings::title_height] = 8;
    host.configure_page_settings(settings);
    uint8_t layout[8 + 32 + 35] = {'C', 'P', 'A', 'G', 1, 1, 1, 0};
    layout[8 + 32 + 32] = unsigned(cntrl::ui::Pages::Page::ports); layout[8 + 32 + 33] = 1;
    host.pages_.layout({layout});
    assert(host.pages_.jack == 1); assert(host.trs_button(1));
    assert(host.pages_.candidate == cntrl::midi::TrsMode::midi_a);
    assert(host.trs_button(1)); assert(host.pages_.candidate == cntrl::midi::TrsMode::off);
    assert(host.trs_button(0)); assert(host.pages_.jack == 3);
    assert(host.trs_button(1)); assert(host.pages_.candidate == cntrl::midi::TrsMode::expression);
    const auto named = host.pages_.view(0, 0, {}); assert(std::strcmp(named.lines[1].text, "South: Off") == 0);
    assert(host.trs_button(0)); assert(host.pages_.jack == 1);
    platform.endpoints = {}; host.configure_page_settings(settings);
    const unsigned selected = host.pages_.jack; assert(host.trs_button(0)); assert(host.pages_.jack == selected);
    const auto none = host.pages_.view(0, 0, {});
    assert(std::strstr(none.lines[1].text, "No configurable") != nullptr && !none.lines[3].text[0] && !none.lines[4].text[0]);
}
static void monitor_page_controls_and_capture_follow_lifecycle(cntrl::Documents &documents, cntrl::ui::Application &app) {
    PagePlatform platform; PageFonts fonts; PageDisplay display;
    uint16_t work[64 * 64] = {}, cache[64 * 64] = {}, previous[64 * 64] = {};
    cntrl::ui::Renderer renderer(display, fonts, work, 64 * 64, cache, previous, 64 * 64);
    constexpr cntrl::ui::ControlBinding bindings[] = {{0,"A"},{1,"B"},{2,"C"},{3,"D"},{4,"E"},{5,"F"}};
    cntrl::ui::PageHost<PagePlatform> host(platform, app, documents, display, fonts, renderer, {0,1,2,3,4,5,4,3,2,bindings,6});
    uint8_t layout[8 + 32 + 35] = {'C','P','A','G',1,1,1,0};
    layout[8 + 32 + 32] = unsigned(cntrl::ui::Pages::Page::midi_monitor); layout[8 + 32 + 33] = 1;
    host.pages_.layout({layout}); host.enter_page();
    assert(platform.monitor.active());
    platform.monitor.packet(cntrl::midi::Monitor::Direction::incoming, 0, 0, {12,{0xc0,1,0}}, 10);
    assert(host.monitor_button(2, true) && platform.monitor.frozen());
    assert(host.monitor_button(2, false) && platform.monitor.frozen());
    assert(host.monitor_button(2, true) && !platform.monitor.frozen());
    host.leave_page(); assert(!platform.monitor.active());
    host.enter_page(); assert(platform.monitor.active() && platform.monitor.tail() == 0);
    assert(host.monitor_button(5, true) && !platform.monitor.active() && app.menu.open());
    app.menu.leave(); host.enter_page(); assert(platform.monitor.active());
    host.configure_page_settings({}); assert(!platform.monitor.active());
    const unsigned budget = cntrl::memory_budget; cntrl::memory_budget = cntrl::memory_used;
    host.enter_page(); assert(!platform.monitor.active());
    assert(host.monitor_button(2, true) && !platform.monitor.frozen());
    cntrl::memory_budget = budget;
}
static void selecting_pages_preserves_state_and_background_work(const char *path) {
    struct Context : Hardware {
        cntrl::ui::Application *application = nullptr;
        unsigned sends = 0;
        bool send(const char *, const uint8_t *, unsigned) override { ++sends; return true; }
        bool state(unsigned operation, uint32_t value) override { return application->state(operation, value, *this); }
        void validation(bool active) override { application->controller().validation(active); }
        bool preview_state(unsigned operation, uint32_t value) override { return application->controller().preview_state(operation, value); }
        bool selection(unsigned operation, const cntrl::Document *&document, unsigned &node) override { return application->controller().selection(operation, document, node); }
        void selection_complete(unsigned operation) override { application->controller().selection_complete(operation); }
    } hardware;
    std::ifstream file(path); std::string hex; file >> hex;
    std::vector<uint8_t> bytes;
    for (unsigned i = 0; i < hex.size(); i += 2) bytes.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
    cntrl::Document document; assert(document.load(bytes.data(), unsigned(bytes.size()), hardware));
    const unsigned monitor_page = unsigned(document.page_layout().page(1) - bytes.data());
    for (unsigned count : {0u,1u,2u,3u,4u}) {
        auto monitored = bytes; monitored[monitor_page] = cntrl::PageLayout::midi_monitor; monitored[monitor_page + 1] = uint8_t(count);
        cntrl::put32(monitored.data() + 20, cntrl::checksum(monitored.data() + 28, unsigned(monitored.size() - 28)));
        cntrl::Document checked; assert(checked.load(monitored.data(), unsigned(monitored.size()), hardware) == (count >= 1 && count <= 3));
    }
    // Match the pedal's one-window Tap Tempo page followed by its patch page.
    const unsigned tap = unsigned(document.page_layout().page(4) - bytes.data());
    bytes[tap + 1] = 1;
    cntrl::put32(bytes.data() + 20, cntrl::checksum(bytes.data() + 28, unsigned(bytes.size() - 28)));
    assert(document.load(bytes.data(), unsigned(bytes.size()), hardware));
    TestStorage storage, library;
    cntrl::Controller primary(hardware, hardware, hardware);
    cntrl::Documents documents(storage, hardware, primary, hardware);
    cntrl::Songs songs(library, hardware);
    alignas(cntrl::Controller) unsigned char extra[(cntrl::PageLayout::max_controllers - 1) * sizeof(cntrl::Controller)];
    for (unsigned i = 1; i < cntrl::PageLayout::max_controllers; ++i) new (extra + (i - 1) * sizeof(cntrl::Controller)) cntrl::Controller(hardware, hardware, hardware, 5 + i);
    {
        cntrl::ui::Application app(hardware, documents, primary, extra, songs, hardware, false); hardware.application = &app;
        primary.activate(document); app.configure(document, hardware);
        PagePlatform platform; PageFonts fonts; PageDisplay display;
        display.windows = 3;
        uint16_t work[4096] = {}, cache[3 * 4096] = {}, previous[3 * 4096] = {};
        cntrl::ui::Renderer renderer(display, fonts, work, 4096, cache, previous, 3 * 4096);
        cntrl::ui::PageHost<PagePlatform> host(platform, app, documents, display, fonts, renderer, {});
        host.pages_.layout(document.page_layout());
        host.select_page(5); assert(app.visible_index == 0 && host.pages_.windows() == 0);
        assert(app.controller().navigate(7)); app.tick(0); assert(app.busy() && hardware.sends == 0);
        host.select_page(6); assert(app.visible_index == 1);
        assert(app.controller().navigate(3)); assert(app.controller().selected_patch() == 1);
        host.select_page(1); assert(app.visible_index == 1 && host.pages_.windows() == 1);
        for (unsigned i = 0; i < 100; ++i) app.tick(200);
        assert(!app.busy() && hardware.sends == 1);
        host.select_page(5); assert(app.controller().selected_patch() == 0 && app.controller().navigation().center == 0);
        host.select_page(6); assert(app.controller().selected_patch() == 1);
        host.select_page(5); app.controller().button(1, true, 300);
        for (unsigned i = 0; i < 20; ++i) app.tick(300);
        assert(app.controller().selected_patch() == 1);
        host.select_page(6); host.select_page(5); app.controller().button(1, true, 400);
        for (unsigned i = 0; i < 20; ++i) app.tick(400);
        assert(app.controller().selected_patch() == 0);
        host.select_page(6);
        host.pages_.browse(true); assert(host.pages_.index() == 5);
        host.pages_.browse(false); assert(host.pages_.index() == 6);
        host.pages_.browse(false); assert(host.pages_.index() == 0);
        char put[128]; std::snprintf(put, sizeof(put), "CNTRL SETUP PUT 54657374 %u %08lx 0\n",
            unsigned(bytes.size()), (unsigned long)cntrl::checksum(bytes.data(), unsigned(bytes.size())));
        for (char c : std::string(put)) documents.receive(uint8_t(c), 0);
        for (uint8_t b : bytes) documents.receive(b, 0);
        for (unsigned i = 0; documents.busy() && i < 10000; ++i) documents.tick(0);
        assert(!documents.busy() && documents.select("Test"));
        host.select_page(5); host.select_page(4);
        for (unsigned screen = 0; screen < 3; ++screen) {
            cntrl::Pane pane; pane.graphics.background = uint16_t(screen + 1); pane.graphics.pad = 0;
            renderer.show(screen, pane);
        }
        for (unsigned i = 0; i < 1000; ++i) renderer.draw(i * 1000);
        assert(renderer.ready());
        // Leaving Tap Tempo keeps the same patch controller visible. Only the
        // outgoing utility window may move.
        host.browse_page(false); assert(host.pages_.index() == 5);
        for (unsigned i = 0; i < 20000; ++i) {
            renderer.draw(1000000 + i * 1000);
            for (unsigned screen = 0; screen < 2; ++screen)
                for (auto pixel : display.pixels[screen]) assert(pixel == screen + 1);
        }
        assert(renderer.ready());
    }
    for (unsigned i = 1; i < cntrl::PageLayout::max_controllers; ++i) std::launder(reinterpret_cast<cntrl::Controller *>(extra + (i - 1) * sizeof(cntrl::Controller)))->~Controller();
}
static void firmware_handoff_is_durable_and_corrupt_return_is_rejected(Hardware &hardware, cntrl::Documents &documents,
        cntrl::ui::Application &app, cntrl::Songs &songs, cntrl::ui::SongSources &sources) {
    VisitFile file; cntrl::ui::FirmwareVisitRecord record; cntrl::ui::RuntimeState runtime;
    cntrl::Controller::NavigationState navigation[9]; cntrl::PatchExecutor *execution = &app.controller().executor(); bool restoring = false;
    cntrl::ui::FirmwareVisit visit(record, file, hardware, hardware, documents, app, songs, sources, runtime, navigation, execution, restoring);
    assert(!visit.request(12, nullptr)); hardware.idle = false; assert(!visit.request(33, nullptr)); hardware.idle = true;
    const cntrl::SongContinuation continuation{};
    assert(visit.request(33, &continuation)); assert(visit.busy() && !visit.request(33, nullptr));
    for (unsigned i = 0; i < 100 && !hardware.handoffs; ++i) visit.poll(true);
    assert(hardware.handoffs == 1 && hardware.configurations == 1 && hardware.selections == 1);
    assert(file.bytes.size() == sizeof(record) && file.maximum_io <= 512 && !file.opened());
    file.bytes[0] ^= 1; hardware.selection.returned = 7; hardware.selection.returned_to_application = true;
    cntrl::ui::FirmwareVisitRecord returned_record;
    cntrl::ui::FirmwareVisit returned(returned_record, file, hardware, hardware, documents, app, songs, sources, runtime, navigation, execution, restoring);
    const unsigned opens = file.opened_count; returned.poll(false); assert(file.opened_count == opens && !returned.busy());
    uint8_t layout[8 + 3 * 32 + 37] = {'C', 'P', 'A', 'G', 1, 3, 1, 0}; layout[8 + 3 * 32 + 32] = 11; layout[8 + 3 * 32 + 33] = 3; app.layout = {layout};
    for (unsigned i = 0; i < 100; ++i) returned.poll(true);
    assert(!returned.busy() && hardware.consumed == 7 && hardware.last_error == "Patch return unavailable");
    assert(hardware.handoffs == 1); app.layout = {};
    VisitFile broken; broken.bad_write = true;
    cntrl::ui::FirmwareVisitRecord failed_record;
    cntrl::ui::FirmwareVisit failed(failed_record, broken, hardware, hardware, documents, app, songs, sources, runtime, navigation, execution, restoring);
    assert(failed.request(33, &continuation)); failed.poll(true); failed.poll(true);
    assert(!failed.busy() && !execution && hardware.last_error == "Firmware switch failed");
}
int main(int argc, char **argv) {
    assert(argc == 2); selecting_pages_preserves_state_and_background_work(argv[1]);
    Hardware hardware; TestStorage setup_storage, song_storage;
    cntrl::Controller primary(hardware, hardware, hardware);
    cntrl::Documents documents(setup_storage, hardware, primary, hardware);
    alignas(cntrl::Controller) unsigned char extra[(cntrl::PageLayout::max_controllers - 1) * sizeof(cntrl::Controller)];
    for (unsigned i = 0; i + 1 < cntrl::PageLayout::max_controllers; ++i) new (extra + i * sizeof(cntrl::Controller)) cntrl::Controller(hardware, hardware, hardware, i + 6);
    {
        cntrl::Songs songs(song_storage, hardware);
        cntrl::ui::Application app(hardware, documents, primary, extra, songs, hardware, true);
        assert(&app.controller() == &primary && !app.feature_open(hardware));
        cntrl::SetupTable setups;
        cntrl::ui::SongSources source(hardware, hardware, hardware, setups, documents, app, songs);
        cntrl::Song capture; const cntrl::Document *captured = nullptr;
        assert(!source.capture(capture, true) && !source.capture_source(captured));
        cntrl::SongSelection missing; std::strcpy(missing.document, "missing");
        uint32_t revision = 0; assert(!source.revision(missing, revision));
        assert(!source.activate(missing, {}, false));
        firmware_handoff_is_durable_and_corrupt_return_is_rejected(hardware, documents, app, songs, source);
        page_ports_follow_declared_sparse_ids_and_supported_modes(documents, app);
        monitor_page_controls_and_capture_follow_lifecycle(documents, app);
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
        app.processing_index = 1; assert(&app.controller() == &app.at(1)); app.tick(0); assert(app.processing_index == cntrl::PageLayout::max_controllers);
    }
    for (unsigned i = 0; i + 1 < cntrl::PageLayout::max_controllers; ++i) std::launder(reinterpret_cast<cntrl::Controller *>(extra + i * sizeof(cntrl::Controller)))->~Controller();
}
