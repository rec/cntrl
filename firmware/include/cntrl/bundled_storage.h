#pragma once
#include "setup_table.h"
#include "checked_array.h"
#include <climits>

namespace cntrl {
// Keep the editor/history record protocol, backed by one indexed bundle.
// Only selection and history records remain separate from the bundle.
class BundledStorage : public SetupStorage {
public:
    BundledStorage(SetupStorage &disk, const Hardware &hardware) : disk_(disk), hardware_(hardware), image_(disk) {}
    SetupTable &setups() { return table_; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(bundle_boot)
#endif
    bool boot() {
        table_.clear(); generation_ = modified_ = bundle_crc_ = 0; image_.length = 0;
        for (unsigned slot = 0; slot < 2; ++slot) {
            uint8_t header[96], end[4]; disk_.choose(bundle_name);
            unsigned size = disk_.size(slot);
            if (size < 140 || !disk_.read(slot, 0, header, 96) || std::memcmp(header, "CNST", 4)
                || checksum(header, 92) != u32(header + 92) || size != u32(header + 8) + 100
                || !disk_.read(slot, size - 4, end, 4) || std::memcmp(end, "DONE", 4)) continue;
            uint32_t crc = 0xffffffff;
            if (!hash_record(slot, u32(header + 8), crc) || ~crc != u32(header + 12)) continue;
            if (!image_.length || int32_t(u32(header + 4) - generation_) > 0) {
                image_.slot = slot; image_.length = u32(header + 8);
                generation_ = u32(header + 4); modified_ = u32(header + 16);
                bundle_crc_ = u32(header + 12);
            }
        }
        return !image_.length || table_.open(image_, hardware_);
    }
    bool available() const override { return disk_.available(); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(bundle_choose)
#endif
    void choose(const char *name) override {
        history_ = false; selection_ = !name[0]; index_ = selection_ ? -1 : table_.index(name);
        if (selection_) disk_.choose("");
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(bundle_next)
#endif
    bool next(const char *after, char *name) override {
        for (unsigned i = 0; i < table_.count(); ++i)
            if (table_.length(i) && name_compare(table_.name(uint16_t(i)), after) > 0) {
                std::snprintf(name, 64, "%s", table_.name(uint16_t(i))); return true;
            }
        return false;
    }
    void catalogue_begin() override { cursor_ = 0; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(bundle_catalogue_step)
#endif
    bool catalogue_step(char *name) override {
        name[0] = 0;
        if (cursor_ == table_.count()) return true;
        unsigned i = cursor_++;
        if (table_.length(i)) std::snprintf(name, 64, "%s", table_.name(uint16_t(i)));
        return false;
    }
    unsigned setup_count() const override { return table_.count(); }
    uint32_t setup_names_signature() const override { return table_.names_signature(); }
    const char *setup_name_at(unsigned i) const override { return table_.name(uint16_t(i)); }
    void prepare_document(const Document &document) override { replacement_ram2_ = document.valid() ? document.page_ram2() : 0; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(bundle_size)
#endif
    unsigned size(unsigned slot) override {
        if (history_ || selection_) { select_disk(); return disk_.size(slot); }
        return index_ >= 0 && slot == (generation_ & 1) ? table_.length(unsigned(index_)) + 100 : 0;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(bundle_read)
#endif
    bool read(unsigned slot, unsigned offset, uint8_t *data, unsigned length) override {
        if (history_ || selection_) { select_disk(); return disk_.read(slot, offset, data, length); }
        if (index_ < 0 || slot != (generation_ & 1) || offset > size(slot) || length > size(slot) - offset) return false;
        const Document *document = table_.length(unsigned(index_)) ? table_.get(uint16_t(index_)) : nullptr;
        if (table_.length(unsigned(index_)) && !document) return false;
        uint8_t header[96] = {}; make_record(header, generation_, table_.length(unsigned(index_)),
            document ? table_.revision(unsigned(index_)) : 0, modified_, table_.name(uint16_t(index_)));
        for (unsigned i = 0; i < length; ++i) {
            unsigned at = offset + i;
            data[i] = at < 96 ? header[at] : at < 96 + table_.length(unsigned(index_))
                ? document->data()[at - 96] : uint8_t("DONE"[at - 96 - table_.length(unsigned(index_))]);
        }
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(bundle_begin)
#endif
    bool begin(unsigned slot) override {
        if (history_ || selection_) { select_disk(); return disk_.begin(slot); }
        incoming_ = consumed_ = 0; writing_ = index_ >= 0; failed_ = !writing_; return writing_;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(bundle_write)
#endif
    bool write(const uint8_t *data, unsigned length) override {
        if (history_ || selection_) { select_disk(); return disk_.write(data, length); }
        if (!writing_ || failed_) return false;
        for (unsigned i = 0; i < length;) {
            if (incoming_ < 96) {
                unsigned n = std::min(length - i, 96u - incoming_);
                std::memcpy(incoming_header_ + incoming_, data + i, n); incoming_ += n; i += n;
                if (incoming_ == 96 && !start_replacement()) { failed_ = true; return false; }
            } else if (consumed_ < replacement_length_) {
                unsigned n = std::min(length - i, replacement_length_ - consumed_);
                if (!append(data + i, n)) { failed_ = true; return false; }
                consumed_ += n; i += n;
            } else {
                unsigned at = incoming_ - 96;
                if (at >= 4 || data[i++] != uint8_t("DONE"[at])) { failed_ = true; return false; }
                ++incoming_;
            }
        }
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(bundle_finish)
#endif
    bool finish() override {
        if (history_ || selection_) { select_disk(); return disk_.finish(); }
        if (!writing_) return false;
        writing_ = false;
        unsigned old_slot = image_.slot, old_length = image_.length;
        if (failed_ || incoming_ != 100 || consumed_ != replacement_length_
            || !copy(table_.offset(unsigned(index_)) + table_.length(unsigned(index_)), table_.envelope().offset + table_.envelope().length)
            || !copy_extensions()
            || !commit_image()) {
            disk_.finish(); image_.slot = old_slot; image_.length = old_length;
            if (!table_.pinned()) table_.open(image_, hardware_);
            return false;
        }
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(bundle_choose_revision)
#endif
    void choose_revision(const char *name, uint64_t number) override {
        history_ = true; selection_ = false; revision_ = number;
        std::snprintf(history_name_, sizeof(history_name_), "%s", name); select_disk();
    }
    void revision_scan(const char *name, uint64_t before, bool records_only) override { disk_.revision_scan(name, before, records_only); }
    bool revision_scan_step(uint64_t &best) override { return disk_.revision_scan_step(best); }
    bool remove_revision(const char *name, uint64_t number) override { return disk_.remove_revision(name, number); }

#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(bundle_upload_begin)
#endif
    bool upload_begin(unsigned length, uint32_t crc, uint32_t modified) {
        if (writing_) return false;
        upload_length_ = length; upload_crc_ = crc; replacement_modified_ = modified;
        output_slot_ = image_.length ? 1 - image_.slot : 0;
        disk_.choose(bundle_name); uint8_t header[96] = {};
        return disk_.begin(output_slot_) && disk_.write(header, sizeof(header));
    }
    bool upload_write(const uint8_t *data, unsigned length) { return disk_.write(data, length); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(bundle_upload_finish)
#endif
    bool upload_finish() {
        if (table_.pinned()) { disk_.finish(); return false; }
        unsigned old_slot = image_.slot, old_length = image_.length;
        image_.slot = output_slot_; image_.length = upload_length_;
        uint32_t crc = 0xffffffff;
        if (!disk_.flush() || !hash_record(output_slot_, upload_length_, crc) || ~crc != upload_crc_
            || !table_.open(image_, hardware_) || !finish_record(upload_crc_)) {
            disk_.finish(); image_.slot = old_slot; image_.length = old_length;
            if (old_length) table_.open(image_, hardware_); else table_.clear();
            return false;
        }
        return true;
    }
    void upload_cancel() { disk_.finish(); }
    unsigned bundle_size() const { return image_.length; }
    uint32_t bundle_crc() const { return bundle_crc_; }
    bool bundle_read(unsigned offset, uint8_t *data, unsigned length) { return image_.read(offset, data, length); }
private:
    inline static constexpr char bundle_name[] = "bundle";
    struct Image : SetupBundleFile {
        SetupStorage &disk;
        unsigned slot = 0, length = 0;
        explicit Image(SetupStorage &value) : disk(value) {}
        uint32_t size() const override { return length; }
        bool read(uint32_t offset, uint8_t *data, unsigned n) override {
            disk.choose(bundle_name); return offset <= length && n <= length - offset && disk.read(slot, 96 + offset, data, n);
        }
    };
    SetupStorage &disk_; const Hardware &hardware_; Image image_; SetupTable table_;
    unsigned cursor_ = 0, incoming_ = 0, consumed_ = 0, replacement_length_ = 0, replacement_ram2_ = 0;
    unsigned output_slot_ = 0, upload_length_ = 0;
    int index_ = -1;
    bool history_ = false, selection_ = false, writing_ = false, failed_ = false;
    uint64_t revision_ = 0;
    uint32_t generation_ = 0, modified_ = 0, body_crc_ = 0xffffffff, upload_crc_ = 0, replacement_modified_ = 0;
    uint32_t bundle_crc_ = 0;
    uint8_t incoming_header_[96] = {}, bundle_header_[40] = {};
    char history_name_[64] = {};
    void select_disk() { if (history_) disk_.choose_revision(history_name_, revision_); else disk_.choose(""); }
    static void update(uint32_t &crc, const uint8_t *data, unsigned n) {
        for (unsigned i = 0; i < n; ++i) { crc ^= data[i]; for (unsigned b = 0; b < 8; ++b) crc = crc & 1 ? (crc >> 1) ^ 0xedb88320 : crc >> 1; }
    }
    static void make_record(uint8_t *h, uint32_t generation, unsigned length, uint32_t crc, uint32_t modified, const char *name) {
        std::memset(h, 0, 96); std::memcpy(h, "CNST", 4); put32(h + 4, generation); put32(h + 8, length);
        put32(h + 12, crc); put32(h + 16, modified); std::snprintf(reinterpret_cast<char *>(h + 20), 64, "%s", name);
        put32(h + 92, checksum(h, 92));
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(bundle_hash_record)
#endif
    bool hash_record(unsigned slot, unsigned length, uint32_t &crc) {
        uint8_t block[512]; disk_.choose(bundle_name);
        for (unsigned offset = 0; offset < length;) {
            unsigned n = std::min(512u, length - offset);
            if (!disk_.read(slot, 96 + offset, block, n)) return false;
            update(crc, block, n); offset += n;
        }
        return true;
    }
    bool append(const uint8_t *data, unsigned length) { update(body_crc_, data, length); return disk_.write(data, length); }
    bool copy(unsigned begin, unsigned end) {
        uint8_t block[512];
        while (begin < end) {
            unsigned n = std::min(512u, end - begin);
            if (!image_.read(begin, block, n) || !append(block, n)) return false;
            begin += n;
        }
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(bundle_start_replacement)
#endif
    bool start_replacement() {
        if (table_.pinned()) return false;
        if (std::memcmp(incoming_header_, "CNST", 4) || checksum(incoming_header_, 92) != u32(incoming_header_ + 92)) return false;
        replacement_length_ = u32(incoming_header_ + 8); replacement_modified_ = u32(incoming_header_ + 16);
        if (table_.envelope().opaque && !replacement_length_) return false;
        unsigned count = table_.count(), active = 0;
        if (!count) return false;
        const unsigned prefix = 2 + 4 * (count - 1);
        uint64_t total = 0;
        for (unsigned i = 0; i < count; ++i) {
            total += int(i) == index_ ? replacement_length_ : table_.length(i);
            active = std::max(active, int(i) == index_ ? replacement_ram2_ : table_.ram2(i));
        }
        uint64_t needs = uint64_t(table_.cntrl_ram2()) + active + setup_table_ram_overhead(count);
        if (needs + table_.memory_margin() >= table_.memory_capacity()) return false;
        unsigned capacity = unsigned(table_.memory_capacity() - needs - table_.memory_margin());
        if (replacement_length_ > memory_budget) return false;
        CheckedArray<unsigned> sizes;
        if (!sizes.resize(count)) return false;
        for (unsigned i = 0; i < count; ++i) sizes[i] = int(i) == index_ ? replacement_length_ : table_.length(i);
        for (unsigned n : sizes) if (n > memory_budget) return false;
        MemoryBuffer chosen;
        if (!prefill(sizes, capacity, chosen)) return false;
        MemoryBuffer manifest;
        if (!manifest.resize(count * 76 + prefix) || uint64_t(40) + manifest.size + total > UINT_MAX) return false;
        unsigned offset = 0;
        manifest.data[count * 76] = uint8_t(count); manifest.data[count * 76 + 1] = uint8_t(count >> 8);
        for (unsigned i = 0; i < count; ++i) {
            uint8_t *entry = manifest.data + i * 76;
            std::memset(entry, 0, 76);
            std::snprintf(reinterpret_cast<char *>(entry), 64, "%s", table_.name(uint16_t(i)));
            put32(entry + 64, sizes[i]); put32(entry + 68, int(i) == index_ ? replacement_ram2_ : table_.ram2(i));
            entry[72] = bool(chosen.data[i / 8] & (1u << (i % 8))); offset += sizes[i];
            if (i + 1 < count) put32(manifest.data + count * 76 + 2 + i * 4, offset);
        }
        std::memset(bundle_header_, 0, sizeof(bundle_header_));
        std::memcpy(bundle_header_, "CSTB", 4); bundle_header_[4] = 1;
        bundle_header_[6] = uint8_t(count); bundle_header_[7] = uint8_t(count >> 8);
        unsigned core_length = 40 + manifest.size + unsigned(total);
        unsigned extensions = image_.length - table_.envelope().offset - table_.envelope().length;
        uint8_t envelope[28];
        if (!binary::prefix(envelope, binary::Kind::bundle, core_length, extensions)) return false;
        upload_length_ = 28 + core_length + extensions;
        put32(bundle_header_ + 8, core_length); put32(bundle_header_ + 16, table_.memory_margin());
        put32(bundle_header_ + 20, table_.cntrl_ram2()); put32(bundle_header_ + 24, active);
        put32(bundle_header_ + 28, unsigned(needs)); put32(bundle_header_ + 32, unsigned(total)); put32(bundle_header_ + 36, capacity);
        output_slot_ = 1 - image_.slot; disk_.choose(bundle_name); uint8_t blank[96] = {};
        body_crc_ = 0xffffffff;
        return disk_.begin(output_slot_) && disk_.write(blank, 96) && disk_.write(envelope, 28) && disk_.write(bundle_header_, 40)
            && append(manifest.data, manifest.size)
            && copy(table_.envelope().offset + 40 + count * 76 + prefix, table_.offset(unsigned(index_)));
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(bundle_prefill)
#endif
    static bool prefill(const CheckedArray<unsigned> &sizes, unsigned capacity, MemoryBuffer &chosen) {
        if (!chosen.resize((sizes.size() + 7) / 8)) return false;
        if (chosen.size) std::memset(chosen.data, 0, chosen.size);
        uint64_t sum = 0; for (unsigned n : sizes) sum += n;
        if (sum <= capacity) { if (chosen.size) std::memset(chosen.data, 255, chosen.size); return true; }
        CheckedArray<uint64_t> bits;
        if (!bits.resize(capacity / 64 + 1)) return false;
        auto reachable = [&](unsigned count) {
            std::fill(bits.begin(), bits.end(), 0); bits[0] = 1;
            for (unsigned i = 0; i < count; ++i) {
                unsigned words = sizes[i] / 64, shift = sizes[i] % 64;
                for (unsigned j = unsigned(bits.size()); j-- > words;) {
                    uint64_t value = bits[j - words] << shift;
                    if (shift && j > words) value |= bits[j - words - 1] >> (64 - shift);
                    bits[j] |= value;
                }
            }
        };
        reachable(unsigned(sizes.size())); unsigned target = capacity;
        while (!(bits[target / 64] & (uint64_t(1) << (target % 64)))) --target;
        for (unsigned i = unsigned(sizes.size()); i--;) {
            reachable(i);
            if (!(bits[target / 64] & (uint64_t(1) << (target % 64)))) { chosen.data[i / 8] |= uint8_t(1u << (i % 8)); target -= sizes[i]; }
        }
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(bundle_commit_image)
#endif
    bool commit_image() {
        if (table_.pinned()) return false;
        put32(bundle_header_ + 12, ~body_crc_);
        if (!disk_.patch(96 + 28, bundle_header_, 40) || !disk_.flush()) return false;
        image_.slot = output_slot_; image_.length = upload_length_;
        uint32_t envelope_crc = 0xffffffff;
        uint8_t block[512];
        for (unsigned offset = 0; offset < image_.length;) {
            unsigned n = std::min(512u, image_.length - offset);
            if (!image_.read(offset, block, n)) return false;
            for (unsigned i = 0; i < n; ++i) if (offset + i >= 16 && offset + i < 20) block[i] = 0;
            update(envelope_crc, block, n); offset += n;
        }
        put32(block, ~envelope_crc);
        if (!disk_.patch(96 + 16, block, 4) || !disk_.flush()) return false;
        uint32_t crc = 0xffffffff;
        return hash_record(output_slot_, upload_length_, crc) && table_.open(image_, hardware_) && finish_record(~crc);
    }
    bool copy_extensions() {
        uint8_t bytes[512];
        unsigned offset = table_.envelope().offset + table_.envelope().length;
        while (offset < image_.length) {
            unsigned n = std::min(512u, image_.length - offset);
            if (!image_.read(offset, bytes, n) || !disk_.write(bytes, n)) return false;
            offset += n;
        }
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(bundle_finish_record)
#endif
    bool finish_record(uint32_t crc) {
        uint8_t header[96]; make_record(header, generation_ + 1, upload_length_, crc, replacement_modified_, bundle_name);
        disk_.choose(bundle_name);
        if (!disk_.patch(0, header, 96) || !disk_.write(reinterpret_cast<const uint8_t *>("DONE"), 4) || !disk_.finish()) return false;
        ++generation_; modified_ = replacement_modified_; bundle_crc_ = crc; return true;
    }
};
} // namespace cntrl
