#pragma once
#include "document.h"
#include "setup_names.h"
#include <cstdio>

namespace cntrl {
// The target's nano C library does not support long-long scanf conversions.
inline bool parse_revision(const char *text, uint64_t &number) {
    if (!*text) return false;
    number = 0;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        unsigned digit = unsigned(*p - '0');
        if (number > (UINT64_MAX - digit) / 10) return false;
        number = number * 10 + digit;
    }
    return true;
}
inline void revision_text(uint64_t number, char *text) {
    char digits[20]; unsigned n = 0;
    do { digits[n++] = char('0' + number % 10); number /= 10; } while (number);
    for (unsigned i = 0; i < n; ++i) text[i] = digits[n - i - 1];
    text[n] = 0;
}
struct Revision {
    uint64_t number = 0;
    uint32_t modified = 0, length = 0;
    char label[64] = {};
};

// One directory entry or at most 512 file bytes per tick. Buffers stay owned by
// Documents until completion; no history-size-dependent allocation or loops.
class Revisions {
public:
    explicit Revisions(SetupStorage &storage) : storage_(storage) {}
    bool busy() const { return phase_ != Phase::idle; }
    bool success() const { return good_; }
    const Revision &info() const { return info_; }
    void read(const char *name, uint64_t number, uint8_t *data) {
        initialize(name, data); info_.number = number; operation_ = Operation::read;
        storage_.choose_revision(name_, number); phase_ = Phase::header;
    }
    void current(const char *name, unsigned slot, uint8_t *data) {
        initialize(name, data); operation_ = Operation::current; slot_ = slot;
        storage_.choose(name_); phase_ = Phase::header;
    }
    void next(const char *name, uint64_t before, uint8_t *data) {
        initialize(name, data); operation_ = Operation::next; scan(before);
    }
    void append(const char *name, const char *label, uint32_t modified, uint8_t *data, unsigned length) {
        initialize(name, data); operation_ = Operation::append;
        info_.length = length; info_.modified = modified;
        std::snprintf(info_.label, sizeof(info_.label), "%s", label);
        phase_ = Phase::hash;
    }
    void erase(const char *name, uint64_t number) {
        initialize(name, nullptr); operation_ = number ? Operation::erase : Operation::clear;
        info_.number = number;
        if (number) phase_ = Phase::remove;
        else { scan(UINT64_MAX); phase_ = Phase::clear_scan; }
    }
    void tick() {
        uint8_t block[512];
        switch (phase_) {
        case Phase::idle: return;
        case Phase::clear_scan:
            best_ = 0;
            if (storage_.revision_scan_step(best_)) { finish(true); return; }
            if (best_) { info_.number = best_; phase_ = Phase::remove; }
            return;
        case Phase::scan:
            if (!storage_.revision_scan_step(best_)) return;
            if (operation_ == Operation::append) {
                if (best_ >= UINT64_MAX - 1) { finish(false); return; }
                info_.number = best_ + 1;
                storage_.choose_revision(name_, info_.number); phase_ = Phase::begin;
            } else if (!best_) finish(operation_ == Operation::clear);
            else {
                info_.number = best_;
                if (operation_ == Operation::clear) phase_ = Phase::remove;
                else { storage_.choose_revision(name_, best_); phase_ = Phase::header; }
            }
            return;
        case Phase::header: {
            unsigned width = operation_ == Operation::current ? 96 : 88;
            if (!storage_.read(slot_, 0, header_, width)) { invalid(); return; }
            bool current = operation_ == Operation::current;
            unsigned length_offset = current ? 8 : 4, crc_offset = current ? 12 : 8;
            info_.length = u32(header_ + length_offset);
            if (std::memcmp(header_, current ? "CNST" : "CREV", 4)
                || checksum(header_, width - 4) != u32(header_ + width - 4)
                || info_.length < 116 || info_.length > max_document
                || storage_.size(slot_) != info_.length + width + 4) { invalid(); return; }
            if (current) {
                if (!text_valid(header_ + 20, 64) || name_compare(reinterpret_cast<const char *>(header_ + 20), name_)) { invalid(); return; }
                info_.number = u32(header_ + 4); info_.modified = u32(header_ + 16);
            } else {
                if (header_[20] ? !text_valid(header_ + 20, 64) : std::memcmp(header_ + 20, zeros_, 64)) { invalid(); return; }
                info_.modified = u32(header_ + 12); std::memcpy(info_.label, header_ + 20, 64);
            }
            expected_crc_ = u32(header_ + crc_offset); offset_ = 0; crc_ = 0xffffffff;
            phase_ = Phase::read; return;
        }
        case Phase::read: {
            unsigned n = amount(); unsigned width = operation_ == Operation::current ? 96 : 88;
            if (!storage_.read(slot_, width + offset_, data_ + offset_, n)) { invalid(); return; }
            update(data_ + offset_, n); offset_ += n;
            if (offset_ == info_.length) phase_ = Phase::read_end;
            return;
        }
        case Phase::read_end:
            if (!storage_.read(slot_, (operation_ == Operation::current ? 96 : 88) + info_.length, block, 4)
                || std::memcmp(block, "DONE", 4) || (crc_ ^ 0xffffffff) != expected_crc_) { invalid(); return; }
            finish(true); return;
        case Phase::hash: {
            unsigned n = amount(); update(data_ + offset_, n); offset_ += n;
            if (offset_ == info_.length) {
                std::memcpy(header_, "CREV", 4); put32(header_ + 4, info_.length);
                put32(header_ + 8, crc_ ^ 0xffffffff); put32(header_ + 12, info_.modified);
                std::memcpy(header_ + 20, info_.label, 64); put32(header_ + 84, checksum(header_, 84));
                scan(UINT64_MAX);
            }
            return;
        }
        case Phase::begin:
            if (!storage_.begin(0)) { finish(false); return; }
            opened_ = true; phase_ = Phase::write_header; return;
        case Phase::write_header:
            if (!storage_.write(header_, 88)) { finish(false); return; }
            offset_ = 0; phase_ = Phase::write; return;
        case Phase::write: {
            unsigned n = amount(); if (!storage_.write(data_ + offset_, n)) { finish(false); return; }
            offset_ += n; if (offset_ == info_.length) phase_ = Phase::write_end; return;
        }
        case Phase::write_end:
            if (!storage_.write(reinterpret_cast<const uint8_t *>("DONE"), 4)) { finish(false); return; }
            phase_ = Phase::close; return;
        case Phase::close: {
            opened_ = false;
            if (!storage_.finish()) { finish(false); return; }
            phase_ = Phase::size; return;
        }
        case Phase::size:
            if (storage_.size(0) != info_.length + 92) { finish(false); return; }
            offset_ = 0; phase_ = Phase::verify; return;
        case Phase::verify: {
            unsigned n = info_.length + 92 - offset_; if (n > 512) n = 512;
            if (!storage_.read(0, offset_, block, n)) { finish(false); return; }
            for (unsigned i = 0; i < n; ++i) {
                unsigned p = offset_ + i;
                uint8_t expected = p < 88 ? header_[p] : p < 88 + info_.length ? data_[p - 88] : uint8_t("DONE"[p - 88 - info_.length]);
                if (block[i] != expected) { finish(false); return; }
            }
            offset_ += n; if (offset_ == info_.length + 92) finish(true); return;
        }
        case Phase::remove:
            if (!storage_.remove_revision(name_, info_.number)) { finish(false); return; }
            if (operation_ == Operation::clear) phase_ = Phase::clear_scan; else finish(true);
            return;
        }
    }
private:
    enum class Operation { read, current, next, append, erase, clear };
    enum class Phase { idle, scan, clear_scan, header, read, read_end, hash, begin, write_header, write, write_end, close, size, verify, remove };
    SetupStorage &storage_;
    Phase phase_ = Phase::idle;
    Operation operation_ = Operation::read;
    Revision info_;
    char name_[64] = {};
    uint8_t header_[96] = {}, *data_ = nullptr;
    unsigned offset_ = 0, slot_ = 0;
    uint32_t crc_ = 0xffffffff, expected_crc_ = 0;
    uint64_t best_ = 0;
    bool good_ = false, opened_ = false;
    inline static const uint8_t zeros_[64] = {};
    unsigned amount() const { unsigned n = info_.length - offset_; return n > 512 ? 512 : n; }
    void initialize(const char *name, uint8_t *data) {
        std::snprintf(name_, sizeof(name_), "%s", name); data_ = data;
        info_ = Revision(); offset_ = slot_ = 0; crc_ = 0xffffffff; good_ = opened_ = false;
        std::memset(header_, 0, sizeof(header_));
    }
    void update(const uint8_t *data, unsigned n) {
        for (unsigned i = 0; i < n; ++i) {
            crc_ ^= data[i];
            for (unsigned b = 0; b < 8; ++b) crc_ = (crc_ >> 1) ^ (0xedb88320u & (0u - (crc_ & 1)));
        }
    }
    void scan(uint64_t before) { best_ = 0; storage_.revision_scan(name_, before, operation_ == Operation::next); phase_ = Phase::scan; }
    void invalid() { if (operation_ == Operation::next) scan(info_.number); else finish(false); }
    void finish(bool good) { if (opened_) { storage_.finish(); opened_ = false; } good_ = good; phase_ = Phase::idle; }
};
}
