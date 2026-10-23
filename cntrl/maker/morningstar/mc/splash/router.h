#pragma once
#include "session.h"
#include <cntrl/documents.h>

namespace cntrl::splash {
// Decide command ownership before handing a line to either binary receiver.
class Router {
public:
    Router(cntrl::Documents &documents, Receiver &assets, Session &session, cntrl::Replies &replies)
        : documents_(documents), assets_(assets), session_(session), replies_(replies) {}
    bool accepts_input() const { return assets_.accepts_input(); }
    void receive(uint8_t byte, uint32_t now) {
        if (assets_.busy()) { assets_.receive(byte, now); return; }
        if (documents_.busy()) { documents_.receive(byte, now); return; }
        last_ = now;
        if (byte != '\n') {
            if (byte == '\r') return;
            if (used_ < sizeof(line_) - 1) line_[used_++] = char(byte);
            else overflow_ = true;
            return;
        }
        if (overflow_) replies_.reply("CNTRL ERROR command-too-long");
        else {
            line_[used_] = 0;
            bool splash = !std::strncmp(line_, "CNTRL SPLASH ", 13);
            if (!std::strncmp(line_, "CNTRL BEGIN ", 12)
                || !std::strncmp(line_, "CNTRL SPLASH BEGIN ", 19)) session_.stop();
            for (unsigned i = 0; i <= used_; ++i) {
                uint8_t c = i == used_ ? '\n' : uint8_t(line_[i]);
                if (splash) assets_.receive(c, now);
                else documents_.receive(c, now);
            }
        }
        used_ = 0; overflow_ = false;
    }
    void poll(uint32_t now) {
        documents_.tick(now);
        assets_.poll(now);
        if ((used_ || overflow_) && uint32_t(now - last_) > 10000) {
            used_ = 0; overflow_ = false;
            replies_.reply("CNTRL ERROR transfer-timeout");
        }
    }
private:
    cntrl::Documents &documents_;
    Receiver &assets_;
    Session &session_;
    cntrl::Replies &replies_;
    char line_[96];
    unsigned used_ = 0;
    uint32_t last_ = 0;
    bool overflow_ = false;
};
} // namespace cntrl::splash
