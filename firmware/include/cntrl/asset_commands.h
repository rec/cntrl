#pragma once
#include "startup_counter.h"
#include "usb_frame.h"
#include <cstdio>
#include <cstring>

namespace cntrl {

// Services binds hardware, replies, counter/file, recorder, history, bundle,
// listing, disk, song_editor, songs, fonts, and an optional native_extension callback.
template<class Services>
class AssetCommands {
public:
    explicit AssetCommands(Services &services) : services_(services) {}
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(asset_data)
#endif
    bool data(UsbKind kind, const uint8_t *bytes, unsigned length, uint32_t now) {
        auto &s = services_;
        if (s.hardware.configuration_locked()) { s.replies.reply("CNTRL ERROR close-switches-first"); return true; }
        switch (kind) {
        case UsbKind::history: return s.history.data(bytes, length, now);
        case UsbKind::bundle: return s.bundle.data(bytes, length, now);
        case UsbKind::disk: return s.disk.data(bytes, length, now);
        case UsbKind::songs: return s.songs.data(bytes, length, now);
        case UsbKind::font: return s.fonts.data(bytes, length, s.replies, now);
        default: return false;
        }
    }

#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(asset_commands)
#endif
    bool command(const char *line, uint32_t now) {
        auto &s = services_;
        if (s.history.command(line, now)) return true;
        if (s.history.busy()) { s.replies.reply("CNTRL ERROR history-import-busy"); return true; }
        if (!std::strcmp(line, "CNTRL MEMORY")) {
            char response[80];
            std::snprintf(response, sizeof(response), "CNTRL MEMORY %u %u", s.hardware.memory_capacity(), s.hardware.memory_runtime());
            s.replies.reply(response); return true;
        }
        if (!std::strcmp(line, "CNTRL STARTS RESET")) {
            if (s.recorder.active()) { s.replies.reply("CNTRL ERROR starts-recording"); return true; }
            s.counter_file.open(StartupCounter::path, files::Mode::create);
            bool reset = s.counter.reset(s.counter_file); s.counter_file.close();
            if (reset) s.replies.reply("CNTRL OK");
            else {
                char response[80]; std::snprintf(response, sizeof(response), "CNTRL ERROR starts-reset %s", s.counter.error);
                s.replies.reply(response);
            }
            return true;
        }
        if (!std::strncmp(line, "CNTRL STARTS ", 13)) {
            s.counter_file.open(StartupCounter::path, files::Mode::read);
            StartupCounter::read_command(line, s.counter_file, s.replies);
            s.counter_file.close(); return true;
        }
        if (!std::strncmp(line, "CNTRL BUNDLE ", 13)) return s.bundle.command(line, now);
        if (!std::strcmp(line, "CNTRL STORAGE NEXT")) {
            char response[600]; s.listing.next(response, sizeof(response)); s.replies.reply(response); return true;
        }
        if (s.hardware.configuration_locked()) { s.replies.reply("CNTRL ERROR close-switches-first"); return true; }
        if (!std::strncmp(line, "CNTRL DISK ", 11)) return s.disk.command(line, now);
        if (s.native_extension && s.native_extension(line)) return true;
        if (!std::strncmp(line, "CNTRL SONGS ", 12)) {
            if (s.song_editor.open()) { s.replies.reply("CNTRL ERROR close-song-editor"); return true; }
            return s.songs.command(line, now);
        }
        return s.fonts.command(line, s.replies, now);
    }

private:
    Services &services_;
};

} // namespace cntrl
