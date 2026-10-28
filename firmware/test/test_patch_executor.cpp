#include "patch_hardware.h"
#include <cntrl/switches.h>
#include <cassert>
#include <fstream>
#include <string>
#include <vector>

using Bytes = std::vector<uint8_t>;

struct PatchBoard : TestPatchHardware, cntrl::midi::Sink {
    cntrl::midi::Bridge bridge;
    const cntrl::Document *foreign = nullptr;
    bool online = true, writable = true;
    std::string unavailable_output;
    unsigned pins = 0, visit_completed = 0;
    uint32_t visit_fingerprint = 0;
    uint8_t visit_buttons = 0;
    Bytes wire[2];
    std::vector<unsigned> operations;
    std::string last_error;
    cntrl::Controller::NavigationState initial_navigation, stock_navigation;
    bool supports(const char *) const override { return true; }
    bool supports_function(unsigned code) const override { return code == 1; }
    unsigned switch_capacity() const override { return 5; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    bool send(const char *, const uint8_t *, unsigned) override { return false; }
    bool available(const char *output) const override { return online && unavailable_output != output; }
    bool send_patch(unsigned owner, const char *output, const uint8_t *bytes, unsigned length) override {
        return bridge.owned_stream(owner, !std::strcmp(output, "trs") ? 1 : 0, bytes, length);
    }
    cntrl::midi::Bridge::Delivery patch_delivery(unsigned owner) const override { return bridge.delivery(owner); }
    void cancel_patch(unsigned owner) override { bridge.cancel_owner(owner); }
    bool submit(unsigned route, const cntrl::midi::Packet &packet) override {
        if (!writable) return false;
        wire[route].insert(wire[route].end(), packet.bytes, packet.bytes + packet.size()); return true;
    }
    void begin() override { ++pins; }
    void end() override { assert(pins); --pins; }
    bool acquire(const char *name, const cntrl::Document *&document, uint32_t &revision) override {
        if (std::strcmp(name, "foreign") || !foreign) return false;
        document = foreign; revision = cntrl::u32(document->data() + 20); return true;
    }
    bool state(unsigned operation, uint32_t) override {
        operations.push_back(operation);
        return controller && operation >= 3 && operation <= 8 ? controller->navigate(operation) : true;
    }
    void validation_context(cntrl::PatchExecutor &executor, bool enabled) override {
        if (!controller) return;
        if (enabled && !executor.reconstructing()) initial_navigation = controller->navigation();
        controller->validation(enabled);
    }
    bool preview_state(unsigned operation, uint32_t value) override { return !controller || controller->preview_state(operation, value); }
    bool selection(unsigned operation, const cntrl::Document *&document, unsigned &root) override {
        return controller && controller->selection(operation, document, root);
    }
    void selection_complete(unsigned operation) override { if (controller) controller->selection_complete(operation); }
    bool stock(uint8_t buttons, cntrl::PatchExecutor &executor) override {
        visit_buttons = buttons; visit_completed = executor.completed(); visit_fingerprint = executor.fingerprint(); return true;
    }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *message) override { last_error = message; }
};

void word(Bytes &bytes, unsigned value) { bytes.push_back(uint8_t(value)); bytes.push_back(uint8_t(value >> 8)); }
void number(Bytes &bytes, unsigned value) { for (unsigned i = 0; i < 4; ++i) bytes.push_back(uint8_t(value >> (i * 8))); }
Bytes node(unsigned kind, const Bytes &payload, unsigned flags = 0) {
    Bytes bytes{uint8_t(kind), uint8_t(flags)}; word(bytes, unsigned(payload.size()));
    bytes.insert(bytes.end(), payload.begin(), payload.end()); return bytes;
}
Bytes midi(const char *output, Bytes message, unsigned flags = 0) {
    Bytes payload(32); std::memcpy(payload.data(), output, std::strlen(output));
    payload.insert(payload.end(), message.begin(), message.end()); return node(2, payload, flags);
}
Bytes sequence(std::initializer_list<unsigned> children) {
    Bytes payload; word(payload, unsigned(children.size())); for (unsigned child : children) word(payload, child);
    return node(5, payload);
}
Bytes reference(const char *document, const char *key) {
    Bytes payload(128); std::memcpy(payload.data(), document, std::strlen(document));
    std::memcpy(payload.data() + 64, key, std::strlen(key)); return node(6, payload);
}
Bytes extend(const Bytes &base, const std::vector<Bytes> &extra, PatchBoard &board) {
    cntrl::Document document; assert(document.load(base.data(), unsigned(base.size()), board));
    unsigned graph = document.graph_metadata(), count = document.nodes(), total = count + unsigned(extra.size());
    unsigned table = graph + 16 + count * 4, first = unsigned(document.node(0) - base.data());
    Bytes section(base.begin() + graph, base.begin() + graph + 16);
    section[8] = uint8_t(total); section[9] = uint8_t(total >> 8);
    unsigned offset = 16 + total * 4 + first - table;
    for (unsigned i = 0; i < total; ++i) {
        number(section, offset);
        offset += i < count ? 4 + cntrl::u16(document.node(i) + 2) : unsigned(extra[i - count].size());
    }
    section.insert(section.end(), base.begin() + table, base.begin() + first);
    for (unsigned i = 0; i < count; ++i) section.insert(section.end(), document.node(i), document.node(i) + 4 + cntrl::u16(document.node(i) + 2));
    for (const Bytes &n : extra) section.insert(section.end(), n.begin(), n.end());
    cntrl::put32(section.data() + 4, unsigned(section.size()));
    Bytes result(base.begin(), base.begin() + graph);
    result.insert(result.end(), section.begin(), section.end());
    result.insert(result.end(), base.begin() + document.display_metadata(), base.end());
    cntrl::put32(result.data() + 8, unsigned(result.size()));
    cntrl::put32(result.data() + 20, cntrl::checksum(result.data() + 28, unsigned(result.size()) - 28));
    return result;
}
void drain(cntrl::PatchExecutor &executor, PatchBoard &board) {
    for (unsigned i = 0; i < 1000 && executor.status() == cntrl::PatchExecutor::Status::pending; ++i) {
        executor.tick(); board.bridge.service(board, i);
    }
}

int main(int argc, char **argv) {
    assert(argc == 2); std::ifstream file(argv[1]); std::string hex; file >> hex;
    Bytes base; for (unsigned i = 0; i < hex.size(); i += 2) base.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
    PatchBoard board; cntrl::Document original; assert(original.load(base.data(), unsigned(base.size()), board));
    unsigned first = original.nodes();
    Bytes sysex(130, 3); sysex.front() = 0xf0; sysex.back() = 0xf7;
    Bytes state{1}; number(state, 192);
    Bytes image = extend(base, {
        midi("din", {0xb0, 64, 127}), midi("trs", {0xe2, 0, 96}, 2), midi("din", sysex, 9),
        midi("din", {0xb0, 99, 1, 0xb0, 98, 2, 0xb0, 6, 3, 0xb0, 38, 4}, 4),
        node(3, state), node(4, {33}), midi("trs", {0xfa}, 1),
        sequence({first + 3, first + 5, first + 6}), sequence({first, first + 1, first + 2, first + 4, first + 7})
    }, board);
    cntrl::Document document; assert(document.load(image.data(), unsigned(image.size()), board));
    cntrl::PatchExecutor executor(board, board, board, 5);
    assert(executor.start(document, first + 8)); assert(board.wire[0].empty());
    board.writable = false; executor.tick(); executor.tick(); assert(executor.status() == cntrl::PatchExecutor::Status::pending);
    assert(board.operations.empty()); board.writable = true; drain(executor, board);
    assert(executor.status() == cntrl::PatchExecutor::Status::suspended && board.pins == 1);
    assert(board.visit_buttons == 33 && board.visit_completed == 6);
    assert((board.wire[1] == Bytes{0xe2, 0, 96}));
    assert(board.wire[0].size() == 3 + sysex.size() + 12 && board.operations.size() == 1);
    unsigned completed = executor.completed(); uint32_t fingerprint = executor.fingerprint(); executor.cancel(); assert(board.pins == 0);
    assert(executor.resume(document, first + 8, completed, fingerprint)); drain(executor, board);
    assert(executor.status() == cntrl::PatchExecutor::Status::completed && board.pins == 0);
    assert(!executor.document().valid());
    assert((board.wire[1] == Bytes{0xe2, 0, 96, 0xfa}));
    assert(board.operations.size() == 1); // No state or MIDI replay after return.
    assert(!executor.resume(document, first + 8, completed, fingerprint ^ 1));
    assert(executor.status() == cntrl::PatchExecutor::Status::failed && board.pins == 0);
    Bytes changed_header = image; changed_header[18] ^= 1;
    cntrl::Document changed_root; assert(changed_root.load(changed_header.data(), unsigned(changed_header.size()), board));
    assert(!executor.resume(changed_root, first + 8, completed, fingerprint));
    assert(executor.status() == cntrl::PatchExecutor::Status::failed && board.pins == 0);
    assert(!executor.start(document, first + 8, true)); // Stock visit forbidden in persistent switch, even nested.
    unsigned effects_before = unsigned(board.wire[0].size()), state_before = unsigned(board.operations.size());
    assert(executor.start(document, first + 8)); executor.tick(); board.bridge.service(board, 0);
    board.unavailable_output = "trs"; executor.tick();
    assert(executor.status() == cntrl::PatchExecutor::Status::failed && board.pins == 0);
    assert(board.wire[0].size() == effects_before + 3 && board.operations.size() == state_before);
    board.unavailable_output.clear(); // Earlier sends remain; later children were not dispatched.
    Bytes malformed = image;
    malformed[document.node(first) - image.data() + 1] = 2; // CC bytes cannot claim pitch-bend ownership.
    cntrl::put32(malformed.data() + 20, cntrl::checksum(malformed.data() + 28, unsigned(malformed.size()) - 28));
    cntrl::Document rejected; assert(!rejected.load(malformed.data(), unsigned(malformed.size()), board));
    malformed = image;
    unsigned browser_root = document.graph_metadata() + 16 + document.nodes() * 4;
    malformed[browser_root] = uint8_t(first + 8); malformed[browser_root + 1] = uint8_t((first + 8) >> 8);
    cntrl::put32(malformed.data() + 20, cntrl::checksum(malformed.data() + 28, unsigned(malformed.size()) - 28));
    assert(!rejected.load(malformed.data(), unsigned(malformed.size()), board)); // Composite cannot masquerade as undoable program.
    Bytes cyclic = extend(base, {sequence({first})}, board); cntrl::Document cycle;
    assert(cycle.load(cyclic.data(), unsigned(cyclic.size()), board));
    assert(!executor.start(cycle, first) && board.last_error == "Patch reference cycle");
    Bytes missing = extend(base, {sequence({first + 1, first + 2}), midi("din", {0xb0, 1, 1}), reference("foreign", "Flute")}, board);
    cntrl::Document dependency; assert(dependency.load(missing.data(), unsigned(missing.size()), board));
    unsigned before = unsigned(board.wire[0].size());
    assert(!executor.start(dependency, first)); assert(board.wire[0].size() == before && board.pins == 0);
    board.foreign = &original;
    assert(executor.start(dependency, first)); drain(executor, board);
    assert(executor.status() == cntrl::PatchExecutor::Status::completed);
    Bytes foreign_cycle = extend(base, {reference("foreign", "Flute")}, board);
    cntrl::Document foreign_view; assert(foreign_view.load(foreign_cycle.data(), unsigned(foreign_cycle.size()), board));
    unsigned identity = unsigned(foreign_view.named_patch(0u) - foreign_cycle.data()) + 64;
    foreign_cycle[identity] = uint8_t(first); foreign_cycle[identity + 1] = uint8_t(first >> 8);
    cntrl::put32(foreign_cycle.data() + 20, cntrl::checksum(foreign_cycle.data() + 28, unsigned(foreign_cycle.size()) - 28));
    assert(foreign_view.load(foreign_cycle.data(), unsigned(foreign_cycle.size()), board)); board.foreign = &foreign_view;
    before = unsigned(board.wire[0].size());
    assert(!executor.start(dependency, first) && board.last_error == "Patch reference cycle");
    assert(board.wire[0].size() == before && board.pins == 0);
    board.foreign = &original;
    assert(executor.start(document, first)); executor.tick(); board.online = false; executor.tick();
    assert(executor.status() == cntrl::PatchExecutor::Status::unknown && board.pins == 0);
    board.online = true;
    // Navigation before a stock visit selects the same graph during resume
    // validation, without replaying that navigation on the actual UI.
    Bytes next{3}; number(next, 0); Bytes previous{4}; number(previous, 0); Bytes activate{7}; number(activate, 0);
    Bytes navigable = extend(base, {node(3, next), node(3, activate), node(4, {33}), node(3, previous),
        sequence({first, first + 1, first + 2, first + 3, first + 1})}, board);
    cntrl::Document navigation_document; assert(navigation_document.load(navigable.data(), unsigned(navigable.size()), board));
    cntrl::Controller controller(board, board, board); board.controller = &controller; controller.activate(navigation_document);
    unsigned initial_bytes = unsigned(board.wire[0].size());
    assert(executor.start(navigation_document, first + 4)); drain(executor, board);
    assert(executor.status() == cntrl::PatchExecutor::Status::suspended && executor.completed() == 3);
    auto initial = board.initial_navigation, at_stock = controller.navigation();
    assert(initial.cursors[0] == 0 && at_stock.cursors[0] == 1);
    completed = executor.completed(); fingerprint = executor.fingerprint(); executor.cancel();
    assert(controller.navigation(initial));
    assert(executor.resume(navigation_document, first + 4, completed, fingerprint));
    assert(controller.selected_patch() == 0); // Reconstruction touched preview state only.
    assert(controller.navigation(at_stock)); drain(executor, board);
    assert(executor.status() == cntrl::PatchExecutor::Status::completed && controller.selected_patch() == 0);
    Bytes selections(board.wire[0].begin() + initial_bytes, board.wire[0].end());
    assert((selections == Bytes{0xc0, 1, 0xb0, 0, 1, 0xb0, 32, 2, 0xc0, 0, 0xb0, 7, 100, 0xb0, 10, 0}));
    // A concurrent navigation change cannot replace an already validated
    // activate-selection dependency while earlier MIDI is pending.
    Bytes frozen = extend(base, {midi("din", {0xb0, 1, 2}), node(3, activate), sequence({first, first + 1})}, board);
    cntrl::Document frozen_document; assert(frozen_document.load(frozen.data(), unsigned(frozen.size()), board));
    controller.activate(frozen_document); initial_bytes = unsigned(board.wire[0].size());
    assert(executor.start(frozen_document, first + 2)); executor.tick();
    assert(controller.navigate(3) && controller.selected_patch() == 1); drain(executor, board);
    selections.assign(board.wire[0].begin() + initial_bytes, board.wire[0].end());
    assert((selections == Bytes{0xb0, 1, 2, 0xb0, 0, 1, 0xb0, 32, 2, 0xc0, 0, 0xb0, 7, 100, 0xb0, 10, 0}));
    controller.activate(original); controller.action(2);
    assert(controller.busy()); controller.action(1); assert(controller.selected_patch() == 0);
    for (unsigned i = 0; i < 20; ++i) { controller.tick(i); board.bridge.service(board, i); }
    assert(controller.depth() == 1); controller.action(1); controller.action(2);
    controller.tick(20); board.bridge.service(board, 20); board.online = false; controller.tick(21);
    assert(!controller.busy() && controller.depth() == 0); board.online = true;
    controller.action(0); initial_bytes = unsigned(board.wire[0].size()); controller.action(2);
    for (unsigned i = 22; i < 42; ++i) { controller.tick(i); board.bridge.service(board, i); }
    assert(board.wire[0].size() == initial_bytes + 14); // Unknown remote state permits resending the previously known program.
    // A dynamic selection changed since entering the switch page must still
    // respect another persistent assignment's reserved controls.
    Bytes switched = extend(base, {node(3, activate)}, board); cntrl::Document switched_document;
    assert(switched_document.load(switched.data(), unsigned(switched.size()), board));
    unsigned assignment = unsigned(switched_document.switch_assignment(0) - switched.data());
    switched[assignment + 2] = uint8_t(first); switched[assignment + 3] = uint8_t(first >> 8);
    cntrl::put32(switched.data() + 20, cntrl::checksum(switched.data() + 28, unsigned(switched.size()) - 28));
    assert(switched_document.load(switched.data(), unsigned(switched.size()), board));
    controller.activate(switched_document); assert(controller.navigate(3));
    cntrl::Switches switches(board, board, board); switches.enter(switched_document, 0); assert(switches.open());
    assert(controller.navigate(4)); initial_bytes = unsigned(board.wire[0].size());
    switches.button(0, true); switches.tick(); board.bridge.service(board, 0);
    assert(board.last_error == "Selected patch overlaps another switch's MIDI controls");
    assert(board.wire[0].size() == initial_bytes);
    switches.close(); switches.tick(); assert(!switches.open() && board.pins == 0);
}
