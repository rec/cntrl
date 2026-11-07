#pragma once
#include <cstdint>

namespace cntrl {
// Cooperative ordering is policy. Clock/GPIO/USB and display readiness are
// services, so a slow display never prevents a due input scan taking the bus.
class CooperativeRuntime {
public:
    explicit constexpr CooperativeRuntime(uint32_t scan_interval_ms = 1)
        : scan_interval_(scan_interval_ms) {}
    bool scan_due(uint32_t now) const { return uint32_t(now - last_scan_) >= scan_interval_; }
    template<class Services>
    void poll(Services &s) {
        s.complete_display(); s.keyboard();
        uint32_t now = s.now();
        if (s.scan_available() && scan_due(now)) {
            const uint32_t gap = scanned_ ? uint32_t(now - last_scan_) : 0;
            last_scan_ = now; scanned_ = true; s.scan(now, gap);
        }
        s.apps(now);
        // Deliver button actions before storage and display work.
        s.midi();
        auto &session = s.session(); auto &commands = s.commands();
        for (unsigned i = 0; i < serial_budget && !session.output_pending() && s.serial_available(); ++i) session.receive(s.serial_read());
        if (session.resetting() && commands.accepts_input() && !s.song_transfer().processing()) {
            commands.cancel_transfer(); s.song_transfer().cancel_transfer(); s.bundle_transfer().cancel(); s.history_transfer().cancel(); s.fonts().cancel_transfer(); s.cancel_listing(); session.reset_done();
        }
        for (unsigned i = 0; i < command_budget && session.input_size() && s.reply_ready() && s.commands_allowed() && commands.accepts_input(); ++i) {
            commands.receive(session.input(), now); session.consumed();
        }
        // A resumable session retains unfinished transfers while disconnected.
        if (session.active()) {
            commands.keepalive(now); s.song_transfer().keepalive(now); s.fonts().keepalive(now); s.bundle_transfer().keepalive(now); s.history_transfer().keepalive(now);
        }
        if (s.reply_ready()) commands.poll(now);
        if (s.reply_ready()) s.song_transfer().poll(now);
        if (s.reply_ready()) s.bundle_transfer().poll(now);
        if (s.reply_ready()) s.history_transfer().tick(now);
        s.song_sources().tick(now); s.songs().tick();
        if (s.reply_ready()) s.fonts().poll(now, s.replies());
        if (s.expression_allowed()) s.expression(now);
        s.navigation(); s.display(now);
    }
    static constexpr unsigned serial_budget = 512, command_budget = 128;
private:
    uint32_t scan_interval_, last_scan_ = 0;
    bool scanned_ = false;
};
} // namespace cntrl
