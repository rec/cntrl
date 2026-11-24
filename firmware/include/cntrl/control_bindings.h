#pragma once
#include "document.h"
#include "memory.h"

namespace cntrl {
// Patch entry, title, expression assignments, then ordinary switch records
// followed by their gesture, hook and shifted-action records.
struct ControlBindings {
    const uint8_t *data = nullptr;
    explicit operator bool() const { return data != nullptr; }
    unsigned count() const { return data[66]; }
    const char *title() const { return reinterpret_cast<const char *>(data + 2); }
    const uint8_t *record(unsigned slot) const { return data + 220 + slot * binding_record_size; }
    const uint8_t *view() const { return (data[67] & 3) == 1 ? record(count()) : nullptr; }
    const uint8_t *style() const { return (data[67] & 3) ? record(count()) : nullptr; }
    uint8_t page_chord(unsigned direction) const { return data && (data[67] & 4) ? data[u16(data - 2) - 2 + direction] : 0; }
    const char *scene_status() const {
        return data && (data[67] & 8)
            ? reinterpret_cast<const char *>(data + u16(data - 2) - ((data[67] & 4) ? 66 : 64))
            : nullptr;
    }
    const uint8_t *find(unsigned input) const {
        for (unsigned i = 0; i < count(); ++i) if (record(i)[0] == input) return record(i);
        return nullptr;
    }
    ExpressionAssignment expression(unsigned input) const {
        const auto *p = data + 68 + input * expression_record_size;
        ExpressionAssignment result;
        std::memcpy(result.output, p, sizeof(result.output));
        result.channel = p[32]; result.controller = p[33]; result.minimum = p[34]; result.maximum = p[35];
        result.reverse = p[36]; result.curve = ExpressionCurve(p[37] & 3);
        result.mirror_route = p[37] >> 2 ? (p[37] >> 2) - 1 : 255;
        return result;
    }
};

// Each resolved switch retains its source document, so inherited actions keep
// their own patch names, instruments and references.
struct ResolvedBindings : CheckedHeapAllocation {
    struct Switch {
        const Document *document = nullptr;
        const uint8_t *record = nullptr;
        const uint8_t *visual = nullptr;
    };
    Switch switches[8] = {};
    ExpressionAssignment expressions[4] = {};
    unsigned count = 0;
    bool overlay(const Document &document, ControlBindings map, bool buttons, bool expression) {
        if (buttons) for (unsigned i = 0; i < map.count(); ++i) {
            const auto *record = map.record(i);
            unsigned at = 0;
            while (at < count && switches[at].record[0] != record[0]) ++at;
            if (at == count) { if (count == 8) return false; ++count; }
            switches[at] = {&document, record, map.view() ? map.view() + binding_view_header_size + i * binding_view_record_size : nullptr};
        }
        if (expression) for (unsigned i = 0; i < 4; ++i) {
            auto assignment = map.expression(i);
            if (assignment.channel) expressions[i] = assignment;
        }
        return true;
    }
    bool scope(const Document &document, unsigned kind, const char *name, unsigned inheritance) {
        int root = document.binding_scope(kind, name);
        return root < 0 || overlay(document, {document.node(unsigned(root)) + 4}, inheritance & 1, inheritance & 2);
    }
};
#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(ResolvedBindings) == 256, "Update banked.estimate_ram for resolved bindings");
#endif

struct BindingDocument : CheckedHeapAllocation {
    MemoryBuffer bytes;
    Document document;
    bool copy(const Document &source, const Hardware &hardware) {
        if (!bytes.resize(source.length())) return false;
        std::memcpy(bytes.data, source.data(), source.length());
        return document.load(bytes.data, bytes.size, hardware);
    }
};
} // namespace cntrl
