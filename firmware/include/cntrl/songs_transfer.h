#pragma once
#include <cntrl/usb_config.h>
#include "songs.h"

namespace cntrl {
class SongsTransfer {
public:
    SongsTransfer(Songs &songs, Replies &replies) : songs_(songs), replies_(replies) {}
    bool processing() const { return processing_; }
    void keepalive(uint32_t now) { if (receiving_) last_ = now; }
    void cancel_transfer() { if (receiving_) songs_.import_abort(); receiving_ = false; }
    bool busy() const { return receiving_ || processing_; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(songs_transfer_data)
#endif
    bool data(const uint8_t *bytes, unsigned length, uint32_t now) {
        last_ = now;
        if (!receiving_ || !length || length > usb_block_size || length > expected_ - received_
            || !songs_.import_write(bytes, length)) return error();
        received_ += length; replies_.reply("CNTRL OK"); return true;
    }
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
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(songs_transfer_command)
#endif
    bool command(const char *line, uint32_t now = 0) {
        last_ = now;
        if (std::strncmp(line, "CNTRL SONGS ", 12)) return false;
        if (processing_) { replies_.reply("CNTRL ERROR songs-busy"); return true; }
        unsigned length = 0, overwrite = 0; unsigned long crc = 0; int end = 0;
        if (!std::strcmp(line, "CNTRL SONGS UNDO SIZE")) {
            uint32_t check = 0;
            if (!songs_.undo_source(length, check)) return error();
            if (!length) { replies_.reply("CNTRL SONGS UNDO NONE"); return true; }
            char response[64]; std::snprintf(response, sizeof(response), "CNTRL SONGS %u %08lx", length, (unsigned long)check);
            replies_.reply(response); return true;
        }
        if (std::sscanf(line, "CNTRL SONGS UNDO READ %u%n", &length, &end) == 1 && end && !line[end]) {
            unsigned size = 0; uint32_t check = 0;
            if (!songs_.undo_source(size, check) || length >= size) return error();
            unsigned n = std::min(usb_block_size, size - length);
            MemoryBuffer buffer;
            if (!buffer.resize(n) || !songs_.read_undo_payload(length, buffer.data, n)) return error();
            replies_.data(buffer.data, n); return true;
        }
        end = 0;
        if (std::sscanf(line, "CNTRL SONGS BEGIN %u %lx %u%n", &length, &crc, &overwrite, &end) == 3
            && end && !line[end] && length >= 12 && length <= 0xffffffffu - 20 && crc <= 0xfffffffful && overwrite <= 1) {
            if (receiving_) { songs_.import_abort(); receiving_ = false; }
            if (!overwrite && (songs_.invalid() || songs_.counts().songs || songs_.counts().sets)) return error();
            if (!songs_.import_begin(length)) return error();
            expected_ = length; received_ = 0; expected_crc_ = uint32_t(crc); receiving_ = true; replies_.reply("CNTRL OK"); return true;
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
            unsigned n = std::min(usb_block_size, songs_.length() - length);
            MemoryBuffer buffer; if (!buffer.resize(n) || !songs_.read_payload(length, buffer.data, n)) return error();
            replies_.data(buffer.data, n); return true;
        }
        return error();
    }
private:
    bool error() {
        if (receiving_) songs_.import_abort();
        receiving_ = false; replies_.reply("CNTRL ERROR songs"); return true;
    }
    Songs &songs_; Replies &replies_;
    unsigned received_ = 0, expected_ = 0;
    uint32_t expected_crc_ = 0;
    bool receiving_ = false, processing_ = false;
    uint32_t last_ = 0;
};
} // namespace cntrl
