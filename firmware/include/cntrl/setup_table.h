#pragma once
#include "document.h"
#include "setup_names.h"
#include <cassert>
#include <cstdlib>
#include <vector>

namespace cntrl {
constexpr unsigned setup_table_ram_overhead(unsigned count) {
    // Resident names/views plus the manifest and size arrays used during saves.
    return count ? count * (148 + 85) + 2 + 4 * (count - 1) + 32 : 0;
}
struct SetupBundleFile {
    virtual ~SetupBundleFile() = default;
    virtual uint32_t size() const = 0;
    virtual bool read(uint32_t offset, uint8_t *data, unsigned length) = 0;
};

// The file contains one sorted name table and a count/offsets/setup-bytes slab.
// RAM holds either that entire slab, or a byte-bounded LRU of validated views.
class SetupTable {
public:
    ~SetupTable() { clear(); }
    SetupTable() = default;
    SetupTable(const SetupTable &) = delete;
    SetupTable &operator=(const SetupTable &) = delete;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_table_open)
#endif
    bool open(SetupBundleFile &file, const Hardware &hardware) {
        clear(); file_ = &file; hardware_ = &hardware;
        uint8_t header[40];
        if (!file.read(0, header, sizeof(header)) || std::memcmp(header, "CSTB", 4)
            || u16(header + 4) != 1 || !u16(header + 6) || u32(header + 8) != file.size()) return false;
        count_ = u16(header + 6); margin_ = u32(header + 16); general_ = u32(header + 20);
        active_ = u32(header + 24); total_setup_ram_ = u32(header + 32);
        unsigned prefix = 2 + 4 * (count_ - 1);
        uint64_t needs = uint64_t(general_) + active_ + setup_table_ram_overhead(count_);
        if (needs + margin_ >= 524288 || needs != u32(header + 28)
            || 524288 - needs - margin_ != u32(header + 36)) return false;
        capacity_ = unsigned(524288 - needs - margin_);
        uint32_t crc = 0xffffffff;
        for (unsigned offset = 40; offset < file.size();) {
            uint8_t block[512]; unsigned n = std::min(uint32_t(512), file.size() - offset);
            if (!file.read(offset, block, n)) return false;
            for (unsigned i = 0; i < n; ++i) {
                crc ^= block[i];
                for (unsigned bit = 0; bit < 8; ++bit) crc = crc & 1 ? (crc >> 1) ^ 0xedb88320 : crc >> 1;
            }
            offset += n;
        }
        if (~crc != u32(header + 12)) return false;
        entries_.resize(count_); names_.resize(count_ * 64); setup_names_.resize(count_);
        unsigned table = 40 + count_ * 76;
        uint64_t position = uint64_t(table) + prefix;
        unsigned peak = 0;
        uint8_t record[76], word[4];
        if (!file.read(table, word, 2) || u16(word) != count_) return false;
        for (unsigned i = 0; i < count_; ++i) {
            if (!file.read(40 + i * 76, record, sizeof(record)) || !text_valid(record, 64)
                || !setup_name(reinterpret_cast<const char *>(record)) || record[72] > 1) return false;
            std::memcpy(names_.data() + i * 64, record, 64);
            setup_names_[i] = names_.data() + i * 64;
            if (i && name_compare(setup_names_[i - 1], setup_names_[i]) >= 0) return false;
            auto &entry = entries_[i]; entry.length = u32(record + 64); entry.prefill = record[72]; entry.ram2 = u32(record + 68);
            peak = std::max(peak, entry.ram2);
            if ((entry.length && entry.length < 116) || entry.length > capacity_ || position + entry.length > file.size()) return false;
            entry.offset = uint32_t(position); position += entry.length;
            if (i + 1 < count_ && (!file.read(table + 2 + i * 4, word, 4)
                || u32(word) != position - table - prefix)) return false;
        }
        if (position != file.size() || position - table - prefix != total_setup_ram_ || peak != active_) return false;
        uint32_t signature = 0xffffffff;
        for (char c : names_) {
            uint8_t byte = uint8_t(c); if (byte >= 'A' && byte <= 'Z') byte += 32;
            signature ^= byte;
            for (unsigned b = 0; b < 8; ++b) signature = signature & 1 ? (signature >> 1) ^ 0xedb88320 : signature >> 1;
        }
        signature_ = ~signature;
        if (total_setup_ram_ <= capacity_) {
            static_setups_ = static_cast<uint8_t *>(std::malloc(prefix + total_setup_ram_));
            if (!static_setups_ || !file.read(table, static_setups_, prefix + total_setup_ram_)) return false;
            for (auto &entry : entries_) {
                entry.bytes = static_setups_ + entry.offset - table;
                if (entry.length && !entry.document.load(entry.bytes, entry.length, hardware)) return false;
                entry.revision = checksum(entry.bytes, entry.length);
            }
            resident_ = total_setup_ram_;
        } else {
            for (unsigned i = 0; i < count_; ++i)
                if (entries_[i].prefill && !get(uint16_t(i))) return false;
        }
        return true;
    }
    const Document *get(uint16_t index) {
        assert(index < count_);
        auto &entry = entries_[index];
        if (!entry.length) return nullptr;
        if (!entry.bytes) {
            while (resident_ + entry.length > capacity_) {
                unsigned victim = tail_;
                while (victim != none && entries_[victim].pins) victim = entries_[victim].previous;
                if (victim == none) return nullptr;
                auto &old = entries_[victim]; unlink(victim);
                resident_ -= old.length; std::free(old.bytes); old.bytes = nullptr; old.document = Document();
            }
            auto *bytes = static_cast<uint8_t *>(std::malloc(entry.length));
            if (!bytes) return nullptr;
            if (!file_->read(entry.offset, bytes, entry.length) || !entry.document.load(bytes, entry.length, *hardware_)) {
                std::free(bytes); return nullptr;
            }
            entry.bytes = bytes; resident_ += entry.length;
            entry.revision = checksum(bytes, entry.length);
        }
        if (!static_setups_) {
            unlink(index); entry.next = head_;
            if (head_ != none) entries_[head_].previous = index;
            else tail_ = index;
            head_ = index;
        }
        return &entry.document;
    }
    void pin(uint16_t index) { assert(index < count_); ++entries_[index].pins; }
    void unpin(uint16_t index) { assert(index < count_ && entries_[index].pins); --entries_[index].pins; }
    bool pinned() const { for (const auto &entry : entries_) if (entry.pins) return true; return false; }
    unsigned count() const { return count_; }
    const char *name(uint16_t index) const { assert(index < count_); return setup_names_[index]; }
    int index(const char *name) const {
        unsigned begin = 0, end = count_;
        while (begin < end) {
            unsigned middle = begin + (end - begin) / 2;
            int order = name_compare(name, setup_names_[middle]);
            if (!order) return int(middle);
            if (order < 0) end = middle; else begin = middle + 1;
        }
        return -1;
    }
    unsigned total_setup_ram() const { return total_setup_ram_; }
    unsigned total_ram2_needs() const { return count_ ? general_ + active_ + setup_table_ram_overhead(count_) : 0; }
    unsigned total_free_ram2() const { return capacity_; }
    unsigned resident_bytes() const { return resident_; }
    bool all_resident() const { return static_setups_ != nullptr; }
    unsigned length(unsigned i) const { return entries_[i].length; }
    unsigned offset(unsigned i) const { return entries_[i].offset; }
    unsigned ram2(unsigned i) const { return entries_[i].ram2; }
    uint32_t revision(unsigned i) const { return entries_[i].revision; }
    uint32_t names_signature() const { return signature_; }
    bool prefill(unsigned i) const { return entries_[i].prefill; }
    unsigned memory_margin() const { return margin_; }
    unsigned cntrl_ram2() const { return general_; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(setup_table_clear)
#endif
    void clear() {
        if (!static_setups_) for (auto &entry : entries_) std::free(entry.bytes);
        std::free(static_setups_); static_setups_ = nullptr;
        std::vector<Entry>().swap(entries_); std::vector<char>().swap(names_);
        std::vector<const char *>().swap(setup_names_);
        count_ = resident_ = capacity_ = total_setup_ram_ = 0; head_ = tail_ = none;
    }
private:
    static constexpr unsigned none = 65535;
    struct Entry {
        Document document;
        uint8_t *bytes = nullptr;
        unsigned offset = 0, length = 0, previous = none, next = none, pins = 0, ram2 = 0;
        uint32_t revision = 0;
        bool prefill = false;
    };
    static_assert(sizeof(void *) != 4 || sizeof(Entry) <= 80, "Update the RAM2 entry estimate");
    SetupBundleFile *file_ = nullptr;
    const Hardware *hardware_ = nullptr;
    std::vector<Entry> entries_;
    std::vector<char> names_;
    std::vector<const char *> setup_names_;
    uint8_t *static_setups_ = nullptr;
    unsigned count_ = 0, resident_ = 0, capacity_ = 0, total_setup_ram_ = 0;
    unsigned margin_ = 0, general_ = 0, active_ = 0, head_ = none, tail_ = none;
    uint32_t signature_ = 0;
    void unlink(unsigned index) {
        auto &entry = entries_[index];
        if (entry.previous != none) entries_[entry.previous].next = entry.next;
        else if (head_ == index) head_ = entry.next;
        if (entry.next != none) entries_[entry.next].previous = entry.previous;
        else if (tail_ == index) tail_ = entry.previous;
        entry.previous = entry.next = none;
    }
};
} // namespace cntrl
