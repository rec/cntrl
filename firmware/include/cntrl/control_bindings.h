#pragma once
#include "document.h"

namespace cntrl {
// Patch entry, title, expression assignments, then ordinary switch records
// followed by their gesture, hook and shifted-action records.
struct ControlBindings {
    const uint8_t *data = nullptr;
    explicit operator bool() const { return data != nullptr; }
    unsigned count() const { return data[66]; }
    const char *title() const { return reinterpret_cast<const char *>(data + 2); }
    const uint8_t *record(unsigned slot) const { return data + 220 + slot * binding_record_size; }
    const uint8_t *find(unsigned input) const {
        for (unsigned i = 0; i < count(); ++i) if (record(i)[0] == input) return record(i);
        return nullptr;
    }
    ExpressionAssignment expression(unsigned input) const {
        const auto *p = data + 68 + input * expression_record_size;
        ExpressionAssignment result;
        std::memcpy(result.output, p, sizeof(result.output));
        result.channel = p[32]; result.controller = p[33]; result.minimum = p[34]; result.maximum = p[35];
        result.reverse = p[36]; result.curve = ExpressionCurve(p[37]);
        return result;
    }
};
} // namespace cntrl
