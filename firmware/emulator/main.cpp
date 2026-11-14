#define CNTRL_SWITCHES 1
#define CNTRL_PANIC 0
#define CNTRL_CABLE_TEST 0
#define CNTRL_ORIGINAL_FIRMWARE 0
#include "target_capabilities.h"
#include "fonts.h"
#include "protocol.h"
#include <cntrl/host/filesystem.h>
#include <cntrl/setup_files.h>
#include <cntrl/bundled_storage.h>
#include <cntrl/slot_storage.h>
#include <cntrl/ui/application_hardware.h>
#include <cntrl/ui/page_host.h>
#include <cntrl/ui/input.h>
#include <cntrl/ui/song_sources.h>
#include <cntrl/ui/font_storage.h>
#include <cntrl/midi_runtime.h>
#include <cntrl/expression_pedals.h>
#include <cntrl/auxiliary_switches.h>
#include <cntrl/asset_storage.h>
#include <cntrl/animation/session.h>
#include <cntrl/runtime.h>
#include <deque>
#include <fstream>
#include <iterator>
#include <memory>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace cntrl::emulator {
class Display final : public ui::Display {
public:
    unsigned count() const override { return target::display_count; }
    ui::Viewport viewport(unsigned index) const override { return target::viewports[index]; }
    void poll() override {}
    bool ready() const override { return true; }
    bool failed() override { return false; }
    bool priority_due() const override { return false; }
    bool submit(unsigned screen, unsigned x, unsigned y, unsigned width, unsigned height, const uint8_t *pixels) override {
        // Keep each protocol frame bounded even for unusually tall viewports.
        for (unsigned first = 0; first < height;) {
            unsigned rows = std::min(height - first,(65536u - 11) / (2 * width));
            if (!rows) return false;
            std::vector<uint8_t> bytes{1};
            for (unsigned value : {screen,x,y + first,width,rows}) integer(bytes,value,2);
            for (unsigned column = 0; column < width; ++column) {
                auto *begin = pixels + 2 * (column * height + first);
                bytes.insert(bytes.end(),begin,begin + 2 * rows);
            }
            frame(bytes); first += rows;
        }
        return true;
    }
};

class MovieDisplay final : public animation::Output {
public:
    explicit MovieDisplay(Display &display) : display_(display) {}
    bool begin(unsigned width,unsigned height,unsigned streams) override {
        if (streams != display_.count()) { error("Movie stream count does not match execution target"); return false; }
        for (unsigned i = 0; i < streams; ++i) {
            const auto viewport = display_.viewport(i);
            if (viewport.width != width || viewport.height != height) { error("Movie geometry does not match execution target"); return false; }
        }
        return true;
    }
    bool ready() const override { return display_.ready(); }
    bool pixels(unsigned stream,unsigned x,unsigned y,const uint8_t *data,unsigned count) override { return display_.submit(stream,x,y,count,1,data); }
private:
    Display &display_;
};

class Board : public ui::ApplicationHardware<Board>, public Replies, public animation::Replies, public KeyboardSink,
              public midi::Sink, public PatchOutputs, public PatchSources,
              public ui::ApplicationServices, public SongResolver, public SongCapture {
public:
    ui::Application *app = nullptr;
    Documents *documents = nullptr;
    ui::PageHost<Board> *host = nullptr;
    midi::MidiFiles *files = nullptr;
    Switches *switches = nullptr;
    ui::SongSources *song_sources = nullptr;
    ExpressionPedals *expression = nullptr;
    font_assets::Storage *font_storage = nullptr;
    const ui::Fonts *fonts = nullptr;
    StoredPatchSources *sources = nullptr;
    animation::Session *animation = nullptr;
    animation::Player *movie_player = nullptr;
    uint64_t time = 0;
    uint64_t accepted_packets = 0;
    uint8_t held = 0;
    unsigned blocked = 0, connected = 0xffff;
    std::array<uint64_t,16> generations{};
    uint8_t modes[4] = {};
    uint16_t readings[4] = {};
    uint8_t contacts[4] = {};
    ui::RuntimeState runtime;
    midi::Runtime midi_runtime;
    midi::Bridge bridge{target::midi_connections};
    std::vector<std::deque<midi::InputEvent>> incoming = std::vector<std::deque<midi::InputEvent>>(target::endpoints.size());
    ui::PageHost<Board> &page_host() { return *host; }
    uint32_t microseconds() const { return uint32_t(time); }
    uint32_t milliseconds() const { return uint32_t(time / 1000); }
    uint32_t application_time() const override { return milliseconds(); }
    unsigned input_count() const override { return target::button_count; }
    const char *input_label(unsigned i) const override { return i < target::button_count ? target::button_labels[i] : ""; }
    unsigned control_input(ControlRole role) const override {
        const auto &c = target::controls;
        const unsigned indices[] = {c.previous,c.next,c.confirm,c.previous_section,c.next_section,c.back}; return indices[unsigned(role)];
    }
    const char *control_label(ControlRole role) const override { return input_label(control_input(role)); }
    unsigned display_count() const override { return target::display_count; }
    const char *display_id(unsigned i) const override { return target::display_ids[i]; }
    unsigned display_width() const override { return target::viewports[0].width; }
    unsigned display_height() const override { return target::viewports[0].height; }
    unsigned memory_capacity() const override { return target::memory_capacity; }
    unsigned memory_runtime() const override { return target::memory_runtime; }
    unsigned switch_capacity() const override { return target::max_switches; }
    bool prepare_pages(const Document &d) override {
        if (!app->prepare_pages(d)) return false;
        if (!midi_runtime.streams().prepare_load(d.midi_streams(),d.midi_streams_size(),midi_outputs(),d.nodes(),time)) {
            app->finish_pages(false); return false;
        }
        if (!midi_runtime.prepare_arpeggiator_configuration(d.arpeggiator_settings(),*this,midi_outputs())) {
            midi_runtime.streams().finish_load(false); app->finish_pages(false); return false;
        }
        return true;
    }
    void finish_pages(bool commit) override { midi_runtime.streams().finish_load(commit); midi_runtime.finish_arpeggiator_configuration(commit); app->finish_pages(commit); }
    midi::Outputs midi_outputs() const override { return target::midi_connections; }
    bool supports(const char *name) const override { auto *endpoint = midi_outputs().find(name); return endpoint && endpoint->output; }
    bool available(const char *name) const override {
        const auto *endpoint = midi_outputs().find(name);
        return endpoint && endpoint->output && (connected & (1u << endpoint->route))
            && (endpoint->port == midi::no_port || midi::midi_output(port_mode(endpoint->port)));
    }
    bool supports_font(unsigned face,unsigned size) const override;
    bool supports_font_asset(uint32_t asset,unsigned size) const override { return font_storage && font_storage->supports(asset,size); }
    bool begin_fonts(unsigned count) override { return font_assets::cache.begin(count); }
    bool stage_font(uint32_t asset,unsigned size) override { return font_storage && font_storage->stage(asset,size); }
    void finish_fonts(bool commit) override { font_assets::cache.finish(commit); }
    bool supports_keyboard() const override { return false; }
    bool keyboard_connected() const { return false; }
    bool keyboard_report(const KeyboardReport &) override { return false; }
    bool supports_trs_mode(unsigned port,midi::TrsMode mode) const override {
        return port < target::port_count && unsigned(mode) <= unsigned(midi::TrsMode::aux) && (target::trs_mode_masks[port] & (1u << unsigned(mode)));
    }
    bool supports_relay(const char *name) const override { for (const char *relay : target::relay_names) if (!std::strcmp(name,relay)) return true; return false; }
    bool relay_write(const char *name,bool engaged) override {
        if (!supports_relay(name)) return false;
        std::vector<uint8_t> bytes{4,uint8_t(engaged)}; bytes.insert(bytes.end(),name,name + std::strlen(name)); frame(bytes); return true;
    }
    midi::TrsMode port_mode(unsigned port) const { return port < 4 ? midi::TrsMode(modes[port]) : midi::TrsMode::off; }
    void configure_trs_ports(const uint8_t *value) override { std::memcpy(modes,value,4); }
    void set_midi_interface(bool enabled) override { runtime.interface = enabled; bridge.configure(enabled); }
    void set_usb_midi(bool device,bool host_enabled) override { bridge.configure_usb(device,host_enabled,milliseconds()); }
    void blank_performance() override { ui::ApplicationHardware<Board>::blank_performance(); }
    void error(const char *text) override { emulator::error(text); if (host) host->error(text); }
    void reply(const char *text) override { if (!std::strncmp(text,"CNTRL ERROR",11) || !std::strncmp(text,"CNTRL SPLASH ERROR",18)) error(text); }
    void data(const uint8_t *,unsigned) override { error("Unexpected serial data in emulator"); }
    bool output_idle() const override { return bridge.idle(); }
    bool configuration_locked() const override { return switches && switches->open(); }
    bool song_transfer_busy() const override { return false; }
    bool switch_editor_open() const override { return switches && switches->open(); }
    void open_switch_editor(const Document &d,uint64_t held_inputs) override { switches->enter(d,uint32_t(held_inputs)); }
    bool shift_switch_editor(bool reverse) override { return switches->shift(reverse); }
    void display_brightness(uint8_t) {}
    bool display_priority_due() const { return false; }
    bool display_busy() const { return false; }
    void displayed() { midi_runtime.displayed(); }
    animation::Session &animation_session() { return *animation; }
    void set_animation_color_cycle(bool enabled) override { if (!movie_player->set_color_cycle(enabled)) error("Animation color memory unavailable"); }
    midi::Monitor &midi_monitor() { return midi_runtime.monitor(); }
    TextMessages &text_messages() { return midi_runtime.text_messages; }
    midi::Bridge &midi_bridge() { return bridge; }
    ExpressionPedals &expression_controls() { return *expression; }
    bool expression_available(unsigned port) const { return port < 4 && port_mode(port) == midi::TrsMode::expression; }
    bool looper_storage_ready() const { return true; }
    bool switch_firmware() { error("Stock firmware execution is not supported by the emulator"); return false; }
    unsigned midi_file_clock_outputs() const { return files->player.clock_outputs(); }
    ui::PerformanceStatus status() const { ui::PerformanceStatus value; value.ram_used[1] = memory_used; value.ram_free[1] = memory_budget > memory_used ? memory_budget - memory_used : 0; return value; }
    bool send(const char *name,const uint8_t *bytes,unsigned count) override { auto *endpoint = midi_outputs().find(name); return endpoint && bridge.pedal(endpoint->route,bytes,count); }
    bool send_patch(unsigned owner,const char *name,const uint8_t *bytes,unsigned count) override { auto *endpoint = midi_outputs().find(name); return endpoint && bridge.owned_stream(owner,endpoint->route,bytes,count); }
    bool prepare_invocation(PatchExecutor &invocation) override {
        if (!bridge.retain_owner(invocation.owner())) return false;
        if (!retain_owner_state(&invocation,invocation.owner())) { bridge.release_owner(invocation.owner()); return false; }
        activate_owner_state(&invocation,invocation.owner()); return true;
    }
    void release_invocation(PatchExecutor &invocation) override { release_owner_state(&invocation); bridge.release_owner(invocation.owner()); }
    bool retain_owner(PatchExecutor &invocation) override {
        if (!bridge.retain_owner(invocation.owner())) return false;
        if (retain_owner_state(&invocation,invocation.owner())) return true;
        bridge.release_owner(invocation.owner()); return false;
    }
    void release_retained_owner(PatchExecutor &invocation) override { release_owner_state(&invocation); bridge.release_owner(invocation.owner()); }
    bool prepare_message(PatchExecutor &invocation,const char *name,const uint8_t *bytes,unsigned length) override {
        auto *output = midi_outputs().find(name); return output && reserve_owner_stream(&invocation,invocation.owner(),output->route,bytes,length);
    }
    bool prepare_owner_cycle(PatchExecutor &invocation) override { activate_owner_state(&invocation,invocation.owner()); return midi::Sink::prepare_owner_cycle(&invocation); }
    midi::Bridge::Delivery patch_delivery(unsigned owner) const override { return bridge.delivery(owner); }
    void cancel_patch(unsigned owner) override { bridge.cancel_owner(owner); }
    const midi::MidiState *midi_state() const override { return &midi::Sink::state(); }
    bool state(unsigned operation,uint32_t value) override { return app->state(operation,value,*this); }
    bool banked(const Document &document,unsigned node) override { return app->activate_banked(document,node); }
    bool preview_state(unsigned operation,uint32_t value) override { return app->controller().preview_state(operation,value); }
    bool selection(unsigned operation,const Document *&document,unsigned &node) override { return app->controller().selection(operation,document,node); }
    void selection_complete(unsigned operation) override { app->controller().selection_complete(operation); }
    void validation(bool begin) override { app->controller().validation(begin); }
    bool stock(uint8_t,PatchExecutor &) override { error("Stock firmware execution is not supported by the emulator"); return false; }
    void begin() override { sources->begin(); }
    void end() override { sources->end(); }
    bool acquire(uint16_t index,const Document *&document,uint32_t &revision) override { return sources->acquire(index,document,revision); }
    uint32_t names_signature() const override { return sources ? sources->names_signature() : 0; }
    midi::Bridge::Delivery bind_controls(const Document &d,unsigned node,PatchExecutor &invocation) override { return switches->bind_controls(d,node,invocation); }
    bool gate_midi_stream(const char *name,bool enabled) override { return midi_runtime.streams().gate(name,enabled); }
    bool select_midi_streams(const Document &d,unsigned node) override {
        unsigned scope; if (!d.stream_scope(node,scope)) return false;
        pickup(d.pickup(node),d.pickup_pairing_ms()); midi_runtime.streams().select(scope,time); return true;
    }
    void configure_midi_streams(const Document &d) override {
        pickup(d.pickup(),d.pickup_pairing_ms()); midi_runtime.streams().file_provider(files);
    }
    bool play_midi_file(unsigned owner,const midi::PlayMidiFilePatch &settings) override { return files->play(owner,settings); }
    midi::Bridge::Delivery midi_file_delivery(unsigned owner) const override { return files->player.delivery(owner); }
    void cancel_midi_file(unsigned owner) override { if (owner == files->player.owner()) files->player.cancel(); }
    bool record_midi_file(const midi::RecordMidiFilePatch &settings) override { return files->record(settings,0,false,microseconds()); }
    bool looper_action(unsigned action,const char *filename) override { return host->looper_action(action,filename); }
    bool activate(const SongSelection &s,const SongContinuation &c,bool resume) override { return song_sources->activate(s,c,resume); }
    SongExecution execution_status() const override { return song_sources->execution_status(); }
    bool capture_source(const Document *&d) override { return song_sources->capture_source(d); }
    bool revision(const SongSelection &s,uint32_t &crc) override { return song_sources->revision(s,crc); }
    bool capture(Song &s,bool reference) override { return song_sources->capture(s,reference); }
    void recalled() override { song_sources->recalled(); }
    bool write_packet(unsigned route,const midi::Packet &packet) override {
        if ((blocked & (1u << route)) || !(connected & (1u << route))) return false;
        std::vector<uint8_t> bytes{2}; integer(bytes,time,8); integer(bytes,route,1);
        bytes.insert(bytes.end(),packet.bytes,packet.bytes + packet.size()); frame(bytes); ++accepted_packets; return true;
    }
    void packet_sent(unsigned route,const midi::Packet &packet) override { midi_runtime.monitor().packet(midi::Monitor::Direction::outgoing,route,0,packet,microseconds()); }
    void midi_transport_poll() {}
    bool midi_host_connected() const { return true; }
    bool midi_device_connected() const { return true; }
    bool midi_endpoint_connected(unsigned route) const { return connected & (1u << route); }
    uint64_t midi_endpoint_generation(unsigned route) const { return generations[route]; }
    bool midi_document_valid() const { return documents->document().valid(); }
    auto &runtime_midi_files() { return *files; }
    auto &runtime_pages() { return host->pages_; }
    unsigned midi_source_count() const { return unsigned(incoming.size()); }
    void midi_switch_tick(uint32_t now) {
        switches->tick(now, uint32_t(app->held));
        if (switches->revision() == binding_revision) return;
        binding_revision = switches->revision();
        if (switches->performance()) {
            app->bindings_enter();
            ExpressionAssignment assignments[4];
            for (unsigned i = 0; i < 4; ++i) assignments[i] = switches->expression(i);
            expression->configure(assignments, midi_outputs());
        } else if (documents->document().valid()) expression->configure(documents->document().page_settings().expression, midi_outputs());
    }
    unsigned binding_revision = 0;
    void midi_flush() {}
    bool midi_read_event(unsigned source,midi::InputEvent &event) {
        if (incoming[source].empty()) return false;
        event = incoming[source].front(); incoming[source].pop_front(); return true;
    }
};

bool Board::supports_font(unsigned face,unsigned size) const { return fonts && fonts->supports(face,size); }

struct NullJob {
    bool output_pending() const { return false; }
    bool resetting() const { return false; }
    bool active() const { return false; }
    bool accepts_input() const { return true; }
    bool processing() const { return false; }
    unsigned input_size() const { return 0; }
    UsbKind input_kind() const { return UsbKind::command; }
    const uint8_t *input_bytes() const { return nullptr; }
    char input() const { return 0; }
    void receive(int,uint32_t) {}
    void receive_data(UsbKind,const uint8_t *,unsigned,uint32_t) {}
    void consumed(unsigned = 1) {}
    void reset_done() {}
    void cancel_transfer() {}
    void cancel() {}
    void keepalive(uint32_t) {}
    void poll(uint32_t) {}
    void poll(uint32_t,Replies &) {}
    void tick(uint32_t) {}
};

struct Actions {
    Board &board;
    bool switches_open() const { return board.switches->open(); }
    void switch_button(unsigned i,bool pressed,uint32_t now) { board.switches->button(i,pressed,now); }
    bool controls_locked() const { return board.app->controls_locked; }
    bool menu_open() const { return board.app->menu.open(); }
    bool songs_open() const { return board.app->song_ui.open(); }
    bool setup_open() const { return board.app->setups.open(); }
    void invoke(ui::Operation operation) { board.app->controller().executor().invoke(StatePatch{unsigned(operation)}); }
    void browse(bool reverse) { board.browse_page(reverse); }
    bool midi_reserved() const { return false; }
    bool keyboard(unsigned i,bool pressed) { return board.keyboard_button(i,pressed); }
    bool monitor(unsigned i,bool pressed) { return board.monitor_button(i,pressed); }
    bool looper(unsigned i,bool pressed) { return board.looper_button(i,pressed); }
    bool expression(unsigned i) { return board.expression_button(i); }
    bool port(unsigned i) { return board.trs_button(i); }
    bool clock(unsigned i,bool pressed) { return board.clock_control(i,pressed); }
    bool has_controller() const { return board.app->visible(); }
    void controller(unsigned i,bool pressed,uint32_t now) { board.app->controller().button(i,pressed,now); }
    void tap(uint32_t now) { board.tap_tempo(now); }
    bool leave_switches() { return true; }
    bool abandoned() const { return board.switches->abandoned(); }
    bool patch_pending() const { return board.app->patch_pending(); }
    void error(const char *text) { board.error(text); }
};

class Session {
public:
    Session(const char *directory,const char *setup,const ui::Fonts &fonts,bool replace_setup)
        : filesystem(std::filesystem::u8path(directory)), movie_reader(filesystem), movie_writer(filesystem), movie_storage(filesystem,movie_reader,movie_writer),
          movie_assets(movie_storage,board), movie_output(display), movie_player(movie_assets,movie_output), movie_session(movie_assets,movie_player),
          reader(filesystem), writer(filesystem), history(filesystem),
          card(filesystem,reader,writer,history), storage(card,board), sources(storage.setups()), font_reader(filesystem), font_writer(filesystem), font_storage(filesystem,font_reader,font_writer), primary(board,board,board,Switches::capacity),
          documents(storage,board,primary,board), song_writer(filesystem), song_storage(filesystem,song_writer,4,"/cntrl/songs",card.ready),
          songs(song_storage,board), application(board,documents,primary,board,board,songs,board,true),
          song_sources(board,board,board,storage.setups(),documents,application,songs),
          midi_source(filesystem), midi_index(filesystem), recording(filesystem), midi_directory(filesystem),
          midi_files(filesystem,midi_source,midi_index,recording,midi_directory,target::midi_connections.mask(true),"/midi","/index"),
          calibrations(filesystem), expression(target::port_count,calibrations,ExpressionPedals::calibration_filename,"Confirm",1,1),
          switches(board,board,board), work(target::maximum_display_area), cache(target::total_display_area), previous(target::total_display_area),
          renderer(display,fonts,work.data(),unsigned(work.size()),cache.data(),previous.data(),unsigned(cache.size())),
          host(board,application,documents,display,fonts,renderer,target::controls),
          input(buttons.data(),target::controls,documents.document(),application,board), action_router{board}, cooperative(target::scan_interval_ms) {
        board.app = &application; board.documents = &documents; board.host = &host;
        board.files = &midi_files; board.switches = &switches; board.song_sources = &song_sources; board.expression = &expression; board.font_storage = &font_storage;
        board.sources = &sources; card.ready = true;
        board.fonts = &fonts;
        board.animation = &movie_session; board.movie_player = &movie_player; movie_storage.ready = true;
        movie_assets.boot();
        font_storage.ready = true;
        host.looper_files(midi_files,board); host.arpeggiator(board.midi_runtime.arpeggiator);
        board.bridge.prepare(); board.midi_runtime.prepare(board.midi_outputs());
        if (!auxiliary.prepare(board)) throw std::runtime_error("auxiliary switch memory unavailable");
        filesystem.make_directory("/cntrl"); filesystem.make_directory("/midi"); filesystem.make_directory("/index");
        import(setup,replace_setup); import_movies(); expression.load_calibrations(); midi_files.boot();
        if (!storage.boot()) throw std::runtime_error("emulator setup bundle failed validation");
        documents.boot(); songs.boot();
        if (!documents.document().valid()) throw std::runtime_error("emulator setup failed validation");
        settle();
    }
    ~Session() {
        for (unsigned i = 0; i < application.layout.controllers(); ++i) application.at(i).executor().cancel();
        primary.executor().cancel(); song_sources.executor.cancel(); switches.close();
    }
    Board board;
    host::FileSystem filesystem;
    Display display;
    host::File movie_reader,movie_writer;
    AssetStorage movie_storage;
    animation::Receiver movie_assets;
    MovieDisplay movie_output;
    animation::Player movie_player;
    animation::Session movie_session;
    host::File reader,writer;
    host::Directory history;
    SetupFiles card;
    BundledStorage storage;
    StoredPatchSources sources;
    host::File font_reader,font_writer;
    font_assets::Storage font_storage;
    Controller primary;
    Documents documents;
    host::File song_writer;
    SlotStorage song_storage;
    Songs songs;
    ui::Application application;
    ui::SongSources song_sources;
    host::File midi_source,midi_index,recording;
    host::Directory midi_directory;
    midi::MidiFiles midi_files;
    host::File calibrations;
    ExpressionPedals expression;
    Switches switches;
    std::vector<uint16_t> work,cache,previous;
    ui::Renderer renderer;
    ui::PageHost<Board> host;
    std::array<ButtonState,target::button_count> buttons{};
    ui::InputCoordinator input;
    AuxiliarySwitches auxiliary;
    Actions action_router;
    CooperativeRuntime cooperative;
    NullJob unused;
    uint32_t now() const { return board.milliseconds(); }
    void complete_display() {}
    void keyboard() { board.poll_keyboard(); }
    bool scan_available() const { return true; }
    void scan(uint32_t now,uint32_t) {
        auxiliary.scan(board,documents.document(),now,[&](unsigned port) { return board.contacts[port]; },[&](unsigned index) { return debounce(index); });
        input.scan(board.held,now,*this,auxiliary.held());
        for (unsigned i = target::button_count; i < 32; ++i) {
            if (auxiliary.event(i) & press_event) switches.button(i,true,now);
            if (auxiliary.event(i) & release_event) switches.button(i,false,now);
        }
    }
    std::optional<unsigned> debounce(unsigned index) const { return switches.debounce_ms(index); }
    void keyboard_release(unsigned index) { board.keyboard_button(index,false); }
    bool panic_open() const { return false; }
    void panic_release(uint32_t) {}
    bool animation_controls(uint8_t pressed,uint32_t now,bool replay) { return movie_session.controls(pressed,now,replay); }
    bool diagnostic_controls(const uint8_t *,uint8_t,uint32_t) { return false; }
    ui::FirmwareVisit *visit() { return nullptr; }
    Actions &actions() { return action_router; }
    void pages(uint32_t now) { application.tick(now); documents.tick(now); }
    void midi() { board.midi_runtime.poll(board); board.relays.tick(now()); }
    void expression_poll(uint32_t now) {
        expression.poll(now,[&](unsigned i) { return board.expression_available(i); },
            [&](unsigned i,unsigned) { return board.readings[i]; },
            [&](const char *output) { return board.available(output); },
            [&](unsigned source,const char *output,const uint8_t *bytes) {
                auto *connection = board.midi_outputs().find(output);
                midi::Packet packet{11,{bytes[0],bytes[1],bytes[2]},uint16_t(0xff00 + source)};
                return connection && board.submit_live(connection->route,packet);
            });
    }
    void navigation() { input.finish(action_router); }
    void display_poll(uint32_t now) { board.display(now,documents.busy() || application.feature_open(board) || application.patch_pending()); }
    void settle(unsigned budget = 4096) {
        unsigned quiet = 0;
        // Zero-duration looping files may remain productive at one timestamp.
        // Yield the command after a bounded amount of cooperative work.
        for (unsigned iteration = 0; quiet < 16 && iteration < budget; ++iteration) {
            uint64_t accepted = board.accepted_packets;
            uint32_t preparation = board.midi_runtime.streams().diagnostics().preparation_visits;
            application.tick(board.milliseconds()); board.midi_runtime.poll(board);
            bool incoming = false;
            for (const auto &source : board.incoming) incoming = incoming || !source.empty();
            quiet = incoming || accepted != board.accepted_packets
                || preparation != board.midi_runtime.streams().diagnostics().preparation_visits ? 0 : quiet + 1;
        }
        display_poll(board.milliseconds());
        for (unsigned i = 0; i < 100000; ++i) {
            bool visible = movie_session.poll(board.milliseconds(),renderer.ready(),documents.busy() || application.feature_open(board) || application.patch_pending());
            if (visible ? movie_player.frame_ready() : renderer.ready()) break;
            display_poll(board.milliseconds());
        }
    }
    void advance(uint64_t time) {
        if (time < board.time) throw std::runtime_error("virtual time cannot move backward");
        while (board.time < time) { board.time += std::min(uint64_t(1000),time - board.time); tick(); }
        settle();
    }
    void tick();
    void cleanup() {
        board.held = 0; std::fill(std::begin(board.contacts),std::end(board.contacts),uint8_t(0));
        for (unsigned i = 0; i < 32; ++i) if (application.held & (uint64_t(1) << i)) switches.button(i,false,board.milliseconds());
        application.held = 0; std::fill(buttons.begin(),buttons.end(),ButtonState{});
        if (!auxiliary.prepare(board)) throw std::runtime_error("auxiliary switch memory unavailable");
        for (unsigned i = 0; i < application.layout.controllers(); ++i) {
            application.at(i).cancel_gesture(); application.at(i).executor().cancel();
        }
        board.midi_runtime.streams().stop_all(); midi_files.player.cancel(); midi_files.looper.stop();
        board.midi_runtime.arpeggiator.suspend(); host.pages_.generator.stop();
        const unsigned blocked = board.blocked;
        board.blocked = 0;
        for (unsigned i = 0; i < 64; ++i) board.midi_runtime.poll(board);
        // Snapshot accepted notes before releasing them: submit mutates the list.
        struct Release { unsigned output; midi::Packet packet; };
        std::vector<Release> releases;
        for (const auto &output : board.midi::Sink::state().outputs) for (const auto &channel : output.channels) {
            for (const auto &note : channel.notes) if (note.velocity) releases.push_back({output.output,{8,{uint8_t(0x80 | channel.channel),note.note,0}}});
            for (const auto &control : channel.controllers) if (control.controller >= 64 && control.controller <= 69 && control.value >= 64)
                releases.push_back({output.output,{11,{uint8_t(0xb0 | channel.channel),control.controller,0}}});
        }
        for (const auto &release : releases) board.submit(release.output,release.packet);
        if (!board.relays.reset()) board.error("Relay cleanup failed");
        board.blocked = blocked;
    }
    void command(const std::vector<uint8_t> &bytes);
private:
    void import_movies() {
        auto directory = filesystem.path("/cntrl/movies");
        if (!std::filesystem::is_directory(directory)) return;
        for (const auto &entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".cnsa") continue;
            std::ifstream stream(entry.path(),std::ios::binary);
            uint64_t size = entry.file_size();
            if (!stream || size > UINT32_MAX) throw std::runtime_error("cannot read compiled movie asset");
            uint8_t block[usb_block_size]; uint32_t crc = 0xffffffff; uint64_t read = 0;
            while (stream.read(reinterpret_cast<char *>(block),sizeof(block)) || stream.gcount()) {
                crc = animation::crc_update(crc,block,unsigned(stream.gcount())); read += uint64_t(stream.gcount());
            }
            if (!stream.eof() || read != size) throw std::runtime_error("compiled movie asset read failed");
            crc = ~crc;
            if (movie_assets.choose(crc)) continue;
            char command[96]; std::snprintf(command,sizeof(command),"CNTRL SPLASH BEGIN %lu %08lx\n",(unsigned long)size,(unsigned long)crc);
            for (const char *c = command; *c; ++c) movie_assets.receive(uint8_t(*c),0);
            stream.clear(); stream.seekg(0); read = 0;
            while (stream.read(reinterpret_cast<char *>(block),sizeof(block)) || stream.gcount()) {
                movie_assets.receive_data(block,unsigned(stream.gcount()),0); read += uint64_t(stream.gcount());
            }
            if (!stream.eof() || read != size) throw std::runtime_error("compiled movie asset read failed");
            while (movie_assets.busy()) movie_assets.poll(0);
            if (!movie_assets.choose(crc)) throw std::runtime_error("compiled movie asset failed validation");
        }
    }
    void import(const char *setup,bool replace) {
        if (!replace && (filesystem.exists("/cntrl/setups/.bundle/0.bin") || filesystem.exists("/cntrl/setups/.bundle/1.bin"))) return;
        std::ifstream stream(std::filesystem::u8path(setup),std::ios::binary);
        if (!stream) throw std::runtime_error("cannot read compiled emulator setup");
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(stream),{}};
        if (bytes.size() < 40 || std::memcmp(bytes.data(),"CSTB",4)) throw std::runtime_error("emulator requires a compiled setup bundle");
        if (!storage.boot() || !storage.upload_begin(unsigned(bytes.size()),checksum(bytes.data(),unsigned(bytes.size())),0)
            || !storage.upload_write(bytes.data(),unsigned(bytes.size())) || !storage.upload_finish())
            throw std::runtime_error("cannot initialize emulator session storage");
    }
};

// Adapt names where an object and a runtime service share an identifier.
struct RuntimeServices {
    Session &s;
    uint32_t now() const { return s.now(); }
    void complete_display() { s.complete_display(); }
    void keyboard() { s.keyboard(); }
    bool scan_available() const { return true; }
    void scan(uint32_t now,uint32_t gap) { s.scan(now,gap); }
    void pages(uint32_t now) { s.pages(now); }
    void midi() { s.midi(); }
    NullJob &session() { return s.unused; }
    NullJob &commands() { return s.unused; }
    NullJob &song_transfer() { return s.unused; }
    NullJob &bundle_transfer() { return s.unused; }
    NullJob &disk_transfer() { return s.unused; }
    NullJob &midi_transfer() { return s.unused; }
    NullJob &history_transfer() { return s.unused; }
    NullJob &fonts() { return s.unused; }
    ui::SongSources &song_sources() { return s.song_sources; }
    Songs &songs() { return s.songs; }
    Replies &replies() { return s.board; }
    bool serial_available() const { return false; }
    int serial_read() const { return 0; }
    bool reply_ready() const { return true; }
    bool commands_allowed() const { return true; }
    void cancel_listing() {}
    bool expression_allowed() const { return true; }
    void expression(uint32_t now) { s.expression_poll(now); }
    void navigation() { s.navigation(); }
    void display(uint32_t now) { s.display_poll(now); }
};
void Session::tick() { RuntimeServices services{*this}; cooperative.poll(services); }
void Session::command(const std::vector<uint8_t> &bytes) {
    const auto *p = bytes.data(); unsigned length = unsigned(bytes.size());
    if (p[0] == 1 && length == 9) advance(u64(p + 1));
    else if (p[0] == 2 && length == 3 && p[1] < target::button_count && p[2] <= 1) {
        board.held = uint8_t((board.held & ~(1u << p[1])) | (unsigned(p[2]) << p[1]));
        tick(); settle();
    } else if (p[0] == 3 && length >= 3 && p[1] < target::endpoints.size() && target::midi_connections.route(p[1])->input && board.midi_endpoint_connected(p[1])) {
        if (board.incoming[p[1]].size() + length - 2 > 65536) throw std::runtime_error("emulated MIDI input queue is full");
        for (unsigned i = 2; i < length; ++i) { midi::InputEvent event; event.input = p[1]; event.byte = p[i]; event.clock_source = p[1]; board.incoming[p[1]].push_back(event); }
        // A one-byte real-time message can require its own queued output poll.
        settle(4096 + 2 * (length - 2));
    } else if (p[0] == 4 && length == 3 && p[1] < target::endpoints.size() && p[2] <= 1) {
        board.blocked = (board.blocked & ~(1u << p[1])) | (unsigned(p[2]) << p[1]); settle();
    } else if (p[0] == 5 && length == 4 && p[1] < target::port_count && u16(p + 2) <= 1023) {
        board.readings[p[1]] = u16(p + 2); tick(); settle();
    } else if (p[0] == 6 && length >= 3 && p[1] >= 1 && p[1] <= 2) {
        std::string text(bytes.begin() + 2,bytes.end());
        if (!board.text_messages().publish(TextMessages::Kind(p[1]),text.c_str(),unsigned(text.size()),board.milliseconds())) throw std::runtime_error("invalid text message");
        settle();
    } else if (p[0] == 7 && length == 5) {
        std::srand(u32(p + 1));
        if (const auto *settings = documents.document().arpeggiator_settings()) {
            std::array<uint8_t,50> configuration{}; std::copy(settings,settings + configuration.size(),configuration.begin());
            put32(configuration.data() + 46,u32(p + 1)); board.midi_runtime.arpeggiator.configure(configuration.data()); settle();
        }
    }
    else if (p[0] == 8 && length == 2 && p[1] <= 1) { board.browse_page(p[1]); settle(); }
    else if (p[0] == 9 && length == 1) cleanup();
    else if (p[0] == 10 && length == 3 && p[1] < target::endpoints.size() && p[2] <= 1) {
        const unsigned mask = 1u << p[1];
        if (bool(board.connected & mask) != bool(p[2])) ++board.generations[p[1]];
        if (!p[2]) board.incoming[p[1]].clear();
        board.connected = (board.connected & ~mask) | (unsigned(p[2]) << p[1]); settle();
    }
    else if (p[0] == 11 && length == 3 && p[1] < target::port_count && p[2] <= 3) {
        if (!board.supports_trs_mode(p[1],midi::TrsMode::aux)) throw std::runtime_error("port does not support auxiliary contacts");
        board.contacts[p[1]] = p[2]; tick(); settle();
    }
    else if (p[0] == 12 && length == 1) {
        std::vector<uint8_t> result{5};
        integer(result,memory_used,4); integer(result,memory_peak,4); integer(result,memory_failures,4); frame(result);
    }
    else throw std::runtime_error("unsupported or malformed emulator command");
}
} // namespace cntrl::emulator

int main(int argc,char **argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY); _setmode(_fileno(stdout),_O_BINARY);
#endif
    std::ios::sync_with_stdio(false);
    try {
        if (argc < 6 || (argc - 4) % 2 || (std::strcmp(argv[3],"0") && std::strcmp(argv[3],"1")))
            throw std::runtime_error("usage: emulator SESSION_DIR COMPILED_SETUP REPLACE_SETUP FONT_ID FONT_FILE [...]");
        cntrl::memory_budget = 32u * 1024 * 1024;
        cntrl::emulator::FontFiles fonts(argc - 4,argv + 4);
        cntrl::emulator::Session session(argv[1],argv[2],*fonts.fonts,argv[3][0] == '1'); cntrl::emulator::ack();
        std::vector<uint8_t> bytes;
        while (cntrl::emulator::request(bytes)) {
            try { session.command(bytes); }
            catch (const std::runtime_error &error) { cntrl::emulator::error(error.what()); }
            cntrl::emulator::ack();
        }
        return 0;
    } catch (const std::runtime_error &error) { cntrl::emulator::error(error.what()); cntrl::emulator::ack(); return 1; }
}
