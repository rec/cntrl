#pragma once
#include "songs.h"

namespace cntrl {
class SongsTransfer {
public:
    SongsTransfer(Songs &songs, Replies &replies) : songs_(songs), replies_(replies) {}
    bool busy() const { return receiving_ || processing_; }
    void poll(uint32_t now) {
        if (processing_) {
            if (!songs_.busy()) {
                processing_ = false; replies_.reply(songs_.success() ? "CNTRL OK" : "CNTRL ERROR songs");
            } else if (uint32_t(now - last_) >= 500) { last_ = now; replies_.reply("CNTRL WAIT"); }
            return;
        }
        if (receiving_ && uint32_t(now - last_) > 10000) {
            songs_.import_abort(); receiving_ = false; replies_.reply("CNTRL ERROR songs-timeout");
        }
    }
    bool command(const char *line, uint32_t now = 0) {
        last_ = now;
        if (std::strncmp(line, "CNTRL SONGS ", 12)) return false;
        if (processing_) { replies_.reply("CNTRL ERROR songs-busy"); return true; }
        auto error = [&]() {
            if (receiving_) songs_.import_abort();
            receiving_ = false; replies_.reply("CNTRL ERROR songs"); return true;
        };
        unsigned length = 0, overwrite = 0; unsigned long crc = 0; int end = 0;
        if (std::sscanf(line, "CNTRL SONGS BEGIN %u %lx %u%n", &length, &crc, &overwrite, &end) == 3
            && end && !line[end] && length >= 16 && length <= 0xffffffffu - 20 && crc <= 0xfffffffful && overwrite <= 1) {
            if (receiving_) { songs_.import_abort(); receiving_ = false; }
            if (!overwrite && (songs_.invalid() || songs_.counts().songs || songs_.counts().sets)) return error();
            if (!songs_.import_begin(length)) return error();
            expected_ = length; received_ = 0; expected_crc_ = uint32_t(crc); receiving_ = true; replies_.reply("CNTRL OK"); return true;
        }
        if (!std::strncmp(line, "CNTRL SONGS DATA ", 17)) {
            unsigned n = unsigned(std::strlen(line + 17));
            if (!receiving_ || !n || n % 2 || n > 256 || n / 2 > expected_ - received_) return error();
            uint8_t bytes[128];
            for (unsigned i = 0; i < n; ++i) {
                char c = line[17 + i]; int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
                if (v < 0) return error();
                if (i % 2) bytes[i / 2] |= uint8_t(v); else bytes[i / 2] = uint8_t(v << 4);
            }
            if (!songs_.import_write(bytes, n / 2)) return error();
            received_ += n / 2; replies_.reply("CNTRL OK"); return true;
        }
        if (!std::strcmp(line, "CNTRL SONGS END")) {
            if (!receiving_ || received_ != expected_) return error();
            receiving_ = false;
            if (!songs_.import_finish(expected_, expected_crc_)) return error();
            processing_ = true; return true;
        }
        if (!std::strcmp(line, "CNTRL SONGS SIZE")) {
            if (songs_.invalid()) return error();
            char response[64];
            std::snprintf(response, 64, "CNTRL SONGS %u %08lx", songs_.length(), (unsigned long)songs_.crc());
            replies_.reply(response); return true;
        }
        end = 0;
        if (std::sscanf(line, "CNTRL SONGS READ %u%n", &length, &end) == 1 && end && !line[end] && length < songs_.length()) {
            unsigned n = std::min(128u, songs_.length() - length);
            uint8_t bytes[128];
            if (!songs_.read_payload(length, bytes, n)) return error();
            char response[280] = "CNTRL DATA ";
            for (unsigned i = 0; i < n; ++i) std::snprintf(response + 11 + i * 2, 3, "%02x", bytes[i]);
            replies_.reply(response); return true;
        }
        return error();
    }
private:
    Songs &songs_; Replies &replies_;
    unsigned received_ = 0, expected_ = 0;
    uint32_t expected_crc_ = 0;
    bool receiving_ = false, processing_ = false;
    uint32_t last_ = 0;
};
} // namespace cntrl
