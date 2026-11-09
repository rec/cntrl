#pragma once
#include "filesystem.h"
#include "documents.h"
#include "usb_config.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace cntrl {
// A single reserved scratch file, never a setup or user-selected path.
class DiskSpeedTransfer {
public:
    DiskSpeedTransfer(files::FileSystem &filesystem, files::File &file, Replies &replies)
        : filesystem_(filesystem), file_(file), replies_(replies) {}
    ~DiskSpeedTransfer() { cancel(); }
    static constexpr unsigned file_size = 1000000;
    static constexpr const char *path = "/cntrl/.disk-speed.tmp";
    bool cancel() {
        file_.close(); state_ = State::idle;
        if (owned_ && !filesystem_.remove(path)) return false;
        owned_ = false; return true;
    }
    void keepalive(uint32_t now) { last_ = now; }
    void poll(uint32_t now) {
        if (state_ != State::idle && uint32_t(now - last_) > 10000) error("disk-timeout");
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(disk_speed_data)
#endif
    bool data(const uint8_t *bytes, unsigned length, uint32_t now) {
        last_ = now;
        if (state_ != State::upload || !length || length > usb_block_size || length > file_size - received_)
            return error("disk-data");
        if (!file_.write(received_, bytes, length)) return error("disk-write");
        for (unsigned i = 0; i < length; ++i) {
            crc_ ^= bytes[i];
            for (unsigned b = 0; b < 8; ++b) crc_ = (crc_ >> 1) ^ (0xedb88320u & (0u - (crc_ & 1)));
        }
        received_ += length; replies_.reply("CNTRL OK"); return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(disk_speed_command)
#endif
    bool command(const char *line, uint32_t now) {
        if (std::strncmp(line, "CNTRL DISK ", 11)) return false;
        last_ = now;
        if (!std::strcmp(line, "CNTRL DISK DELETE")) {
            if (!cancel()) return error("disk-delete");
            replies_.reply("CNTRL OK"); return true;
        }
        unsigned long crc = 0; unsigned offset = 0; int end = 0;
        if (std::sscanf(line, "CNTRL DISK BEGIN %lx%n", &crc, &end) == 1 && end && !line[end] && crc <= 0xfffffffful) {
            if (!cancel() || !filesystem_.available()
                || (!filesystem_.exists("/cntrl") && !filesystem_.make_directory("/cntrl"))
                || !file_.open(path, files::Mode::truncate)) return error("disk-create");
            owned_ = true; state_ = State::upload; received_ = 0;
            expected_crc_ = uint32_t(crc); crc_ = 0xffffffff;
            replies_.reply("CNTRL OK"); return true;
        }
        if (!std::strcmp(line, "CNTRL DISK END")) {
            if (state_ != State::upload || received_ != file_size || ~crc_ != expected_crc_) return error("disk-checksum");
            if (!file_.sync()) return error("disk-sync");
            file_.close();
            if (!file_.open(path, files::Mode::read) || file_.size() != file_size) return error("disk-open");
            state_ = State::download; replies_.reply("CNTRL OK"); return true;
        }
        if (std::sscanf(line, "CNTRL DISK READ %u%n", &offset, &end) == 1 && end && !line[end]) {
            if (state_ != State::download || offset >= file_size) return error("disk-read");
            unsigned length = std::min(usb_block_size, file_size - offset);
            MemoryBuffer buffer; if (!buffer.resize(length)) return error("disk-allocation");
            if (!file_.read(offset, buffer.data, length)) return error("disk-read");
            replies_.data(buffer.data, length); return true;
        }
        return error("disk-command");
    }
private:
    enum class State { idle, upload, download } state_ = State::idle;
    files::FileSystem &filesystem_; files::File &file_; Replies &replies_;
    bool owned_ = false;
    unsigned received_ = 0;
    uint32_t expected_crc_ = 0, crc_ = 0, last_ = 0;
    bool error(const char *message) {
        bool removed = cancel(); char response[80];
        std::snprintf(response, sizeof(response), "CNTRL ERROR %s", removed ? message : "disk-delete");
        replies_.reply(response); return true;
    }
};
} // namespace cntrl
