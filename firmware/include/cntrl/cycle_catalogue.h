#pragma once
#include "binary_format.h"

namespace cntrl {
class CycleCatalogue {
public:
    static constexpr unsigned all = 65535;
    struct Program {
        const uint8_t *data;
        const uint8_t *patch() const { return data + 2; }
        const uint8_t *bank() const { return data + 84; }
        unsigned instrument() const { return binary::read16(patch()); }
        unsigned node() const { return binary::read16(data); }
        unsigned tags() const { return binary::read16(data + 88); }
        unsigned names_size() const { return binary::read16(data + 90); }
        const uint8_t *names() const { return data + 92 + tags() * 2; }
        Program next() const { return {names() + names_size()}; }
#ifdef CNTRL_FLASH_ROUTINE
        CNTRL_FLASH_ROUTINE(cycle_catalogue_program_matches)
#endif
        bool matches(unsigned alias, unsigned tag) const {
            if (alias != instrument()) return false;
            if (tag == all) return true;
            for (unsigned i = 0; i < tags(); ++i)
                if (binary::read16(data + 92 + i * 2) == tag) return true;
            return false;
        }
    };

    CycleCatalogue(const uint8_t *data = nullptr, unsigned size = 0) : data_(data), size_(size) {}
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(cycle_catalogue_from_setup)
#endif
    static CycleCatalogue from_setup(const uint8_t *data, unsigned length, unsigned offset) {
        while (offset < length) {
            const auto *record = data + offset;
            const unsigned size = binary::read32(record + 4);
            if (binary::read16(record) == binary::cycle_catalogue)
                return CycleCatalogue(record + binary::record_size, size);
            offset += binary::record_size + size;
        }
        return {};
    }
    unsigned size() const { return data_ ? binary::read16(data_) : 0; }
    unsigned tags() const { return data_ ? binary::read16(data_ + 2) : 0; }
    unsigned patch_limit() const { return binary::read16(data_ + 4); }
    unsigned cycle_limit() const { return binary::read16(data_ + 6); }
    const char *tag(unsigned i) const { return reinterpret_cast<const char *>(data_ + 8 + i * 64); }
    Program first() const { return {data_ + 8 + tags() * 64}; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(cycle_catalogue_count)
#endif
    unsigned count(unsigned alias, unsigned tag = all) const {
        unsigned matches = 0;
        if (size()) for (auto item = first(); item.data != data_ + size_; item = item.next())
            matches += item.matches(alias, tag);
        return matches;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(cycle_catalogue_contains_node)
#endif
    bool contains_node(unsigned node) const {
        if (size()) for (auto item = first(); item.data != data_ + size_; item = item.next())
            if (item.node() == node) return true;
        return false;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(cycle_catalogue_valid)
#endif
    bool valid(unsigned instruments, unsigned nodes, unsigned pages) const {
        if (!data_) return true;
        if (size_ < 8 || !patch_limit() || !cycle_limit() || tags() > (size_ - 8) / 64) return false;
        for (unsigned i = 0; i < tags(); ++i)
            if (!tag(i)[0] || tag(i)[63]) return false;
        const auto *end = data_ + size_;
        Program item = first();
        for (unsigned i = 0; i < size(); ++i) {
            if (end - item.data < 92 || item.instrument() >= instruments || item.node() >= nodes
                || item.tags() > tags() || 2 * item.tags() + item.names_size() > unsigned(end - item.data - 92))
                return false;
            for (unsigned j = 0; j < item.tags(); ++j)
                if (binary::read16(item.data + 92 + 2 * j) >= tags()) return false;
            for (unsigned j = 0; j < 3; ++j)
                if (binary::read16(item.patch() + 76 + 2 * j) >= pages) return false;
            const auto *name = item.names();
            const auto *next = item.next().data;
            for (unsigned j = 0; j < 3; ++j) {
                if (next - name < 2) return false;
                const unsigned length = binary::read16(name);
                name += 2;
                if (length == 65535) continue;
                if (length > unsigned(next - name)) return false;
                name += length;
            }
            if (name != next) return false;
            item = item.next();
        }
        return item.data == end;
    }
private:
    const uint8_t *data_;
    unsigned size_;
};
} // namespace cntrl
