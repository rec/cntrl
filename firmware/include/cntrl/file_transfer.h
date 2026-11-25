#pragma once
#include "documents.h"
#include "filesystem.h"
#include "usb_frame.h"
#include "usb_config.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace cntrl {
// Named assets use independent MIDI/source namespaces. Publication uses
// a synced temporary file; a failed replacement retains/restores the old file.
class FileTransfer {
public:
    enum class Namespace { midi, source, language, doom };
    FileTransfer(files::FileSystem &filesystem, files::File &reader,
                 files::File &writer, Replies &replies, Namespace space = Namespace::midi)
        : filesystem_(filesystem), reader_(reader), writer_(writer), replies_(replies), space_(space) {}
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
        const char *prefix = space_ == Namespace::source ? "CNTRL SOURCE " : space_ == Namespace::language ? "CNTRL LANGUAGE " : space_ == Namespace::doom ? "CNTRL DOOM_ASSET " : "CNTRL MIDI_FILE ";
        const unsigned prefix_length = unsigned(std::strlen(prefix));
        if (std::strncmp(line, prefix, prefix_length)) return false;
        line += prefix_length;
        last_ = now;
        if (!std::strcmp(line, "END")) {
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
            const bool cleanup_pending = existing && !filesystem_.remove(previous_);
            replies_.reply(cleanup_pending ? "CNTRL OK CLEANUP_PENDING" : "CNTRL OK"); return true;
        }
        char hex[191] = {}, name[96] = {}; unsigned length = 0, overwrite = 0; unsigned long crc = 0; int end = 0;
        if (std::sscanf(line, "BEGIN %190s %u %lx %u%n", hex, &length, &crc, &overwrite, &end) == 4
            && end && !line[end] && length && crc <= 0xfffffffful && overwrite <= 1) {
            cancel();
            if (!decode(hex, name) || (space_ == Namespace::source && (overwrite || !source_name(name)))
                || (space_ == Namespace::doom && (std::strcmp(name, "e1m1.img") || length != 4 * 1024 * 1024))
                || (space_ == Namespace::language && (!language_name(name) || length > 262144))) return error("midi-file-name");
            paths(name);
            if (!filesystem_.available() || !recover()) return error("midi-file-storage");
            if (filesystem_.exists(path_) && !overwrite) return error("midi-file-exists");
            const char *directory = folder();
            if (!filesystem_.exists(directory) && !filesystem_.make_directory(directory)) return error("midi-file-directory");
            if (!writer_.open(temporary_, files::Mode::truncate)) return error("midi-file-create");
            expected_ = length; received_ = 0; checksum_ = uint32_t(crc); crc_ = 0xffffffff; overwrite_ = overwrite; uploading_ = true;
            replies_.reply("CNTRL SEND"); return true;
        }
        end = 0;
        bool size = std::sscanf(line, "SIZE %190s%n", hex, &end) == 1 && end && !line[end];
        if (!size) {
            end = 0;
            if (std::sscanf(line, "READ %190s %u%n", hex, &length, &end) != 2 || !end || line[end]) return error("midi-file-command");
        }
        if (uploading_) return error("midi-file-busy");
        if (!decode(hex, name) || (space_ == Namespace::source && !source_name(name))
            || (space_ == Namespace::language && !language_name(name))
            || (space_ == Namespace::doom && std::strcmp(name, "e1m1.img"))) return error("midi-file-name");
        paths(name);
        if (!filesystem_.available() || !recover(false)) return error("midi-file-storage");
        if (!reader_.open(path_, files::Mode::read)) {
            if (filesystem_.exists(path_)) return error("midi-file-open");
            char response[40]; std::snprintf(response, sizeof(response), "%sMISSING", prefix);
            replies_.reply(response); return true;
        }
        if (size) {
            char response[64]; std::snprintf(response, sizeof(response), "%s%lu", prefix, (unsigned long)reader_.size());
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
    Namespace space_;
    const char *folder() const { return space_ == Namespace::source ? "/cntrl/source" : space_ == Namespace::language ? "/cntrl/languages" : space_ == Namespace::doom ? "/cntrl/doom" : "/cntrl/midi"; }
    static bool language_name(const char *name) {
        const unsigned length = unsigned(std::strlen(name));
        if (length < 5 || length > 19 || std::strcmp(name + length - 4, ".bin")) return false;
        char tag[16] = {}; std::memcpy(tag, name, length - 4);
        return GlobalSettings::language_valid(tag);
    }
    static bool source_name(const char *name) {
        if (std::strlen(name) != 68 || std::strcmp(name + 64, ".bin")) return false;
        for (unsigned i = 0; i < 64; ++i)
            if (!(name[i] >= '0' && name[i] <= '9') && !(name[i] >= 'a' && name[i] <= 'f')) return false;
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_error)
#endif
    bool error(const char *message) {
        cancel(); char response[80]; std::snprintf(response, sizeof(response), "CNTRL ERROR %s%s", space_ == Namespace::source ? "source-" : space_ == Namespace::language ? "language-" : space_ == Namespace::doom ? "doom-asset-" : "midi-file-", message + 10); replies_.reply(response); return true;
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
        const char *directory = folder();
        std::snprintf(path_, sizeof(path_), "%s/%s", directory, name);
        std::snprintf(temporary_, sizeof(temporary_), "%s/.cntrl-upload-%s", directory, name);
        std::snprintf(previous_, sizeof(previous_), "%s/.cntrl-previous-%s", directory, name);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_recover)
#endif
    bool recover(bool require_cleanup = true) {
        if (!filesystem_.exists(previous_)) return true;
        if (filesystem_.exists(path_)) return filesystem_.remove(previous_) || !require_cleanup;
        return filesystem_.rename(previous_, path_);
    }
};
} // namespace cntrl
