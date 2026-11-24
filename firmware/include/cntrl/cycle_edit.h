#pragma once
#include "document.h"
#include "setup_names.h"

namespace cntrl {
#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(cycle_edit_append)
#endif
inline bool append_cycle(const Document &source, const char *name, unsigned instrument,
                         unsigned tag, MemoryBuffer &output) {
    const auto &catalogue = source.cycle_catalogue();
    const unsigned selected = catalogue.count(instrument, tag);
    const unsigned cycles = source.patch_cycles(), patches = source.patches();
    const unsigned name_length = unsigned(std::strlen(name));
    if (!selected || source.has_opaque_metadata() || cycles >= catalogue.cycle_limit()
        || patches + selected > catalogue.patch_limit() || !name_length || name_length >= 64
        || !utf8_valid(reinterpret_cast<const uint8_t *>(name), name_length)) return false;
    for (unsigned i = 0; i < cycles; ++i)
        if (!name_compare(name, reinterpret_cast<const char *>(source.patch_cycle(i)))) return false;

    unsigned names_size = 0;
    auto item = catalogue.first();
    for (unsigned i = 0; i < catalogue.size(); ++i, item = item.next()) {
        if (item.matches(instrument, tag)) names_size += item.names_size();
    }
    constexpr unsigned cycle_bytes = 68, patch_bytes = 82, recipe_bytes = 68;
    const unsigned growth = cycle_bytes + selected * (patch_bytes + 4 + 2) + names_size;
    const unsigned length = source.length() + growth + binary::record_size + recipe_bytes;
    if (length - binary::header_size > binary::maximum_payload || !output.resize(length)) return false;
    const auto *data = source.data();
    auto *destination = output.data;
    auto *cursor = destination;
    auto copy = [&](const uint8_t *from, const uint8_t *end) {
        const unsigned bytes = unsigned(end - from);
        std::memcpy(cursor, from, bytes); cursor += bytes;
    };
    auto programs = [&](auto append) {
        auto program = catalogue.first();
        for (unsigned i = 0; i < catalogue.size(); ++i, program = program.next())
            if (program.matches(instrument, tag)) append(program);
    };
    copy(data, source.patch_cycle(cycles));
    auto *cycle = cursor;
    std::memset(cycle, 0, cycle_bytes);
    std::memcpy(cycle, name, name_length);
    binary::write16(cycle + 64, patches);
    binary::write16(cycle + 66, selected);
    cursor += cycle_bytes;
    copy(source.patch_cycle(cycles), source.patch(patches));
    programs([&](CycleCatalogue::Program program) {
        auto *patch = cursor;
        copy(program.patch(), program.patch() + patch_bytes);
        binary::write16(patch + 2, cycles);
    });
    const auto *banks = source.bank(patches);
    copy(source.patch(patches), banks);
    programs([&](CycleCatalogue::Program program) { copy(program.bank(), program.bank() + 4); });
    const auto *graph = data + source.graph_metadata();
    copy(banks, graph);
    programs([&](CycleCatalogue::Program program) { copy(program.names(), program.next().data); });
    auto *new_graph = cursor;
    const unsigned index_bytes = 16 + source.nodes() * 4;
    const auto *root_end = graph + index_bytes + patches * 2;
    copy(graph, root_end);
    programs([&](CycleCatalogue::Program program) {
        binary::write16(cursor, program.node()); cursor += 2;
    });
    put32(new_graph + 4, u32(graph + 4) + selected * 2);
    binary::write16(new_graph + 12, patches + selected);
    for (unsigned i = 16; i < index_bytes; i += 4)
        put32(new_graph + i, u32(new_graph + i) + selected * 2);
    copy(root_end, data + source.length());

    binary::write16(cursor, binary::cycle_recipe);
    binary::write16(cursor + 2, 0);
    put32(cursor + 4, recipe_bytes);
    cursor += binary::record_size;
    std::memset(cursor, 0, recipe_bytes);
    binary::write16(cursor, cycles);
    binary::write16(cursor + 2, instrument);
    if (tag != CycleCatalogue::all) std::memcpy(cursor + 4, catalogue.tag(tag), 64);
    cursor += recipe_bytes;

    auto *new_core = destination + source.core_offset();
    const unsigned core_length = source.core_length() + growth;
    put32(destination + 12, length - binary::header_size);
    put32(new_core - 4, core_length);
    put32(new_core + 8, core_length);
    binary::write16(new_core + 14, cycles + 1);
    binary::write16(new_core + 16, patches + selected);
    auto charge = [](unsigned bytes) { return bytes ? ((bytes + 7) & ~7u) + 8 : 0; };
    const unsigned state = source.instruments() * 8 + cycles * 4;
    const auto layout = source.page_layout();
    unsigned extra_ram = std::max(1u, layout.controllers()) * (charge(state + 4) - charge(state));
    for (unsigned i = 0; i < layout.pages(); ++i)
        if (layout.page(i)[0] == PageLayout::banked)
            extra_ram += charge(length) - charge(source.length());
    put32(destination + source.metadata() + cycle_bytes + selected * patch_bytes
        + scrolling_offset + 8, source.page_ram2() + extra_ram);
    return unsigned(cursor - destination) == length && binary::seal_setup(destination, length);
}
} // namespace cntrl
