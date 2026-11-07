#pragma once
#include <cstdint>

namespace cntrl {
// Cooperative ordering is policy. Clock/GPIO/USB and display readiness are
// services, so a slow display never prevents a due input scan taking the bus.
class CooperativeRuntime {
public:
    template<class Services>
    void poll(Services &s) {
        uint32_t now = s.now();
        s.complete_display(); s.keyboard();
        if (s.scan_available() && s.scan_due()) { now = s.now(); s.scan(now); }
        s.apps(now);
        // Deliver button actions before storage and display work.
        s.midi();
        auto &session = s.session(); auto &commands = s.commands();
        for (unsigned i = 0; i < serial_budget && !session.output_pending() && s.serial_available(); ++i) session.receive(s.serial_read());
        if (session.resetting() && commands.accepts_input() && !s.song_transfer().processing()) {
            commands.cancel_transfer(); s.song_transfer().cancel_transfer(); s.bundle_transfer().cancel(); s.fonts().cancel_transfer(); s.cancel_listing(); session.reset_done();
        }
        for (unsigned i = 0; i < command_budget && session.input_size() && s.reply_ready() && s.commands_allowed() && commands.accepts_input(); ++i) {
            commands.receive(session.input(), now); session.consumed();
        }
        // A resumable session retains unfinished transfers while disconnected.
        if (session.active()) {
            commands.keepalive(now); s.song_transfer().keepalive(now); s.fonts().keepalive(now); s.bundle_transfer().keepalive(now);
        }
        if (s.reply_ready()) commands.poll(now);
        if (s.reply_ready()) s.song_transfer().poll(now);
        if (s.reply_ready()) s.bundle_transfer().poll(now);
        s.song_sources().tick(now); s.songs().tick();
        if (s.reply_ready()) s.fonts().poll(now, s.replies());
        if (s.expression_allowed()) s.expression(now);
        s.navigation(); s.display(now);
    }
    static constexpr unsigned serial_budget = 512, command_budget = 128;
};
} // namespace cntrl
