#pragma once
#include "documents.h"
#include "filesystem.h"
#include "usb_frame.h"
#include "usb_config.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace cntrl {
// Named SMF assets share the runtime's /cntrl/midi directory. Publication uses
// a synced temporary file; a failed replacement retains/restores the old file.
class MidiFileTransfer {
public:
    MidiFileTransfer(files::FileSystem &filesystem, files::File &reader,
                     files::File &writer, Replies &replies)
        : filesystem_(filesystem), reader_(reader), writer_(writer), replies_(replies) {}
    bool busy() const { return uploading_; }
    void keepalive(uint32_t now) { last_ = now; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_cancel)
#endif
    void cancel() {
        reader_.close(); writer_.close(); uploading_ = false;
        if (temporary_[0]) filesystem_.remove(temporary_);
        temporary_[0] = 0;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_poll)
#endif
    void poll(uint32_t now) {
        if (uploading_ && uint32_t(now - last_) > 10000) error("midi-file-timeout");
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_data)
#endif
    bool data(const uint8_t *bytes, unsigned length, uint32_t now) {
        last_ = now;
        if (!uploading_ || !length || length > usb_block_size || length > expected_ - received_)
            return error("midi-file-data");
        if (!writer_.write(received_, bytes, length)) return error("midi-file-write");
        for (unsigned i = 0; i < length; ++i) crc_ = (crc_ >> 8) ^ usb_crc_table[(crc_ ^ bytes[i]) & 255];
        received_ += length; replies_.reply("CNTRL OK"); return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_command)
#endif
    bool command(const char *line, uint32_t now) {
        if (std::strncmp(line, "CNTRL MIDI_FILE ", 16)) return false;
        last_ = now;
        if (!std::strcmp(line, "CNTRL MIDI_FILE END")) {
            if (!uploading_ || received_ != expected_ || ~crc_ != checksum_) return error("midi-file-checksum");
            bool synced = writer_.sync(); writer_.close();
            if (!synced) return error("midi-file-sync");
            bool existing = filesystem_.exists(path_);
            if (existing && (!overwrite_ || !filesystem_.rename(path_, previous_))) return error("midi-file-replace");
            if (!filesystem_.rename(temporary_, path_)) {
                if (existing && !filesystem_.rename(previous_, path_)) return error("midi-file-restore");
                return error("midi-file-publish");
            }
            temporary_[0] = 0; uploading_ = false;
            if (existing && !filesystem_.remove(previous_)) return error("midi-file-cleanup");
            replies_.reply("CNTRL OK"); return true;
        }
        char hex[191] = {}, name[96] = {}; unsigned length = 0, overwrite = 0; unsigned long crc = 0; int end = 0;
        if (std::sscanf(line, "CNTRL MIDI_FILE BEGIN %190s %u %lx %u%n", hex, &length, &crc, &overwrite, &end) == 4
            && end && !line[end] && length && crc <= 0xfffffffful && overwrite <= 1) {
            cancel();
            if (!decode(hex, name)) return error("midi-file-name");
            paths(name);
            if (!filesystem_.available() || !recover()) return error("midi-file-storage");
            if (filesystem_.exists(path_) && !overwrite) return error("midi-file-exists");
            if (!filesystem_.exists("/cntrl/midi") && !filesystem_.make_directory("/cntrl/midi")) return error("midi-file-directory");
            if (!writer_.open(temporary_, files::Mode::truncate)) return error("midi-file-create");
            expected_ = length; received_ = 0; checksum_ = uint32_t(crc); crc_ = 0xffffffff; overwrite_ = overwrite; uploading_ = true;
            replies_.reply("CNTRL SEND"); return true;
        }
        end = 0;
        bool size = std::sscanf(line, "CNTRL MIDI_FILE SIZE %190s%n", hex, &end) == 1 && end && !line[end];
        if (!size) {
            end = 0;
            if (std::sscanf(line, "CNTRL MIDI_FILE READ %190s %u%n", hex, &length, &end) != 2 || !end || line[end]) return error("midi-file-command");
        }
        if (uploading_) return error("midi-file-busy");
        if (!decode(hex, name)) return error("midi-file-name");
        paths(name);
        if (!filesystem_.available() || !recover()) return error("midi-file-storage");
        if (!reader_.open(path_, files::Mode::read)) {
            if (filesystem_.exists(path_)) return error("midi-file-open");
            replies_.reply("CNTRL MIDI_FILE MISSING"); return true;
        }
        if (size) {
            char response[64]; std::snprintf(response, sizeof(response), "CNTRL MIDI_FILE %lu", (unsigned long)reader_.size());
            reader_.close(); replies_.reply(response); return true;
        }
        if (length >= reader_.size()) { reader_.close(); return error("midi-file-offset"); }
        unsigned count = std::min(usb_block_size, unsigned(reader_.size() - length));
        MemoryBuffer bytes;
        bool read = bytes.resize(count) && reader_.read(length, bytes.data, count); reader_.close();
        if (!read) return error("midi-file-read");
        replies_.data(bytes.data, count); return true;
    }
private:
    files::FileSystem &filesystem_; files::File &reader_, &writer_; Replies &replies_;
    char path_[128] = {}, temporary_[128] = {}, previous_[128] = {};
    uint32_t last_ = 0, expected_ = 0, received_ = 0, checksum_ = 0, crc_ = 0;
    bool uploading_ = false, overwrite_ = false;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_error)
#endif
    bool error(const char *message) {
        cancel(); char response[80]; std::snprintf(response, sizeof(response), "CNTRL ERROR %s", message); replies_.reply(response); return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_decode)
#endif
    static bool decode(const char *hex, char (&name)[96]) {
        unsigned length = unsigned(std::strlen(hex));
        if (!length || length > 190 || length % 2) return false;
        for (unsigned i = 0; i < length; i += 2) {
            auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
            int a = digit(hex[i]), b = digit(hex[i + 1]);
            if (a < 0 || b < 0) return false;
            unsigned c = unsigned(a * 16 + b);
            if (c < 32 || c > 126 || c == '/' || c == '\\') return false;
            name[i / 2] = char(c);
        }
        bool reserved = true;
        for (unsigned i = 0; i < 7; ++i) {
            char c = name[i]; if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            if (c != ".cntrl-"[i]) reserved = false;
        }
        return std::strcmp(name, ".") && std::strcmp(name, "..") && !reserved;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_paths)
#endif
    void paths(const char *name) {
        std::snprintf(path_, sizeof(path_), "/cntrl/midi/%s", name);
        std::snprintf(temporary_, sizeof(temporary_), "/cntrl/midi/.cntrl-upload-%s", name);
        std::snprintf(previous_, sizeof(previous_), "/cntrl/midi/.cntrl-previous-%s", name);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_recover)
#endif
    bool recover() {
        if (!filesystem_.exists(previous_)) return true;
        return filesystem_.exists(path_) ? filesystem_.remove(previous_) : filesystem_.rename(previous_, path_);
    }
};
} // namespace cntrl
