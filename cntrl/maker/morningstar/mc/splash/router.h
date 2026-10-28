#pragma once
#include "session.h"
#include <cntrl/documents.h>

namespace cntrl::splash {
// Decide command ownership before handing a line to either binary receiver.
class Router {
public:
    Router(cntrl::Documents &documents, Receiver &assets, Session &session, cntrl::Replies &replies,
           void (*storage_info)(bool), void (*midi_status)(unsigned) = nullptr,
           bool (*asset_command)(const char *) = nullptr)
        : documents_(documents), assets_(assets), session_(session), replies_(replies),
          storage_info_(storage_info), midi_status_(midi_status), asset_command_(asset_command) {}
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
            if (asset_command_ && (!std::strncmp(line_, "CNTRL FONT ", 11) || !std::strncmp(line_, "CNTRL SONGS ", 12))) {
                session_.stop(); asset_command_(line_); used_ = 0; overflow_ = false; return;
            }
            if (midi_status_ && !std::strcmp(line_, "CNTRL MIDI RESET")) {
                midi_status_(9); used_ = 0; overflow_ = false; return;
            }
            unsigned route = 0; int end = 0;
            if (midi_status_ && std::sscanf(line_, "CNTRL MIDI STATUS %u%n", &route, &end) == 1
                && end && !line_[end]) {
                midi_status_(route); used_ = 0; overflow_ = false; return;
            }
            if (!std::strcmp(line_, "CNTRL STORAGE") || !std::strcmp(line_, "CNTRL STORAGE SCAN")) {
                storage_info_(!std::strcmp(line_, "CNTRL STORAGE SCAN"));
                used_ = 0; overflow_ = false;
                return;
            }
            bool splash = !std::strncmp(line_, "CNTRL SPLASH ", 13);
            if (!std::strncmp(line_, "CNTRL SETUP PUT ", 16)
                || !std::strncmp(line_, "CNTRL SETUP GET ", 16)
                || !std::strncmp(line_, "CNTRL SETUP SET ", 16)
                || !std::strncmp(line_, "CNTRL SETUP DELETE ", 19)
                || !std::strcmp(line_, "CNTRL SETUP FORWARD")
                || !std::strcmp(line_, "CNTRL SETUP BACKWARD")
                || !std::strcmp(line_, "CNTRL SETUP FINISH")
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
        if (!replies_.reply_ready()) return;
        documents_.tick(now);
        if (!replies_.reply_ready()) return;
        assets_.poll(now);
        if (!replies_.reply_ready()) return;
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
    void (*storage_info_)(bool);
    void (*midi_status_)(unsigned);
    bool (*asset_command_)(const char *);
    char line_[320];
    unsigned used_ = 0;
    uint32_t last_ = 0;
    bool overflow_ = false;
};
} // namespace cntrl::splash
