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
            UsbBlock block; if (!block.bytes) return error("allocation");
            auto *bytes = block.bytes; char *response = block.text;
            std::strcpy(response, "CNTRL BUNDLE ");
            if (!storage_.bundle_read(offset, bytes, length)) return error("bundle-read");
            for (unsigned i = 0; i < length; ++i) std::snprintf(response + 13 + i * 2, 3, "%02x", bytes[i]);
            replies_.reply(response); return true;
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
        if (!std::strncmp(line, "CNTRL BUNDLE DATA ", 18)) {
            if (!active_) return error("no-bundle-transfer");
            const char *hex = line + 18; unsigned width = unsigned(std::strlen(hex));
            UsbBlock block; if (!block.bytes) return fail("allocation");
            auto *bytes = block.bytes;
            if (!width || width > usb_block_size * 2 || width % 2 || width / 2 > expected_ - received_) return fail("bundle-data");
            for (unsigned i = 0; i < width; i += 2) {
                unsigned value = 0;
                for (unsigned j = 0; j < 2; ++j) {
                    char c = hex[i + j];
                    if (c >= '0' && c <= '9') value = value * 16 + unsigned(c - '0');
                    else if (c >= 'a' && c <= 'f') value = value * 16 + unsigned(c - 'a' + 10);
                    else return fail("bundle-data");
                }
                bytes[i / 2] = uint8_t(value);
            }
            unsigned n = width / 2;
            if (!storage_.upload_write(bytes, n)) return fail("bundle-write");
            for (unsigned i = 0; i < n; ++i) { crc_ ^= bytes[i]; for (unsigned b = 0; b < 8; ++b) crc_ = crc_ & 1 ? (crc_ >> 1) ^ 0xedb88320 : crc_ >> 1; }
            received_ += n; last_ = now; replies_.reply("CNTRL OK"); return true;
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
