#pragma once
#include "document.h"
#include "checked_array.h"
#include "checked_list.h"

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

enum class PatchKind { instrument_program, channel_program, control_change };
struct PatchField { const char *label; int minimum, maximum; };
inline constexpr PatchField patch_fields[][3] = {
    {{"Program", 1, 128}, {"Volume", -1, 127}, {"Pan", -65, 63}},
    {{"Program", 1, 128}, {"MIDI channel", 1, 16}, {nullptr, 0, 0}},
    {{"Control Change value", 0, 127}, {"Controller", 0, 127}, {"MIDI channel", 1, 16}},
};
struct PatchValues {
    PatchKind kind = PatchKind::instrument_program;
    int values[3] = {};
    uint32_t bank = UINT32_MAX;
    bool differs(const PatchValues &other) const {
        return kind != other.kind || bank != other.bank || !std::equal(values, values + 3, other.values);
    }
};
struct EditableAction {
    uint16_t node = UINT16_MAX;
    uint8_t input = UINT8_MAX, trigger = 0;
    PatchKind kind = PatchKind::instrument_program;
    uint8_t number = 0, value = 0;
};
inline constexpr const char *patch_action_triggers[] = {
    "On", "Off", "Release", "Hold", "Hold release", "Double press", "Double release", "Repeat",
    "Double hold", "Double hold release", "Triple press", "Triple release", "Triple hold", "Triple hold release",
    "Enter", "Exit",
};

#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(editable_patch_value)
#endif
inline const uint8_t *editable_patch_values(const Document &document, unsigned root, PatchValues &values) {
    for (unsigned i = 0; i < document.nodes() && root < document.nodes(); ++i) {
        const auto *node = document.node(root);
        if (node[0] == 10) root = u16(node + 4);
        else if (node[0] == 6 && u16(node + 4) == 65535)
            root = unsigned(document.named_patch(reinterpret_cast<const char *>(node + 6)));
        else if (node[0] == 1) {
            values = {PatchKind::instrument_program, {int(node[6]) + 1,
                node[9] == 255 ? -1 : int(node[9]), node[10] == 255 ? -65 : int(node[10]) - 64}, u32(node + 11)};
            return node;
        } else if (node[0] == 2 && u16(node + 2) == 34 && (node[36] & 0xf0) == 0xc0) {
            values = {PatchKind::channel_program, {int(node[37]) + 1, (node[36] & 15) + 1, 0}};
            return node;
        } else if (node[0] == 2 && node[1] == 0 && u16(node + 2) == 35 && (node[36] & 0xf0) == 0xb0) {
            values = {PatchKind::control_change, {node[38], node[37], (node[36] & 15) + 1}};
            return node;
        } else break;
    }
    return nullptr;
}

#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(read_patch_actions)
#endif
inline bool read_patch_actions(const Document &document, unsigned root, CheckedList<EditableAction> &actions) {
    struct Frame { EditableAction action; unsigned child = 0; };
    CheckedArray<uint8_t> visited;
    CheckedList<Frame> stack;
    actions.release();
    if (root >= document.nodes() || !visited.resize(document.nodes())) return false;
    const auto visit = [&](EditableAction action) {
        if (action.node >= document.nodes() || visited[action.node]) return true;
        visited[action.node] = 1;
        return stack.append({action}) != nullptr;
    };
    if (!visit({uint16_t(root)})) return false;
    while (!stack.empty()) {
        auto &frame = stack.back();
        const auto *node = document.node(frame.action.node), *payload = node + 4;
        auto next = frame.action;
        const unsigned child = frame.child++;
        if (node[0] == 5 && child < u16(payload)) {
            next.node = u16(payload + 2 + child * 2);
        } else if (node[0] == 10 && child <= 30u * payload[66]) {
            if (!child) next.node = u16(payload);
            else {
                const unsigned branch = (child - 1) % 30;
                const auto *binding = payload + 220 + (child - 1) / 30 * binding_record_size;
                const unsigned offset = branch < 2 ? 2 + branch * 2
                    : branch < 14 ? 72 + (branch - 2) * 2
                    : branch < 28 ? binding_shifted_offset + (branch - 14) * 2
                    : binding_hooks_offset + (branch - 28) * 2;
                next = {u16(binding + offset), binding[0], uint8_t(branch)};
            }
        } else if (node[0] == 12 && !child) {
            next.node = u16(payload);
        } else if (node[0] == 6 && !child && u16(payload) == UINT16_MAX) {
            next.node = uint16_t(document.named_patch(reinterpret_cast<const char *>(payload + 2)));
        } else {
            PatchValues values;
            if ((node[0] == 1 || node[0] == 2) && editable_patch_values(document, next.node, values)) {
                next.kind = values.kind;
                next.number = uint8_t(values.kind == PatchKind::control_change ? values.values[1] : values.values[0]);
                next.value = uint8_t(values.values[0]);
                if (!actions.append(next)) return false;
            }
            stack.pop_back();
            continue;
        }
        if (!visit(next)) return false;
    }
    return !actions.empty();
}

#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(read_patch_banks)
#endif
inline bool read_patch_banks(const Document &document, const uint8_t *node, CheckedArray<uint32_t> &banks) {
    if (node[0] != 1) { banks.release(); return true; }
    const unsigned instrument = u16(node + 4);
    unsigned count = 0;
    for (unsigned i = 0; i < document.mappings(); ++i)
        if (u16(document.mapping(i)) == instrument && u32(document.mapping(i) + 2) != UINT32_MAX) ++count;
    if (!banks.resize(count)) return false;
    unsigned at = 0;
    for (unsigned i = 0; i < document.mappings(); ++i) {
        const auto *mapping = document.mapping(i);
        if (u16(mapping) == instrument && u32(mapping + 2) != UINT32_MAX) banks[at++] = u32(mapping + 2);
    }
    return true;
}

#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(adjacent_patch_bank)
#endif
inline uint32_t adjacent_patch_bank(const CheckedArray<uint32_t> &banks, uint32_t current, bool forward) {
    uint32_t candidate = UINT32_MAX;
    for (uint32_t bank : banks) {
        if (forward) {
            if ((current == UINT32_MAX || bank > current) && bank < candidate) candidate = bank;
        } else if (bank < current && current != UINT32_MAX && (candidate == UINT32_MAX || bank > candidate)) {
            candidate = bank;
        }
    }
    return forward && candidate == UINT32_MAX ? current : candidate;
}

#ifdef CNTRL_FLASH_ROUTINE
CNTRL_FLASH_ROUTINE(write_patch_value)
#endif
inline bool write_patch_values(const Document &document, unsigned root, const PatchValues &draft, uint8_t *bytes) {
    PatchValues previous;
    const auto *node = editable_patch_values(document, root, previous);
    if (!node || previous.kind != draft.kind || !previous.differs(draft)
        || (draft.kind != PatchKind::instrument_program && draft.bank != previous.bank)) return false;
    const auto &fields = patch_fields[unsigned(draft.kind)];
    for (unsigned i = 0; i < 3; ++i)
        if (draft.values[i] < fields[i].minimum || draft.values[i] > fields[i].maximum) return false;
    auto *edited = bytes + (node - document.data());
    if (draft.kind == PatchKind::instrument_program) {
        if (draft.bank != previous.bank) {
            const uint8_t *bank = nullptr;
            if (draft.bank != UINT32_MAX) {
                for (unsigned i = 0; i < document.mappings(); ++i) {
                    const auto *mapping = document.mapping(i);
                    if (u16(mapping) == u16(node + 4) && u32(mapping + 2) == draft.bank) { bank = mapping; break; }
                }
                if (!bank) return false;
            }
            edited[7] = bank ? bank[6] : 255;
            edited[8] = bank ? bank[7] : 255;
            put32(edited + 11, draft.bank);
        }
        edited[6] = uint8_t(draft.values[0] - 1);
        edited[9] = draft.values[1] == -1 ? 255 : uint8_t(draft.values[1]);
        edited[10] = draft.values[2] == -65 ? 255 : uint8_t(draft.values[2] + 64);
        for (unsigned i = 0; i < document.patches(); ++i) {
            const int program = patch_edit_node(document, document.browser_root(i));
            if (program >= 0 && document.node(unsigned(program)) == node) {
                std::memcpy(bytes + (document.patch(i) - document.data()) + 4, edited + 6, 5);
                put32(bytes + (document.bank(i) - document.data()), draft.bank);
            }
        }
    } else if (draft.kind == PatchKind::channel_program) {
        edited[36] = uint8_t(0xc0 | (draft.values[1] - 1));
        edited[37] = uint8_t(draft.values[0] - 1);
    } else {
        edited[36] = uint8_t(0xb0 | (draft.values[2] - 1));
        edited[37] = uint8_t(draft.values[1]);
        edited[38] = uint8_t(draft.values[0]);
    }
    return binary::seal_setup(bytes, document.length());
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
