#pragma once
#include "binary_format.h"
#include "filesystem.h"
#include <cstdio>
#include <limits>
#include <initializer_list>

namespace cntrl {
// Only coordinated mutable stores change roots. Content-addressed assets,
// recordings and the monotonic startup counter remain shared across upgrades.
inline uint32_t persistent_root = 0;
#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(persistent_path)
#endif
inline bool persistent_path(const char *path, char (&mapped)[224]) {
    bool mutable_store = false;
    for (const char *store : {"/cntrl/setups", "/cntrl/history", "/cntrl/songs", "/cntrl/selection"}) {
        const unsigned n = unsigned(std::strlen(store));
        if (!std::strncmp(path, store, n) && (!path[n] || path[n] == '/')) mutable_store = true;
    }
    int size = persistent_root && mutable_store
        ? std::snprintf(mapped, sizeof(mapped), "/cntrl/.generations/%08lx%s", (unsigned long)persistent_root, path + 6)
        : std::snprintf(mapped, sizeof(mapped), "%s", path);
    return size >= 0 && unsigned(size) < sizeof(mapped);
}

class Deployment {
public:
    explicit Deployment(files::FileSystem &filesystem) : filesystem_(filesystem) {}
    bool pending() const { return pending_; }
    bool ready() const { return ready_; }
    uint32_t root() const { return active_; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(deployment_boot)
#endif
    bool boot() {
        pending_ = false; ready_ = false; active_ = sequence_ = persistent_root = 0;
        bool found = false, present = false;
        for (unsigned slot = 0; slot < 2; ++slot) {
            char path[64]; filename(slot, path);
            if (!filesystem_.exists(path)) continue;
            present = true; auto file = filesystem_.file(); uint8_t bytes[24];
            if (!file || !file->open(path, files::Mode::read) || file->size() != sizeof(bytes)
                || !file->read(0, bytes, sizeof(bytes))) continue;
            file->close();
            if (std::memcmp(bytes, "CDEP", 4) || std::memcmp(bytes + 20, "DONE", 4)
                || binary::crc(bytes, 16) != binary::read32(bytes + 16)) continue;
            const uint32_t sequence = binary::read32(bytes + 4), root = binary::read32(bytes + 8);
            if (!sequence || binary::read16(bytes + 14)) continue;
            if (!found || sequence > sequence_) {
                sequence_ = sequence; active_ = root; format_ = binary::read16(bytes + 12); found = true;
            }
        }
        if ((present && !found) || (found && format_ != binary::development_format)) return false;
        persistent_root = active_; ready_ = true; return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(deployment_begin)
#endif
    bool begin(uint32_t root) {
        if (!ready_ || pending_ || !root || root == active_ || sequence_ == std::numeric_limits<uint32_t>::max()) return false;
        // Give the legacy root its own committed record before staging starts.
        if (!sequence_ && !publish(active_)) return false;
        char path[96]; std::snprintf(path, sizeof(path), "/cntrl/.generations/%08lx", (unsigned long)root);
        if (filesystem_.exists(path) || !filesystem_.make_directory(path)) return false;
        for (const char *store : {"setups", "history", "songs", "selection"}) {
            std::snprintf(path, sizeof(path), "/cntrl/.generations/%08lx/%s", (unsigned long)root, store);
            if (!filesystem_.make_directory(path)) return false;
        }
        staged_ = root; persistent_root = root; pending_ = true; return true;
    }
    bool commit() {
        if (!pending_ || !ready_ || !publish(staged_)) return false;
        pending_ = false; return true;
    }
    bool recover(uint32_t root) {
        if (pending_ || sequence_ == std::numeric_limits<uint32_t>::max()) return false;
        char path[96]; completion_path(root, path);
        uint8_t record[24]; auto file = filesystem_.file();
        if (!file || !file->open(path, files::Mode::read) || file->size() != sizeof(record)
            || !file->read(0, record, sizeof(record)) || std::memcmp(record, "CDEP", 4)
            || std::memcmp(record + 20, "DONE", 4) || binary::read32(record + 8) != root
            || binary::read16(record + 12) != binary::development_format || binary::read16(record + 14)
            || !binary::read32(record + 4) || binary::crc(record, 16) != binary::read32(record + 16)) return false;
        file->close();
        staged_ = persistent_root = root; pending_ = ready_ = true; return true;
    }
    void abort() { boot(); }
private:
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(deployment_publish)
#endif
    bool publish(uint32_t root) {
        if (!filesystem_.make_directory("/cntrl/.deploy")) return false;
        uint8_t bytes[24] = {};
        std::memcpy(bytes, "CDEP", 4); binary::write32(bytes + 4, sequence_ + 1);
        binary::write32(bytes + 8, root);
        bytes[12] = uint8_t(binary::development_format & 0xff); bytes[13] = uint8_t(binary::development_format >> 8);
        binary::write32(bytes + 16, binary::crc(bytes, 16)); std::memcpy(bytes + 20, "DONE", 4);
        // Retained roots need proof of a completed stage even after both
        // activation slots have advanced. Never recover an abandoned partial root.
        char directory[96], completed[96];
        std::snprintf(directory, sizeof(directory), "/cntrl/.generations/%08lx", (unsigned long)root);
        completion_path(root, completed);
        if (!filesystem_.exists(completed)) {
            if (!filesystem_.make_directory(directory) || !write_record(completed, bytes)) return false;
        }
        char path[64]; filename((sequence_ + 1) % 2, path);
        if (!write_record(path, bytes)) return false;
        active_ = root; ++sequence_; return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(deployment_write_record)
#endif
    bool write_record(const char *path, const uint8_t (&bytes)[24]) {
        auto file = filesystem_.file();
        if (!file || !file->open(path, files::Mode::truncate) || !file->write(0, bytes, sizeof(bytes)) || !file->sync()) return false;
        file->close(); uint8_t verify[24];
        if (!file->open(path, files::Mode::read) || file->size() != sizeof(verify)
            || !file->read(0, verify, sizeof(verify)) || std::memcmp(bytes, verify, sizeof(bytes))) return false;
        file->close(); return true;
    }
    files::FileSystem &filesystem_;
    uint32_t active_ = 0, staged_ = 0, sequence_ = 0;
    uint16_t format_ = 0;
    bool pending_ = false, ready_ = false;
    static void filename(unsigned slot, char (&path)[64]) {
        std::snprintf(path, sizeof(path), "/cntrl/.deploy/%u.bin", slot);
    }
    static void completion_path(uint32_t root, char (&path)[96]) {
        std::snprintf(path, sizeof(path), "/cntrl/.generations/%08lx/.committed", (unsigned long)root);
    }
};
} // namespace cntrl
