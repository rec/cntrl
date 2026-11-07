#pragma once
#include "documents.h"

namespace cntrl {
// A converted snapshot is published in slot 1. Slot 0 and the revision directory
// remain untouched, including after interruption or a conflicting retry.
class HistoryImport {
public:
    HistoryImport(SetupStorage &storage, Documents &documents, Hardware &hardware, Replies &replies)
        : storage_(storage), documents_(documents), hardware_(hardware), replies_(replies) {}
    bool busy() const { return phase_ != Phase::idle; }
    void keepalive(uint32_t now) { if (busy()) last_ = now; }
    void cancel() {
        if (writing_) storage_.finish();
        writing_ = false; phase_ = Phase::idle; data_.clear();
    }
    bool command(const char *line, uint32_t now) {
        unsigned length = 0; unsigned long crc = 0; int end = 0;
        char encoded[127], number[21];
        if (!std::strncmp(line, "CNTRL HISTORY RESERVE ", 22)) {
            if (busy() || documents_.busy() || documents_.dirty() || hardware_.configuration_locked()) return error("history-import-busy");
            if (std::sscanf(line, "CNTRL HISTORY RESERVE %126s %20s%n", encoded, number, &end) != 2
                || !end || line[end] || !decode_name(encoded) || !parse_revision(number, revision_)
                || !revision_ || revision_ == UINT64_MAX) return error("history-reserve-header");
            storage_.choose_revision(name_, revision_);
            if (storage_.size(0) || storage_.size(1)) { replies_.reply("CNTRL EXISTS"); return true; }
            if (!storage_.begin(1) || !storage_.finish()) return error("history-reserve-write");
            replies_.reply("CNTRL OK"); return true;
        }
        if (!std::strncmp(line, "CNTRL HISTORY IMPORT ", 21)) {
            if (busy() || documents_.busy() || documents_.dirty() || hardware_.configuration_locked()) return error("history-import-busy");
            if (std::sscanf(line, "CNTRL HISTORY IMPORT %126s %20s %u %lx%n", encoded, number, &length, &crc, &end) != 4
                || !end || line[end] || !decode_name(encoded) || !parse_revision(number, revision_)
                || !revision_ || revision_ == UINT64_MAX || crc > 0xfffffffful
                || length < 208 || length > memory_budget || !data_.resize(length)) return fail("history-import-header");
            expected_ = length; expected_crc_ = uint32_t(crc); received_ = 0;
            phase_ = Phase::receive; last_ = progress_ = now; replies_.reply("CNTRL SEND"); return true;
        }
        if (!std::strncmp(line, "CNTRL HISTORY DATA ", 19)) {
            if (phase_ != Phase::receive) return fail("history-import-state");
            const char *hex = line + 19; unsigned width = unsigned(std::strlen(hex));
            if (!width || width % 2 || width > 256 || width / 2 > expected_ - received_) return fail("history-import-data");
            for (unsigned i = 0; i < width; i += 2) {
                int high = digit(hex[i]), low = digit(hex[i + 1]);
                if (high < 0 || low < 0) return fail("history-import-data");
                data_.data[received_++] = uint8_t(high * 16 + low);
            }
            last_ = now; replies_.reply("CNTRL OK"); return true;
        }
        if (!std::strcmp(line, "CNTRL HISTORY END")) {
            if (phase_ != Phase::receive || received_ != expected_ || checksum(data_.data, expected_) != expected_crc_
                || !valid_record()) return fail("history-import-record");
            storage_.choose_revision(name_, revision_); offset_ = 0;
            existing_length_ = storage_.size(1);
            phase_ = existing_length_ >= 208 ? Phase::existing_header : Phase::begin;
            last_ = progress_ = now; return true;
        }
        return false;
    }
    void tick(uint32_t now) {
        if (!busy()) return;
        if (uint32_t(now - last_) > 10000) { fail("history-import-timeout"); return; }
        if (phase_ == Phase::receive) return;
        if (uint32_t(now - progress_) >= 500) { replies_.reply("CNTRL WAIT"); progress_ = now; }
        uint8_t block[512]; unsigned n = std::min(512u, expected_ - offset_);
        switch (phase_) {
        case Phase::idle: case Phase::receive: return;
        case Phase::existing_header: {
            uint8_t header[88];
            if (!storage_.read(1, 0, header, sizeof(header))) { fail("history-import-read"); return; }
            unsigned names = u32(header + 16), length = u32(header + 4);
            uint64_t body = 88ull + uint64_t(names) * 64;
            if (std::memcmp(header, "CREV", 4) || names > 65535 || length < 116
                || body + length + 4 != existing_length_ || checksum(header, 84) != u32(header + 84)
                || (header[20] ? !text_valid(header + 20, 64) : !all_zero(header + 20, 64))) {
                phase_ = Phase::begin; return;
            }
            existing_body_ = unsigned(body); existing_expected_crc_ = u32(header + 8);
            phase_ = Phase::existing_version; return;
        }
        case Phase::existing_version:
            if (!storage_.read(1, existing_body_, block, 6)) { fail("history-import-read"); return; }
            if (std::memcmp(block, "CSW1", 4) || u16(block + 4) != document_version) { phase_ = Phase::begin; return; }
            existing_offset_ = 88; existing_crc_ = 0xffffffff; phase_ = Phase::existing_payload; return;
        case Phase::existing_payload:
            n = std::min(512u, existing_length_ - 4 - existing_offset_);
            if (!storage_.read(1, existing_offset_, block, n)) { fail("history-import-read"); return; }
            for (unsigned i = 0; i < n; ++i) {
                existing_crc_ ^= block[i];
                for (unsigned b = 0; b < 8; ++b) existing_crc_ = existing_crc_ & 1 ? (existing_crc_ >> 1) ^ 0xedb88320u : existing_crc_ >> 1;
            }
            existing_offset_ += n;
            if (existing_offset_ == existing_length_ - 4) phase_ = Phase::existing_end;
            return;
        case Phase::existing_end:
            if (!storage_.read(1, existing_length_ - 4, block, 4)) { fail("history-import-read"); return; }
            if (std::memcmp(block, "DONE", 4) || ~existing_crc_ != existing_expected_crc_) { phase_ = Phase::begin; return; }
            if (existing_length_ != expected_) { fail("history-import-conflict"); return; }
            phase_ = Phase::compare; return;
        case Phase::compare:
            if (!storage_.read(1, offset_, block, n) || std::memcmp(block, data_.data + offset_, n)) { fail("history-import-conflict"); return; }
            offset_ += n;
            if (offset_ == expected_) { cancel(); replies_.reply("CNTRL EXISTS"); }
            return;
        case Phase::begin:
            if (!storage_.begin(1)) { fail("history-import-open"); return; }
            writing_ = true; phase_ = Phase::write; return;
        case Phase::write:
            if (!storage_.write(data_.data + offset_, n)) { fail("history-import-write"); return; }
            offset_ += n; if (offset_ == expected_) phase_ = Phase::close; return;
        case Phase::close: {
            bool good = storage_.finish(); writing_ = false;
            if (!good || storage_.size(1) != expected_) { fail("history-import-sync"); return; }
            offset_ = 0; phase_ = Phase::verify; return;
        }
        case Phase::verify:
            if (!storage_.read(1, offset_, block, n) || std::memcmp(block, data_.data + offset_, n)) { fail("history-import-verify"); return; }
            offset_ += n;
            if (offset_ == expected_) { cancel(); replies_.reply("CNTRL OK"); }
            return;
        }
    }
private:
    enum class Phase { idle, receive, existing_header, existing_version, existing_payload, existing_end, compare, begin, write, close, verify };
    SetupStorage &storage_; Documents &documents_; Hardware &hardware_; Replies &replies_;
    MemoryBuffer data_;
    Phase phase_ = Phase::idle;
    char name_[64] = {};
    uint64_t revision_ = 0;
    unsigned expected_ = 0, received_ = 0, offset_ = 0;
    unsigned existing_length_ = 0, existing_body_ = 0, existing_offset_ = 0;
    uint32_t existing_crc_ = 0, existing_expected_crc_ = 0;
    uint32_t expected_crc_ = 0, last_ = 0, progress_ = 0;
    bool writing_ = false;
    static int digit(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }
    bool decode_name(const char *hex) {
        unsigned n = unsigned(std::strlen(hex)); if (!n || n % 2 || n > 126) return false;
        std::memset(name_, 0, sizeof(name_));
        for (unsigned i = 0; i < n; i += 2) {
            int high = digit(hex[i]), low = digit(hex[i + 1]); if (high < 0 || low < 0) return false;
            name_[i / 2] = char(high * 16 + low);
        }
        return std::strlen(name_) == n / 2 && setup_name(name_);
    }
    bool valid_record() const {
        const uint8_t *p = data_.data; unsigned names = u32(p + 16), length = u32(p + 4);
        uint64_t body = 88ull + uint64_t(names) * 64;
        if (std::memcmp(p, "CREV", 4) || names > 65535 || length < 116 || body + length + 4 != expected_
            || checksum(p, 84) != u32(p + 84) || std::memcmp(p + expected_ - 4, "DONE", 4)
            || checksum(p + 88, expected_ - 92) != u32(p + 8)) return false;
        if (p[20] ? !text_valid(p + 20, 64) : !all_zero(p + 20, 64)) return false;
        for (unsigned i = 0; i < names; ++i) {
            const auto *name = p + 88 + i * 64;
            if (!text_valid(name, 64) || !setup_name(reinterpret_cast<const char *>(name))) return false;
        }
        Document document;
        return document.load(p + unsigned(body), length, hardware_);
    }
    static bool all_zero(const uint8_t *p, unsigned n) { for (unsigned i = 0; i < n; ++i) if (p[i]) return false; return true; }
    bool error(const char *message) {
        char response[96]; std::snprintf(response, sizeof(response), "CNTRL ERROR %s", message); replies_.reply(response); return true;
    }
    bool fail(const char *message) { cancel(); return error(message); }
};
} // namespace cntrl
