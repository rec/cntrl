#pragma once
#include <cntrl/navigation_snapshots.h>
#include "song_sources.h"
#include <cntrl/filesystem.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace cntrl::ui {
struct RuntimeState { uint8_t brightness = 192; bool interface = false; };
struct FirmwareVisitRecord {
    static constexpr uint32_t format = 0x46564934;
    static constexpr unsigned header_size = 94 + sizeof(SongContinuation);
    uint32_t token = 0, setup_crc = 0;
    char setup[64] = {};
    SongContinuation continuation;
    Controller::NavigationState initial_navigation, return_navigation;
    RuntimeState runtime;
    uint32_t ui_operation = 0, controller = 0;
    bool encode(MemoryBuffer &destination) const {
        if (!initial_navigation.valid || !return_navigation.valid) return false;
        const unsigned initial = initial_navigation.encoded_size(), returned = return_navigation.encoded_size();
        if (uint64_t(header_size) + initial + returned + 4 > UINT_MAX) return false;
        MemoryBuffer bytes;
        if (!bytes.resize(header_size + initial + returned + 4)) return false;
        auto *p = bytes.data;
        put32(p, format); put32(p + 4, token); put32(p + 8, setup_crc);
        std::memcpy(p + 12, setup, 64); std::memcpy(p + 76, &continuation, sizeof(continuation));
        unsigned offset = 76 + sizeof(continuation);
        p[offset++] = runtime.brightness; p[offset++] = runtime.interface;
        put32(p + offset, ui_operation); offset += 4; put32(p + offset, controller); offset += 4;
        put32(p + offset, initial); offset += 4; put32(p + offset, returned); offset += 4;
        if (!initial_navigation.encode(p + offset, initial) || !return_navigation.encode(p + offset + initial, returned)) return false;
        put32(p + bytes.size - 4, checksum(p, bytes.size - 4)); destination.swap(bytes);
        return true;
    }
    bool decode(const uint8_t *bytes, unsigned size) {
        if (size < header_size + 44 || u32(bytes) != format || u32(bytes + size - 4) != checksum(bytes, size - 4)) return false;
        const unsigned initial = u32(bytes + header_size - 8), returned = u32(bytes + header_size - 4);
        if (initial < 20 || returned < 20 || uint64_t(header_size) + initial + returned + 4 != size
            || !text_valid(bytes + 12, 64) || bytes[77 + sizeof(continuation)] > 1) return false;
        FirmwareVisitRecord candidate;
        candidate.token = u32(bytes + 4); candidate.setup_crc = u32(bytes + 8);
        std::memcpy(candidate.setup, bytes + 12, 64); std::memcpy(&candidate.continuation, bytes + 76, sizeof(continuation));
        if (!text_valid(reinterpret_cast<const uint8_t *>(candidate.continuation.document), 64)
            || !text_valid(reinterpret_cast<const uint8_t *>(candidate.continuation.set), 64)
            || !text_valid(reinterpret_cast<const uint8_t *>(candidate.continuation.song), 64)) return false;
        unsigned offset = 76 + sizeof(continuation);
        candidate.runtime.brightness = bytes[offset++]; candidate.runtime.interface = bytes[offset++];
        candidate.ui_operation = u32(bytes + offset); offset += 4; candidate.controller = u32(bytes + offset);
        if (!candidate.initial_navigation.decode(bytes + header_size, initial)
            || !candidate.return_navigation.decode(bytes + header_size + initial, returned)) return false;
        *this = candidate; return true;
    }
};
struct FirmwareSelection { uint32_t next = 0, returned = 0; bool can_enter = false, returned_to_application = false; };
struct FirmwareVisitServices {
    virtual ~FirmwareVisitServices() = default;
    virtual FirmwareSelection firmware_selection() const = 0;
    virtual bool valid_exit(uint8_t) const = 0;
    virtual bool storage_available() const = 0;
    virtual bool output_idle() const = 0;
    virtual bool stop_clock() = 0;
    virtual bool song_transfer_busy() const = 0;
    virtual bool configure_return(uint8_t pair, uint32_t token) = 0;
    virtual bool choose_external_firmware() = 0;
    virtual bool consume_return(uint32_t token) = 0;
    virtual void handoff_firmware() = 0;
    virtual bool switch_invocation(PatchExecutor &) const = 0;
};
class FirmwareVisit {
public:
    FirmwareVisit(FirmwareVisitRecord &record, files::File &file, FirmwareVisitServices &services,
                  Hardware &hardware, Documents &documents, Application &application, Songs &songs,
                  SongSources &sources, RuntimeState &runtime, NavigationSnapshots &navigation, PatchExecutor *&active_invocation, bool &restoring_invocation)
        : invocation(active_invocation), restoring(restoring_invocation), record_(record), file_(file), services_(services), hardware_(hardware), documents_(documents),
          application_(application), songs_(songs), sources_(sources), runtime_(runtime), navigation_(navigation) {}
    bool busy() const { return stage_ != Stage::idle; }
    bool start(uint8_t pair) { return application_.controller().executor().invoke(StockFirmwarePatch{pair}); }
    bool stock(uint8_t pair, PatchExecutor &execution) {
        invocation = &execution;
        if (!execution.document().valid()) return request(pair, nullptr);
        SongContinuation saved;
        if (&execution == &sources_.executor) saved = sources_.context();
        else if (services_.switch_invocation(execution)) saved.caller = 2;
        saved.node = execution.root(); saved.completed = execution.completed(); saved.sources_crc = execution.fingerprint();
        return request(pair, &saved);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(firmware_visit_request)
#endif
    bool request(uint8_t pair, const SongContinuation *continuation) {
        if (busy() || !services_.valid_exit(pair) || documents_.busy() || documents_.dirty() || services_.song_transfer_busy() || !services_.output_idle()) return false;
        for (unsigned i = 0; i < application_.layout.controllers(); ++i)
            if (i != application_.rendering() && application_.at(i).busy()) return false;
        const auto *initial = continuation && invocation ? navigation_.find(*invocation) : nullptr;
        if (continuation && !initial) return false;
        const auto state = services_.firmware_selection();
        if (!state.can_enter || (continuation && !services_.storage_available())) return false;
        record_ = FirmwareVisitRecord(); exit_pair_ = pair; offset_ = 0;
        if (continuation) {
            record_.token = state.next; record_.continuation = *continuation;
            record_.initial_navigation = *initial;
            record_.return_navigation = application_.controller().navigation(); record_.runtime = runtime_;
            record_.ui_operation = application_.pending_operation; record_.controller = application_.rendering();
            std::snprintf(record_.setup, sizeof(record_.setup), "%s", documents_.selected());
            record_.setup_crc = setup_crc();
            if (!record_.encode(encoded_)) { record_ = FirmwareVisitRecord(); return false; }
            stage_ = Stage::create;
        } else stage_ = Stage::configure;
        if (!services_.stop_clock()) { stage_ = Stage::idle; encoded_.clear(); return false; }
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(firmware_visit_poll)
#endif
    void poll(bool released) {
        switch (stage_) {
        case Stage::idle:
            if (resume_checked_ || !released || documents_.busy()) return;
            resume_checked_ = true;
            if (const auto state = services_.firmware_selection(); state.returned && state.returned_to_application) {
                record_ = FirmwareVisitRecord(); record_.token = state.returned; stage_ = Stage::open;
            }
            return;
        case Stage::create:
            if (!file_.open(path, files::Mode::truncate)) { fail(); return; }
            stage_ = Stage::write; return;
        case Stage::write: {
            const unsigned count = chunk();
            if (!file_.write(offset_, encoded_.data + offset_, count)) { fail(); return; }
            offset_ += count; if (offset_ == encoded_.size) stage_ = Stage::sync; return;
        }
        case Stage::sync:
            if (!file_.sync()) { fail(); return; }
            offset_ = 0; stage_ = Stage::verify; return;
        case Stage::verify: {
            uint8_t bytes[512]; const unsigned count = chunk();
            if (!file_.read(offset_, bytes, count) || std::memcmp(bytes, encoded_.data + offset_, count)) { fail(); return; }
            offset_ += count;
            if (offset_ == encoded_.size) { file_.close(); encoded_.clear(); stage_ = Stage::configure; } return;
        }
        case Stage::configure:
            if (!services_.configure_return(exit_pair_, record_.token)) { fail(); return; }
            stage_ = Stage::select; return;
        case Stage::select:
            if (!services_.choose_external_firmware()) { fail(); return; }
            stage_ = Stage::handoff; return;
        case Stage::handoff:
            services_.handoff_firmware(); return;
        case Stage::open: {
            token_ = record_.token; offset_ = 0;
            uint8_t magic[4];
            valid_ = file_.open(path, files::Mode::read) && file_.size() >= FirmwareVisitRecord::header_size + 44
                && file_.size() <= UINT_MAX && file_.read(0, magic, 4) && u32(magic) == FirmwareVisitRecord::format
                && encoded_.resize(unsigned(file_.size()));
            stage_ = Stage::read; return;
        }
        case Stage::read: {
            const unsigned count = chunk();
            valid_ = valid_ && file_.read(offset_, encoded_.data + offset_, count);
            offset_ += count;
            if (valid_ && offset_ < encoded_.size) return;
            file_.close(); stage_ = Stage::validate; return;
        }
        case Stage::validate:
            valid_ = valid_ && record_.decode(encoded_.data, encoded_.size) && record_.token == token_
                && !std::strncmp(record_.setup, documents_.selected(), sizeof(record_.setup)) && record_.setup_crc == setup_crc();
            encoded_.clear();
            stage_ = Stage::consume; return;
        case Stage::consume:
            if (!services_.consume_return(token_)) { fail(); return; }
            stage_ = Stage::resume; return;
        case Stage::resume:
            resume(); return;
        }
    }
    PatchExecutor *&invocation;
    bool &restoring;
    static constexpr const char *path = "/cntrl/firmware-visit.bin";
private:
    enum class Stage { idle, create, write, sync, verify, configure, select, handoff, open, read, validate, consume, resume };
    FirmwareVisitRecord &record_;
    files::File &file_;
    FirmwareVisitServices &services_;
    Hardware &hardware_;
    Documents &documents_;
    Application &application_;
    Songs &songs_;
    SongSources &sources_;
    RuntimeState &runtime_;
    NavigationSnapshots &navigation_;
    Stage stage_ = Stage::idle;
    uint32_t offset_ = 0, token_ = 0;
    uint8_t exit_pair_ = 0;
    bool resume_checked_ = false, valid_ = false;
    MemoryBuffer encoded_;
    unsigned chunk() const { return std::min<unsigned>(encoded_.size - offset_, 512u); }
    uint32_t setup_crc() const {
        const auto &document = documents_.document(); return document.valid() ? checksum(document.data(), document.length()) : 0;
    }
    void fail() {
        file_.close(); encoded_.clear(); stage_ = Stage::idle; songs_.cancel_function();
        if (invocation) invocation->cancel();
        invocation = nullptr; hardware_.error("Firmware switch failed");
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(firmware_visit_resume)
#endif
    void resume() {
        const auto continuation = record_.continuation; stage_ = Stage::idle;
        if (record_.controller >= application_.layout.controllers()) { hardware_.error("Page return unavailable"); return; }
        application_.visible_index = record_.controller;
        if (!valid_ || !application_.controller().navigation(record_.initial_navigation)) { hardware_.error("Patch return unavailable"); return; }
        restoring = true;
        const bool resumed = continuation.caller == 1 ? songs_.resume(continuation)
            : application_.controller().executor().resume(documents_.document(), continuation.node, continuation.completed, continuation.sources_crc);
        restoring = false;
        if (!resumed || !application_.controller().navigation(record_.return_navigation)) {
            application_.controller().executor().cancel(); sources_.executor.cancel(); hardware_.error("Patch return unavailable"); return;
        }
        const auto &execution = continuation.caller == 1 ? sources_.executor : application_.controller().executor();
        if (!navigation_.put(execution, record_.initial_navigation)) {
            application_.controller().executor().cancel(); sources_.executor.cancel();
            hardware_.error("Patch return memory unavailable"); return;
        }
        hardware_.set_brightness(record_.runtime.brightness); hardware_.set_midi_interface(record_.runtime.interface);
        application_.pending_operation = record_.ui_operation;
        if (continuation.caller == 1) application_.song_ui.perform(); else application_.return_to_switches = continuation.caller == 2;
    }
};
} // namespace cntrl::ui
