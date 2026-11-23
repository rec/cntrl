#pragma once
#include "document.h"

namespace cntrl {
// Walk the same path the setup editor can edit. References are kept out of
// the copy; the binding maps and the program they lead to are copied.
inline int patch_edit_node(const Document &document, unsigned root, bool *reference = nullptr) {
    unsigned node = root;
    for (unsigned i = 0; i < document.nodes(); ++i) {
        const auto *p = document.node(node);
        if (p[0] == 1) return int(node);
        if (p[0] == 10) { node = u16(p + 4); continue; }
        if (p[0] != 6 || u16(p + 4) != 65535) return -1;
        int next = document.named_patch(reinterpret_cast<const char *>(p + 6));
        if (next < 0) return -1;
        if (reference) *reference = true;
        node = unsigned(next);
    }
    return -1;
}

#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(patch_edit_detach)
#endif
inline bool detach_patch(const Document &source, unsigned patch, MemoryBuffer &output) {
    if (source.has_opaque_metadata()) return false;
    const auto *data = source.data();
    const unsigned root = source.browser_root(patch), count = source.nodes();
    unsigned copies = 0, bytes = 0, node = root;
    for (unsigned visited = 0;; ++visited) {
        if (visited >= count || node >= count) return false;
        const auto *p = source.node(node);
        if (p[0] == 6 && u16(p + 4) == 65535) {
            node = unsigned(source.named_patch(reinterpret_cast<const char *>(p + 6)));
            continue;
        }
        if (p[0] != 1 && p[0] != 10) return false;
        ++copies; bytes += 4 + u16(p + 2);
        if (p[0] == 1) break;
        node = u16(p + 4);
    }
    if (copies > 65535u - count) return false;
    const auto *pickup = source.pickup_record();
    const unsigned offset = source.core_offset();
    const unsigned envelope = binary::header_size + binary::record_size;
    const unsigned growth = copies * 4 + bytes + (pickup ? copies : 0);
    const unsigned origin_bytes = (copies + 1) * 4;
    const unsigned length = source.length() - offset + envelope + growth
        + binary::record_size + origin_bytes;
    if (length - binary::header_size > binary::maximum_payload || !output.resize(length)) return false;
    const unsigned original_graph = source.graph_metadata();
    const unsigned old_graph_size = u32(data + original_graph + 4);
    auto *destination = output.data;
    uint8_t prefix[envelope];
    if (!binary::prefix(prefix, binary::Kind::setup, source.core_length() + growth,
            length - envelope - source.core_length() - growth)) return false;
    std::memcpy(destination, prefix, envelope);
    auto *cursor = destination + envelope;
    auto copy = [&](const uint8_t *from, unsigned size) {
        std::memcpy(cursor, from, size); cursor += size;
    };
    const auto *before_graph = data + offset;
    uint8_t *policies = nullptr;
    if (pickup) {
        const auto *end = pickup + 9 + count;
        auto *header = cursor + (pickup - before_graph);
        copy(before_graph, unsigned(end - before_graph));
        binary::write16(header + 4, count + copies);
        policies = cursor; cursor += copies;
        before_graph = end;
    }
    copy(before_graph, unsigned(data + original_graph - before_graph));
    auto *graph = cursor;
    copy(data + original_graph, 16 + count * 4);
    cursor += copies * 4;
    put32(graph + 4, old_graph_size + copies * 4 + bytes);
    binary::write16(graph + 8, count + copies);
    auto *roots = cursor;
    copy(data + original_graph + 16 + count * 4, old_graph_size - 16 - count * 4);
    binary::write16(roots + patch * 2, count);
    for (unsigned i = 0; i < count; ++i) {
        auto *index = graph + 16 + i * 4;
        put32(index, u32(index) + copies * 4);
    }
    auto *origins = destination + length - origin_bytes;
    auto *record = origins - binary::record_size;
    binary::write16(record, binary::patch_edit_origins); binary::write16(record + 2, 0);
    put32(record + 4, origin_bytes);
    binary::write16(origins, count); binary::write16(origins + 2, root); origins += 4;
    node = root;
    unsigned next = count;
    for (;;) {
        const auto *p = source.node(node);
        if (p[0] == 6) {
            node = unsigned(source.named_patch(reinterpret_cast<const char *>(p + 6)));
            continue;
        }
        auto *clone = cursor;
        put32(graph + 16 + next * 4, unsigned(clone - graph));
        if (policies) policies[next - count] = pickup[9 + (next == count && pickup[9 + root] ? root : node)];
        binary::write16(origins, next); binary::write16(origins + 2, node); origins += 4;
        copy(p, 4 + u16(p + 2));
        if (p[0] == 1) break;
        binary::write16(clone + 4, ++next);
        node = u16(p + 4);
    }
    const auto *tail = data + original_graph + old_graph_size;
    copy(tail, unsigned(data + source.length() - tail));
    put32(destination + envelope + 8, source.core_length() + growth);
    return binary::seal_setup(destination, length);
}
} // namespace cntrl
