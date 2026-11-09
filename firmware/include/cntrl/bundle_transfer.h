#pragma once
#include <cntrl/usb_config.h>
#include "bundled_storage.h"
#include "documents.h"

namespace cntrl {
class BundleTransfer {
public:
    BundleTransfer(BundledStorage &storage, Documents &documents, Hardware &hardware, Replies &replies)
        : storage_(storage), documents_(documents), hardware_(hardware), replies_(replies) {}
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(bundle_data)
#endif
    bool data(const uint8_t *bytes, unsigned length, uint32_t now) {
        if (!active_ || !length || length > usb_block_size || length > expected_ - received_) return fail("bundle-data");
        if (!storage_.upload_write(bytes, length)) return fail("bundle-write");
        for (unsigned i = 0; i < length; ++i) {
            crc_ ^= bytes[i]; for (unsigned b = 0; b < 8; ++b) crc_ = crc_ & 1 ? (crc_ >> 1) ^ 0xedb88320 : crc_ >> 1;
        }
        received_ += length; last_ = now; replies_.reply("CNTRL OK"); return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(bundle_command)
#endif
    bool command(const char *line, uint32_t now) {
        if (std::strncmp(line, "CNTRL BUNDLE ", 13)) return false;
        unsigned length = 0, overwrite = 0, offset = 0; unsigned long crc = 0; int end = 0;
        if (!std::strcmp(line, "CNTRL BUNDLE SIZE")) {
            char response[80]; std::snprintf(response, sizeof(response), "CNTRL BUNDLE %u %08lx",
                storage_.bundle_size(), (unsigned long)storage_.bundle_crc()); replies_.reply(response); return true;
        }
        if (std::sscanf(line, "CNTRL BUNDLE GET %u %u%n", &offset, &length, &end) == 2 && !line[end]
            && length && length <= usb_block_size) {
            MemoryBuffer buffer; if (!buffer.resize(length)) return error("allocation");
            if (!storage_.bundle_read(offset, buffer.data, length)) return error("bundle-read");
            replies_.data(buffer.data, length); return true;
        }
        if (std::sscanf(line, "CNTRL BUNDLE BEGIN %u %lx %u%n", &length, &crc, &overwrite, &end) == 3
            && !line[end] && length >= 40 && length <= 0xffffffffu - 100 && crc <= 0xfffffffful && overwrite <= 1) {
            if (active_ || documents_.busy() || documents_.dirty() || hardware_.configuration_locked()) return error("bundle-busy");
            bool present = false;
            for (unsigned i = 0; i < storage_.setups().count(); ++i) present = present || storage_.setups().length(i);
            if (present && !overwrite) return error("bundle-exists-use--overwrite");
            if (!storage_.upload_begin(length, uint32_t(crc), 0)) return error("bundle-write");
            expected_ = length; expected_crc_ = uint32_t(crc); received_ = 0; crc_ = 0xffffffff;
            active_ = true; last_ = now; replies_.reply("CNTRL SEND"); return true;
        }
        if (!std::strcmp(line, "CNTRL BUNDLE END")) {
            if (!active_) return error("no-bundle-transfer");
            if (hardware_.configuration_locked()) return fail("bundle-busy");
            if (received_ != expected_ || ~crc_ != expected_crc_) return fail("bundle-checksum");
            active_ = false;
            if (!storage_.upload_finish()) return error("invalid-bundle");
            documents_.boot(); replies_.reply("CNTRL OK"); return true;
        }
        return error("bundle-command");
    }
    void keepalive(uint32_t now) { if (active_) last_ = now; }
    void poll(uint32_t now) { if (active_ && uint32_t(now - last_) > 10000) fail("bundle-timeout"); }
    void cancel() { if (active_) storage_.upload_cancel(); active_ = false; }
private:
    BundledStorage &storage_; Documents &documents_; Hardware &hardware_; Replies &replies_;
    bool active_ = false;
    unsigned expected_ = 0, received_ = 0;
    uint32_t crc_ = 0xffffffff, expected_crc_ = 0, last_ = 0;
    bool error(const char *message) { char response[80]; std::snprintf(response, sizeof(response), "CNTRL ERROR %s", message); replies_.reply(response); return true; }
    bool fail(const char *message) { cancel(); return error(message); }
};
} // namespace cntrl
