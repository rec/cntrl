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
        if (!std::strncmp(line, "CNTRL DISK DATA ", 16)) {
            const char *hex = line + 16; unsigned width = unsigned(std::strlen(hex));
            if (state_ != State::upload || !width || width % 2 || width > usb_block_size * 2
                || width / 2 > file_size - received_) return error("disk-data");
            UsbBlock block; if (!block.bytes) return error("disk-allocation");
            for (unsigned i = 0; i < width; ++i) {
                char c = hex[i]; int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
                if (digit < 0) return error("disk-data");
                if (i % 2) block.bytes[i / 2] |= uint8_t(digit);
                else block.bytes[i / 2] = uint8_t(digit << 4);
            }
            unsigned length = width / 2;
            if (!file_.write(received_, block.bytes, length)) return error("disk-write");
            for (unsigned i = 0; i < length; ++i) {
                crc_ ^= block.bytes[i];
                for (unsigned b = 0; b < 8; ++b) crc_ = (crc_ >> 1) ^ (0xedb88320u & (0u - (crc_ & 1)));
            }
            received_ += length; replies_.reply("CNTRL OK"); return true;
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
            UsbBlock block; if (!block.bytes) return error("disk-allocation");
            unsigned length = std::min(usb_block_size, file_size - offset);
            if (!file_.read(offset, block.bytes, length)) return error("disk-read");
            std::strcpy(block.text, "CNTRL DATA ");
            for (unsigned i = 0; i < length; ++i) std::snprintf(block.text + 11 + i * 2, 3, "%02x", block.bytes[i]);
            replies_.reply(block.text); return true;
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
