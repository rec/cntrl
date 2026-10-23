#pragma once
#include "controller.h"
#include <cstdlib>

namespace cntrl {
struct Replies {
    virtual ~Replies() = default;
    virtual void reply(const char *text) = 0;
};

class Documents {
public:
    Documents(Storage &storage, Hardware &hardware, Controller &controller, Replies &replies)
        : storage_(storage), hardware_(hardware), controller_(controller), replies_(replies) {}

    void boot() {
        // Boot may read both slots; writes occur only after an explicit upload.
        for (unsigned slot = 0; slot < 2; ++slot) {
            uint32_t generation;
            Document candidate;
            if (read_slot(slot, staging_, candidate, generation)
                && (active_slot_ < 0 || int32_t(generation - generation_) > 0)) {
                std::memcpy(active_, staging_, candidate.length());
                document_.load(active_, candidate.length(), hardware_);
                generation_ = generation;
                active_slot_ = int(slot);
            }
        }
        if (document_.valid()) controller_.activate(document_);
        else hardware_.error("No show: upload a document");
    }

    void receive(uint8_t byte, uint32_t now) {
        if (writing_) return; // Host waits for DONE before its next request.
        last_byte_ = now;
        if (receiving_) {
            staging_[received_++] = byte;
            if (received_ == expected_) {
                receiving_ = false;
                Document candidate;
                if (checksum(staging_, expected_) != expected_crc_
                    || !candidate.load(staging_, expected_, hardware_)) {
                    replies_.reply("CNTRL ERROR invalid-document");
                    return;
                }
                slot_ = active_slot_ == 0 ? 1 : 0;
                if (!storage_.begin(slot_)) {
                    replies_.reply("CNTRL ERROR storage-unavailable");
                    return;
                }
                std::memcpy(header_, "CNST", 4);
                put32(header_ + 4, generation_ + 1);
                put32(header_ + 8, expected_);
                put32(header_ + 12, expected_crc_);
                if (!storage_.write(header_, 16)) {
                    storage_.finish();
                    replies_.reply("CNTRL ERROR storage-write");
                    return;
                }
                written_ = 0;
                writing_ = true;
            }
            return;
        }
        if (byte == '\n') {
            line_[line_size_] = 0;
            if (!overflow_) command();
            else replies_.reply("CNTRL ERROR command-too-long");
            line_size_ = 0; overflow_ = false;
        } else if (byte != '\r') {
            if (line_size_ < sizeof(line_) - 1) line_[line_size_++] = char(byte);
            else overflow_ = true;
        }
    }

    void tick(uint32_t now) {
        if ((receiving_ || line_size_ || overflow_) && uint32_t(now - last_byte_) > 10000) {
            receiving_ = false; line_size_ = 0; overflow_ = false;
            replies_.reply("CNTRL ERROR transfer-timeout");
        }
        if (!writing_) return;
        unsigned n = expected_ - written_;
        if (n > 512) n = 512;
        if (n && !storage_.write(staging_ + written_, n)) {
            writing_ = false; storage_.finish();
            replies_.reply("CNTRL ERROR storage-write");
            return;
        }
        written_ += n;
        if (written_ != expected_) return;
        // The commit trailer is last. The old slot remains intact throughout.
        bool committed = storage_.write(reinterpret_cast<const uint8_t *>("DONE"), 4);
        committed = storage_.finish() && committed;
        writing_ = false;
        uint32_t generation;
        Document candidate;
        if (!committed || !read_slot(slot_, staging_, candidate, generation)
            || generation != generation_ + 1 || candidate.length() != expected_
            || checksum(staging_, expected_) != expected_crc_) {
            replies_.reply("CNTRL ERROR storage-verify");
            return;
        }
        std::memcpy(active_, staging_, expected_);
        document_.load(active_, expected_, hardware_);
        active_slot_ = int(slot_); generation_ = generation;
        controller_.activate(document_);
        char response[80];
        std::snprintf(response, sizeof(response), "CNTRL DONE %u %08lx", expected_, (unsigned long)expected_crc_);
        replies_.reply(response);
    }
    bool busy() const { return receiving_ || writing_; }
private:
    Storage &storage_;
    Hardware &hardware_;
    Controller &controller_;
    Replies &replies_;
    uint8_t active_[max_document] = {}, staging_[max_document] = {};
    uint8_t header_[16] = {};
    Document document_;
    int active_slot_ = -1;
    uint32_t generation_ = 0, expected_crc_ = 0, last_byte_ = 0;
    unsigned expected_ = 0, received_ = 0, written_ = 0, slot_ = 0, line_size_ = 0;
    char line_[96] = {};
    bool receiving_ = false, writing_ = false, overflow_ = false;

    bool read_slot(unsigned slot, uint8_t *buffer, Document &document, uint32_t &generation) {
        uint8_t h[16], trailer[4];
        if (!storage_.read(slot, 0, h, 16) || std::memcmp(h, "CNST", 4)) return false;
        unsigned length = u32(h + 8);
        if (length < 24 || length > max_document || storage_.size(slot) != length + 20
            || !storage_.read(slot, 16, buffer, length)
            || !storage_.read(slot, 16 + length, trailer, 4)
            || std::memcmp(trailer, "DONE", 4) || checksum(buffer, length) != u32(h + 12)
            || !document.load(buffer, length, hardware_)) return false;
        generation = u32(h + 4);
        return true;
    }
    void command() {
        if (!std::strcmp(line_, "CNTRL STATUS")) {
            char response[80];
            std::snprintf(response, sizeof(response), "CNTRL READY 4 %u %08lx", document_.length(),
                          document_.valid() ? (unsigned long)checksum(active_, document_.length()) : 0ul);
            replies_.reply(response);
            return;
        }
        unsigned length;
        unsigned long crc;
        int end = 0;
        if (std::sscanf(line_, "CNTRL BEGIN %u %lx%n", &length, &crc, &end) == 2
            && end > 0 && !line_[end] && crc <= 0xfffffffful
            && length >= 24 && length <= max_document) {
            expected_ = length; expected_crc_ = uint32_t(crc); received_ = 0;
            receiving_ = true;
            replies_.reply("CNTRL SEND");
        } else replies_.reply("CNTRL ERROR invalid-command");
    }
};
} // namespace cntrl
