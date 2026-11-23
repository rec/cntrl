#pragma once
#include "application.h"
#include <cntrl/patch_sources.h>

namespace cntrl::ui {
class SongSources : public cntrl::SongResolver, public cntrl::SongCapture {
public:
    SongSources(Hardware &hardware, PatchOutputs &outputs, PatchSources &sources, SetupTable &setups,
                Documents &documents, Application &application, Songs &songs, unsigned owner = 8)
        : executor(hardware, outputs, sources, owner), hardware_(hardware), outputs_(outputs), sources_(sources), setups_(setups),
          documents_(documents), application_(application), songs_(songs) {}
    cntrl::PatchExecutor executor;
    ~SongSources() override { executor.cancel(); }
    void tick(uint32_t now) {
        executor.tick(now);
        if (embedded_.data && !executor.busy()) {
            embedded_.clear(); embedded_document_ = cntrl::Document();
        }
    }
    const cntrl::SongContinuation &context() const { return context_; }
    void clear_song_midi_streams() override { outputs_.select_song_midi_streams(nullptr, midi::Streams::system_scope); }
    midi::Bridge::Delivery leave() override { return outputs_.prepare_leave(executor); }
    void recalled() override { application_.controller().cancel_gesture(); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(song_sources_capture)
#endif
    bool capture(cntrl::Song &song, bool reference) override {
        const auto &d = documents_.document();
        if (!d.valid() || !application_.layout.controllers() || !d.patches() || documents_.busy() || documents_.dirty() || application_.patch_pending()) return false;
        unsigned index = application_.controller().selected_patch();
        const uint8_t *p = d.patch(index);
        std::snprintf(song.name, 64, "%s", reinterpret_cast<const char *>(p + 12));
        std::snprintf(song.label, 64, "%s", song.name);
        song.node = d.browser_root(index);
        if (reference) {
            const char *key = nullptr;
            const uint8_t *node = d.node(song.node);
            if (node[0] == 6 && cntrl::u16(node + 4) == 65535) key = reinterpret_cast<const char *>(node + 6);
            for (unsigned i = 0; !key && i < d.named_patches(); ++i)
                if (cntrl::u16(d.named_patch(i) + 64) == song.node) key = reinterpret_cast<const char *>(d.named_patch(i));
            if (!key) return false;
            std::snprintf(song.document, 64, "%s", documents_.selected());
            std::snprintf(song.key, 64, "%s", key); song.source = 0xffffffff;
        } else song.source = 0xfffffffe;
        return true;
    }
    bool capture_source(const cntrl::Document *&source) override {
        if (documents_.busy() || documents_.dirty() || application_.busy() || songs_.executing()) return false;
        source = &documents_.document(); return source->valid();
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(song_sources_revision)
#endif
    bool revision(const cntrl::SongSelection &song, uint32_t &crc) override {
        sources_.begin();
        const cntrl::Document *source = nullptr;
        int index = setups_.index(song.document);
        bool valid = song.document[0] && index >= 0 && sources_.acquire(uint16_t(index), source, crc);
        sources_.end(); return valid;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(song_sources_activate)
#endif
    bool activate(const cntrl::SongSelection &song, const cntrl::SongContinuation &context, bool resume) override {
        if (application_.busy() || executor.busy() || documents_.busy() || documents_.dirty()) return false;
        embedded_.clear();
        sources_.begin();
        const cntrl::Document *source = nullptr;
        unsigned node = song.node;
        uint32_t revision = 0;
        bool valid = false;
        if (song.document[0]) {
            int index = setups_.index(song.document);
            valid = index >= 0 && sources_.acquire(uint16_t(index), source, revision);
            if (valid) {
                int root = source->named_patch(song.key);
                valid = root >= 0; node = valid ? unsigned(root) : 0;
            }
        } else {
            unsigned size = songs_.source_size(song.source);
            if (size && embedded_.resize(size) && songs_.read_source(song.source, 0, embedded_.data, size)
                && embedded_document_.load(embedded_.data, size, hardware_)) {
                source = &embedded_document_; valid = true;
            }
        }
        context_ = context;
        if (valid) {
            MemoryBuffer scope_bytes;
            Document scope_document;
            unsigned size = songs_.source_size(0);
            const Document *scope_source = song.source == 0 && !song.document[0] ? source : nullptr;
            const auto *current_song = songs_.song(context.song);
            unsigned song_bindings = current_song ? current_song->bindings : ~0u;
            if (!scope_source && size) {
                if (!scope_bytes.resize(size) || !songs_.read_source(0, 0, scope_bytes.data, size)
                    || !scope_document.load(scope_bytes.data, size, hardware_)) { sources_.end(); return false; }
                scope_source = &scope_document;
            }
            if (scope_source && song_bindings == ~0u && scope_source->binding_scope(0) < 0) scope_source = nullptr;
            if (!scope_source && song_bindings != ~0u) { sources_.end(); return false; }
            auto previous = application_.controller().navigation();
            if (!resume) application_.controller().external_selection();
            valid = resume ? node == context.node && executor.resume(*source, node, context.completed, context.sources_crc)
                           : executor.start(*source, node);
            if (valid) valid = outputs_.binding_context(scope_source, song_bindings);
            const bool entering = context.caller == 1 && (resume || songs_.boundary());
            if (valid && entering && song_bindings != ~0u) {
                unsigned scope;
                valid = scope_source->stream_scope(song_bindings, scope)
                    && outputs_.select_song_midi_streams(scope_source, scope);
            }
            if (valid && !resume) valid = outputs_.select_midi_streams(*source, node);
            if (!valid) {
                executor.cancel();
                if (entering) clear_song_midi_streams();
            }
            if (!valid && !resume) application_.controller().navigation(previous);
        }
        sources_.end(); return valid;
    }
    cntrl::SongExecution execution_status() const override {
        switch (executor.status()) {
        case cntrl::PatchExecutor::Status::pending: return cntrl::SongExecution::pending;
        case cntrl::PatchExecutor::Status::failed: return cntrl::SongExecution::failed;
        case cntrl::PatchExecutor::Status::unknown: return cntrl::SongExecution::unknown;
        case cntrl::PatchExecutor::Status::suspended: return cntrl::SongExecution::suspended;
        default: return cntrl::SongExecution::completed;
        }
    }
private:
    Hardware &hardware_;
    PatchOutputs &outputs_;
    PatchSources &sources_;
    SetupTable &setups_;
    Documents &documents_;
    Application &application_;
    Songs &songs_;
    cntrl::SongContinuation context_;
    cntrl::Document embedded_document_;
    cntrl::MemoryBuffer embedded_;
};
} // namespace cntrl::ui
