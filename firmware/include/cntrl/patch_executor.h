#pragma once
#include "document.h"
#include "midi_file.h"
#include <cstdlib>

namespace cntrl {
class PatchExecutor;
struct StatePatch { unsigned operation; uint32_t value = 0; };
struct StockFirmwarePatch { uint8_t exit_buttons; };

struct PatchSources {
    virtual ~PatchSources() = default;
    virtual void begin() {}
    virtual void end() {}
    virtual bool acquire(const char *name, const Document *&document, uint32_t &revision) = 0;
};

struct PatchOutputs {
    virtual ~PatchOutputs() = default;
    virtual bool available(const char *output) const = 0;
    // Borrowed bytes remain pinned until delivery completes or cancellation.
    virtual bool send_patch(unsigned owner, const char *output, const uint8_t *bytes, unsigned length) = 0;
    virtual midi::Bridge::Delivery patch_delivery(unsigned owner) const = 0;
    virtual void cancel_patch(unsigned owner) = 0;
    virtual const midi::MidiState *midi_state() const { return nullptr; }
    virtual bool state(unsigned operation, uint32_t value) = 0;
    virtual bool play_midi_file(unsigned, const midi::PlayMidiFilePatch &) { return false; }
    virtual midi::Bridge::Delivery midi_file_delivery(unsigned) const { return midi::Bridge::Delivery::cancelled; }
    virtual void cancel_midi_file(unsigned) {}
    virtual bool record_midi_file(const midi::RecordMidiFilePatch &) { return false; }
    virtual void validation(bool) {}
    virtual void validation_context(PatchExecutor &, bool enabled) { validation(enabled); }
    virtual bool preview_state(unsigned, uint32_t) { return true; }
    virtual bool selection(unsigned, const Document *&, unsigned &) { return false; }
    virtual void selection_complete(unsigned) {}
    virtual midi::Bridge::Delivery prepare_leave(PatchExecutor &) { return midi::Bridge::Delivery::complete; }
    virtual bool stock(uint8_t buttons, PatchExecutor &invocation) = 0;
};

class PatchExecutor {
public:
    struct Resource {
        char output[32]; unsigned channel, control;
        int value = -1;
        // Channel/control 256 reserves the entire destination output.
        bool overlaps(const Resource &other) const {
            return !std::strcmp(output, other.output) && (channel == 256 || other.channel == 256
                || (channel == other.channel && control == other.control));
        }
    };
    enum class Status { idle, pending, completed, failed, unknown, suspended };
    PatchExecutor(Hardware &hardware, PatchOutputs &outputs, PatchSources &sources, unsigned owner)
        : hardware_(hardware), outputs_(outputs), sources_(sources), owner_(owner) {}
    ~PatchExecutor() { cancel(); std::free(frames_); std::free(resources_); std::free(bindings_); }
    PatchExecutor(const PatchExecutor &) = delete;
    PatchExecutor &operator=(const PatchExecutor &) = delete;

    Status status() const { return status_; }
    bool busy() const { return status_ == Status::pending || status_ == Status::suspended; }
    unsigned root() const { return root_; }
    unsigned owner() const { return owner_; }
    unsigned completed() const { return completed_; }
    uint32_t fingerprint() const { return fingerprint_; }
    const Document &document() const { return document_; }
    const char *error() const { return error_; }
    bool reconstructing() const { return reconstructing_; }
    bool started() const { return started_; }
    unsigned resource_count() const { return resource_count_; }
    const Resource *resources() const { return resources_; }
    bool state_complete() const { return state_complete_; }
    bool invoke(const StatePatch &patch) {
        return outputs_.state(patch.operation, patch.value);
    }
    bool invoke(const StockFirmwarePatch &patch) {
        if (busy() || !button_pair_valid(patch.exit_buttons)) return false;
        reset(); document_ = Document(); builtin_stock_ = patch.exit_buttons;
        status_ = Status::pending; return true;
    }

    bool start(const Document &document, unsigned node, bool persistent = false) {
        return resume(document, node, 0, 0, persistent);
    }
    // Reconstruct the traversal without replaying completed effects. The caller
    // consumes its durable continuation before this method dispatches anything.
    bool resume(const Document &document, unsigned node, unsigned completed, uint32_t fingerprint,
                bool persistent = false) {
        if (busy()) return false;
        Document source = document;
        reset(); document_ = source; root_ = node; persistent_ = persistent;
        sources_.begin(); pinned_ = true;
        outputs_.validation_context(*this, true); validating_ = true;
        fingerprint_ = 2166136261u;
        if (!document_.valid()) return fail("Invalid patch root");
        hash(checksum(document_.data(), document_.length()));
        if (!push(&document_, node)) return false;
        unsigned leaves = 0;
        while (depth_) {
            const uint8_t *n = next_leaf(true);
            if (!n) { if (status_ == Status::failed) return false; break; }
            if (persistent && (n[0] == 4 || (n[0] == 3 && (n[4] == 9 || n[4] == 10))))
                return fail("Persistent switches cannot leave their context");
            if (!collect(n, *frames_[depth_ - 1].document)) return false;
            ++leaves; --depth_;
        }
        if (completed > leaves || (fingerprint && fingerprint != fingerprint_))
            return fail("Patch sources changed since firmware visit");
        outputs_.validation_context(*this, false); validating_ = false;
        if (!push(&document_, root_)) return false;
        binding_cursor_ = 0;
        completed_ = 0;
        uint32_t expected_fingerprint = fingerprint_;
        reconstructing_ = completed != 0;
        if (reconstructing_) { outputs_.validation_context(*this, true); validating_ = true; }
        while (completed_ < completed) {
            if (!next_leaf(true)) return fail("Invalid patch continuation");
            --depth_; ++completed_;
        }
        if (validating_) { outputs_.validation_context(*this, false); validating_ = false; }
        reconstructing_ = false; fingerprint_ = expected_fingerprint;
        status_ = Status::pending;
        return true;
    }
    bool delaying() const { return delaying_; }
    void tick(uint32_t now = 0) {
        if (status_ != Status::pending) return;
        if (builtin_stock_) { dispatch_stock(builtin_stock_); return; }
        if (file_waiting_) {
            auto delivery = outputs_.midi_file_delivery(owner_);
            if (delivery == midi::Bridge::Delivery::pending) return;
            file_waiting_ = false;
            if (delivery != midi::Bridge::Delivery::complete) { fail("MIDI file playback failed"); return; }
            complete_leaf();
        }
        if (waiting_) {
            if (!outputs_.available(waiting_output_)) {
                outputs_.cancel_patch(owner_); waiting_ = false; finish(Status::unknown); return;
            }
            auto delivered = outputs_.patch_delivery(owner_);
            if (delivered == midi::Bridge::Delivery::cancelled) { finish(Status::unknown); return; }
            if (delivered != midi::Bridge::Delivery::complete) return;
            waiting_ = false; complete_leaf();
        }
        if (delaying_) {
            if (uint32_t(now - delay_started_) < delay_duration_) return;
            delaying_ = false; complete_leaf();
        }
        const uint8_t *n = next_leaf(false);
        if (!n) {
            if (status_ != Status::failed) finish(Status::completed);
            return;
        }
        const uint8_t *p = n + 4;
        const char *output = nullptr;
        const uint8_t *bytes = nullptr; unsigned length = 0;
        switch (n[0]) {
        case 1: {
            const uint8_t *instrument = frames_[depth_ - 1].document->instrument(u16(p));
            output = reinterpret_cast<const char *>(instrument + 32);
            auto cc = [&](uint8_t controller, uint8_t value) {
                if (value != 255) { program_[length++] = 0xb0 | instrument[64]; program_[length++] = controller; program_[length++] = value; }
            };
            cc(0, p[3]); cc(32, p[4]);
            program_[length++] = 0xc0 | instrument[64]; program_[length++] = p[2];
            cc(7, p[5]); cc(10, p[6]); bytes = program_;
            break;
        }
        case 2: output = reinterpret_cast<const char *>(p); bytes = p + 32; length = u16(n + 2) - 32; break;
        case 3:
            if ((p[0] == 9 || p[0] == 10) && !leaving()) return;
            if (!invoke(StatePatch{p[0], u32(p + 1)})) { fail("Application operation failed"); return; }
            started_ = true;
            complete_leaf(); return;
        case 8: {
            midi::PlayMidiFilePatch settings;
            std::memcpy(settings.filename, p, 96); settings.outputs = u16(p + 96);
            settings.loops = u32(p + 98); settings.pre_delay = u32(p + 102); settings.post_delay = u32(p + 106);
            std::memcpy(&settings.speed, p + 110, 8); settings.reverse = p[118]; settings.transpose = int8_t(p[119]);
            if (!outputs_.play_midi_file(owner_, settings)) { fail("Cannot open MIDI file"); return; }
            file_waiting_ = started_ = true; return;
        }
        case 9:
            if (!outputs_.record_midi_file({u16(p), u16(p + 2)})) { fail("MIDI recording failed"); return; }
            started_ = true; complete_leaf(); return;
        case 7:
            delay_started_ = now; delay_duration_ = u32(p);
            delaying_ = started_ = true;
            return;
        case 4:
            dispatch_stock(p[0]);
            return;
        default: fail("Invalid executable patch"); return;
        }
        if (!outputs_.available(output)) { fail("MIDI output unavailable"); return; }
        if (outputs_.send_patch(owner_, output, bytes, length)) { waiting_ = started_ = true; waiting_output_ = output; }
    }
    void cancel() {
        if (file_waiting_) outputs_.cancel_midi_file(owner_);
        file_waiting_ = false;
        if (waiting_) outputs_.cancel_patch(owner_);
        waiting_ = delaying_ = false; builtin_stock_ = 0; depth_ = 0;
        finish(Status::idle);
    }
private:
    struct Frame { const Document *document; unsigned node, child; };
    struct Binding { const Document *document; unsigned node, operation; };
    Hardware &hardware_; PatchOutputs &outputs_; PatchSources &sources_;
    unsigned owner_; Document document_;
    Frame *frames_ = nullptr;
    Binding *bindings_ = nullptr;
    unsigned binding_capacity_ = 0, binding_count_ = 0, binding_cursor_ = 0;
    Resource *resources_ = nullptr;
    unsigned resource_capacity_ = 0, resource_count_ = 0;
    unsigned capacity_ = 0, depth_ = 0, root_ = 0, completed_ = 0;
    uint32_t fingerprint_ = 0;
    uint32_t delay_started_ = 0, delay_duration_ = 0;
    bool delaying_ = false;
    uint8_t program_[14] = {};
    uint8_t builtin_stock_ = 0;
    Status status_ = Status::idle;
    const char *error_ = "";
    const char *waiting_output_ = nullptr;
    bool file_waiting_ = false;
    bool waiting_ = false, pinned_ = false, persistent_ = false, validating_ = false, reconstructing_ = false, started_ = false;
    bool state_complete_ = true;

    void reset() {
        cancel(); error_ = ""; root_ = completed_ = resource_count_ = binding_count_ = binding_cursor_ = 0; started_ = false; state_complete_ = true;
    }
    void finish(Status status) {
        status_ = status;
        if (validating_) { validating_ = false; outputs_.validation_context(*this, false); }
        if (status != Status::pending && status != Status::suspended) { document_ = Document(); depth_ = 0; }
        if (pinned_) { pinned_ = false; sources_.end(); }
    }
    bool fail(const char *message) {
        if (file_waiting_) outputs_.cancel_midi_file(owner_);
        file_waiting_ = false;
        if (waiting_) outputs_.cancel_patch(owner_);
        waiting_ = false; error_ = message; hardware_.error(message);
        finish(Status::failed); return false;
    }
    bool push(const Document *document, unsigned node) {
        if (node >= document->nodes()) return fail("Invalid patch node");
        for (unsigned i = 0; i < depth_; ++i)
            if (frames_[i].document->data() == document->data() && frames_[i].node == node)
                return fail("Patch reference cycle");
        if (depth_ == capacity_) {
            unsigned capacity = capacity_ ? capacity_ * 2 : 8;
            void *memory = std::realloc(frames_, capacity * sizeof(Frame));
            if (!memory) return fail("Patch traversal memory exhausted");
            frames_ = static_cast<Frame *>(memory); capacity_ = capacity;
        }
        frames_[depth_++] = {document, node, 0};
        return true;
    }
    void hash(uint32_t value) { fingerprint_ = (fingerprint_ ^ value) * 16777619u; }
    const uint8_t *next_leaf(bool validating) {
        while (depth_) {
            Frame &f = frames_[depth_ - 1];
            const Document *document = f.document;
            const uint8_t *n = document->node(f.node), *p = n + 4;
            if (!f.child && validating) { hash(u32(document->data() + 20)); hash(f.node); }
            if (n[0] == 3 && (p[0] == 7 || p[0] == 8)) {
                if (f.child) { if (!validating) outputs_.selection_complete(p[0]); --depth_; continue; }
                ++f.child; unsigned node = 0;
                if (!bind_selection(validating, p[0], document, node)) return nullptr;
                if (node == 65535) { if (!validating) outputs_.selection_complete(p[0]); --depth_; continue; }
                if (!push(document, node)) return nullptr;
            } else if (n[0] == 5) {
                unsigned count = u16(p);
                if (f.child == count) { --depth_; continue; }
                unsigned node = u16(p + 2 + f.child++ * 2);
                if (!push(document, node)) return nullptr;
            } else if (n[0] == 6) {
                if (f.child) { --depth_; continue; }
                ++f.child;
                uint32_t revision = 0;
                if (p[0] && !sources_.acquire(reinterpret_cast<const char *>(p), document, revision)) {
                    fail("Referenced setup is missing or invalid"); return nullptr;
                }
                if (validating) hash(revision);
                int node = document->named_patch(reinterpret_cast<const char *>(p + 64));
                if (node < 0) { fail("Referenced patch does not exist"); return nullptr; }
                if (!push(document, unsigned(node))) return nullptr;
            } else {
                if (validating && n[0] == 3 && !outputs_.preview_state(p[0], u32(p + 1))) {
                    fail("Invalid application operation"); return nullptr;
                }
                return n;
            }
        }
        return nullptr;
    }
    void complete_leaf() { --depth_; ++completed_; }
    bool bind_selection(bool validating, unsigned operation, const Document *&document, unsigned &node) {
        if (!validating || reconstructing_) {
            if (binding_cursor_ >= binding_count_ || bindings_[binding_cursor_].operation != operation)
                return fail("Invalid selected-patch binding");
            const Binding &binding = bindings_[binding_cursor_++];
            document = binding.document; node = binding.node; return true;
        }
        if (!outputs_.selection(operation, document, node)) return fail("No patch selected for activation");
        if (binding_count_ == binding_capacity_) {
            unsigned capacity = binding_capacity_ ? binding_capacity_ * 2 : 8;
            void *memory = std::realloc(bindings_, capacity * sizeof(Binding));
            if (!memory) return fail("Patch binding memory exhausted");
            bindings_ = static_cast<Binding *>(memory); binding_capacity_ = capacity;
        }
        bindings_[binding_count_++] = {document, node, operation};
        if (node != 65535) hash(checksum(document->data(), document->length()));
        return true;
    }
    void dispatch_stock(uint8_t buttons) {
        if (!leaving()) return;
        // Count the visit before checkpointing, so a return resumes after it.
        ++completed_; if (depth_) --depth_;
        builtin_stock_ = 0; started_ = true; status_ = Status::suspended;
        if (!outputs_.stock(buttons, *this)) fail("Firmware switch failed");
    }
    bool resource(const char *output, unsigned channel, unsigned control, int expected = -1) {
        Resource value = {}; std::snprintf(value.output, sizeof(value.output), "%s", output); value.channel = channel; value.control = control;
        for (unsigned i = 0; i < resource_count_; ++i)
            if (resources_[i].channel == channel && resources_[i].control == control && !std::strcmp(resources_[i].output, output)) {
                resources_[i].value = expected; return true;
            }
        if (resource_count_ == resource_capacity_) {
            unsigned capacity = resource_capacity_ ? resource_capacity_ * 2 : 8;
            void *memory = std::realloc(resources_, capacity * sizeof(Resource));
            if (!memory) return fail("Patch ownership memory exhausted");
            resources_ = static_cast<Resource *>(memory); resource_capacity_ = capacity;
        }
        value.value = expected; resources_[resource_count_++] = value; return true;
    }
    bool collect(const uint8_t *node, const Document &document) {
        const uint8_t *p = node + 4;
        if (node[0] == 1) {
            const uint8_t *instrument = document.instrument(u16(p));
            const char *output = reinterpret_cast<const char *>(instrument + 32);
            unsigned channel = instrument[64];
            if (!resource(output, channel, 128, p[2])) return false;
            constexpr unsigned controllers[] = {0, 32, 7, 10};
            for (unsigned i = 0; i < 4; ++i)
                if (p[3 + i] != 255 && !resource(output, channel, controllers[i], i < 2 ? -1 : p[3 + i])) return false;
            if (p[3] != 255 && !resource(output, channel, 130, p[3])) return false;
            if (p[4] != 255 && !resource(output, channel, 131, p[4])) return false;
        } else if (node[0] == 2) {
            const char *output = reinterpret_cast<const char *>(p);
            if (node[1] & 1) {
                if (u16(node + 2) == 34 && p[32] >> 4 == 12)
                    return resource(output, p[32] & 15, 128, p[33]);
                state_complete_ = false; return resource(output, 256, 256);
            }
            if (node[1] == 4) {
                if (!resource(output, p[32] & 15, 256 + (p[34] << 7) + p[37], (p[40] << 7) + p[43])) return false;
                for (unsigned c : {99u, 98u, 6u, 38u}) if (!resource(output, p[32] & 15, c)) return false;
                return true;
            }
            for (unsigned offset = 32; offset < u16(node + 2); offset += 3) {
                unsigned status = p[offset], control = status >> 4 == 14 ? 129 : p[offset + 1];
                int expected = status >> 4 == 14 ? p[offset + 1] | (p[offset + 2] << 7) : p[offset + 2];
                if ((control == 6 || control == 38 || control >= 96) && status >> 4 == 11) state_complete_ = false;
                if (!resource(output, status & 15, control, expected)) return false;
            }
        } else if (node[0] == 8) {
            state_complete_ = false;
            unsigned mask = u16(p + 96);
            for (const auto &out : midi::output_connections) if ((mask & (1u << out.route)) && !resource(out.name, 256, 256)) return false;
            for (const auto &out : midi::usb_output_connections) if ((mask & (1u << out.route)) && !resource(out.name, 256, 256)) return false;
        } else if (node[0] != 7) state_complete_ = false;
        return true;
    }
    bool leaving() {
        auto delivery = outputs_.prepare_leave(*this);
        if (delivery == midi::Bridge::Delivery::pending) return false;
        if (delivery != midi::Bridge::Delivery::complete) { fail("Switch cleanup has unknown remote state"); return false; }
        return true;
    }
};
} // namespace cntrl
