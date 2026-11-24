#pragma once
#include "receiver.h"
#include <cntrl/asset_storage.h>
#include <charconv>
#include <optional>

namespace cntrl::animation {
class MovieLibrary {
public:
    MovieLibrary(AssetStorage &storage, Receiver &receiver, unsigned width, unsigned height, unsigned streams)
        : storage_(storage), receiver_(receiver), width_(width), height_(height), streams_(streams) {}

    void start() { directory_ = storage_.movies(); }
    void close() { directory_.reset(); }
    bool playable(uint32_t key) { return receiver_.playable(key, width_, height_, streams_); }

    // Visit one directory entry per poll so scanning never waits for the full card list.
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(movie_library_step)
#endif
    bool step(std::optional<uint32_t> &key) {
        key.reset();
        if (!directory_) return true;
        char name[256];
        bool folder;
        if (!directory_->next(name, sizeof(name), folder)) { close(); return true; }
        if (!folder && std::strlen(name) == 12 && !std::strcmp(name + 8, ".bin")) {
            uint32_t number;
            const auto parsed = std::from_chars(name, name + 8, number, 16);
            if (parsed.ec == std::errc{} && parsed.ptr == name + 8 && playable(number)) key = number;
        }
        return false;
    }
private:
    AssetStorage &storage_;
    Receiver &receiver_;
    unsigned width_, height_, streams_;
    std::unique_ptr<files::Directory> directory_;
};
} // namespace cntrl::animation
