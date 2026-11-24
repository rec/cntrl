#pragma once
#include <cntrl/binary_format.h>
#include <cntrl/filesystem.h>

namespace cntrl::ui {
// A record only increases, so the highest valid slot also identifies which
// slot to preserve while writing the next record.
class MemonScore {
public:
    void attach(files::FileSystem &filesystem) { filesystem_ = &filesystem; }
    unsigned high() const { return best_; }
    void observe(uint32_t score) { best_ = std::max(best_, score); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(memon_score_load)
#endif
    bool load() {
        if (loaded_) return true;
        if (!filesystem_ || !filesystem_->available()) return false;
        bool present = false, valid = false;
        for (unsigned i = 0; i < 2; ++i) {
            if (!filesystem_->exists(paths[i])) continue;
            present = true;
            uint32_t score;
            if (!read(i, score)) continue;
            valid = true;
            if (score > saved_) { saved_ = score; next_ = 1 - i; }
        }
        observe(saved_);
        loaded_ = !present || valid;
        return loaded_;
    }

#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(memon_score_save)
#endif
    bool save() {
        if (!loaded_ && !load()) return false;
        if (best_ <= saved_) return true;
        uint8_t bytes[12] = {'M', 'E', 'M', '1'};
        binary::write32(bytes + 4, best_);
        binary::write32(bytes + 8, binary::crc(bytes, 8));
        auto file = filesystem_->file();
        if (!file || !file->open(paths[next_], files::Mode::truncate)
            || !file->write(0, bytes, sizeof(bytes)) || !file->sync()) return false;
        file->close();
        uint32_t checked;
        if (!read(next_, checked) || checked != best_) return false;
        saved_ = best_; next_ = 1 - next_;
        return true;
    }

private:
    static constexpr const char *paths[2] = {"/cntrl/memon-0.bin", "/cntrl/memon-1.bin"};
    files::FileSystem *filesystem_ = nullptr;
    uint32_t best_ = 0, saved_ = 0;
    unsigned next_ = 0;
    bool loaded_ = false;

#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(memon_score_read)
#endif
    bool read(unsigned slot, uint32_t &score) {
        auto file = filesystem_->file();
        uint8_t bytes[12];
        if (!file || !file->open(paths[slot], files::Mode::read) || file->size() != sizeof(bytes)
            || !file->read(0, bytes, sizeof(bytes)) || std::memcmp(bytes, "MEM1", 4)
            || binary::read32(bytes + 8) != binary::crc(bytes, 8)) return false;
        score = binary::read32(bytes + 4);
        return true;
    }
};
} // namespace cntrl::ui
