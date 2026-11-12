#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include "checked_list.h"
#include "checked_array.h"
#include "midi_parameter.h"

namespace cntrl::midi {
struct NoteState {
    uint8_t note;
    std::optional<uint8_t> velocity, pressure;
};

struct ControllerState {
    uint8_t controller, value;
    bool known = true;
};

struct ParameterState {
    uint16_t parameter;
    std::optional<uint16_t> value;
    bool registered = false;
    std::optional<uint8_t> coarse = {};
};

struct ChannelState {
    uint8_t channel;
    CheckedList<NoteState> notes;
    CheckedList<ControllerState> controllers;
    std::optional<uint8_t> program, pressure;
    std::optional<uint16_t> pitch_bend;
    std::optional<uint8_t> bank_msb, bank_lsb;
    CheckedList<ParameterState> parameters;
    bool nrpn = false, pitch_known = true;
};

struct OutputState {
    unsigned output;
    CheckedList<ChannelState> channels;
};
#if UINTPTR_MAX == UINT32_MAX
static_assert(CheckedList<NoteState>::node_size() == 32);
static_assert(CheckedList<ControllerState>::node_size() == 28);
static_assert(CheckedList<ParameterState>::node_size() == 36);
static_assert(CheckedList<ChannelState>::node_size() == 132);
static_assert(CheckedList<OutputState>::node_size() == 60);
#endif

// Last accepted channel messages, rather than a model of an instrument's voices.
// Channel numbers are zero based, as they are in MIDI status bytes.
class MidiState {
    struct Credits {
        CheckedList<NoteState> notes;
        CheckedList<ControllerState> controllers;
        CheckedList<ParameterState> parameters;
        CheckedList<OutputState> outputs;
        CheckedList<ChannelState> channels;
        bool active = false;
        unsigned target_keys = 0, target_contexts = 0, target_outputs = 0;
    };
    Credits credits_[2];
    struct Requirement { unsigned output, channel, kind, address; };
    struct OwnerState {
        const void *identity = nullptr;
        unsigned owner = 0, references = 0;
        bool current = false, sealed = false;
        CheckedObject<Credits> credit;
        CheckedList<Requirement> requirements;
    };
    CheckedList<OwnerState> owners_;
public:
    CheckedList<OutputState> outputs;
    unsigned revision() const { return revision_; }
    void forget(unsigned output) {
        auto o = std::find_if(outputs.begin(), outputs.end(), [output](const auto &v) { return v.output == output; });
        if (o != outputs.end()) { outputs.erase(o); ++revision_; }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_state_value)
#endif
    int value(unsigned output, unsigned channel, unsigned control) const {
        auto o = std::find_if(outputs.begin(), outputs.end(), [output](const auto &v) { return v.output == output; });
        if (o == outputs.end()) return -1;
        auto c = std::find_if(o->channels.begin(), o->channels.end(), [channel](const auto &v) { return v.channel == channel; });
        if (c == o->channels.end()) return -1;
        if (control == 128) return c->program ? *c->program : -1;
        if (control == 129) return c->pitch_bend && c->pitch_known ? *c->pitch_bend : -1;
        if (control == 130) return c->bank_msb ? *c->bank_msb : -1;
        if (control == 131) return c->bank_lsb ? *c->bank_lsb : -1;
        if (control >= 256) {
            bool registered = control >= 33024, coarse = control >= 16640 && !registered;
            unsigned parameter = control - (registered ? 33024 : coarse ? 16640 : 256);
            for (const auto &p : c->parameters) if (p.registered == registered && p.parameter == parameter)
                return coarse ? p.coarse ? *p.coarse : -1 : p.value ? *p.value : -1;
            return -1;
        }
        for (const auto &v : c->controllers)
            if (v.controller == control && v.known) return v.value;
        return -1;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_state_parameter_value)
#endif
    int parameter_value(unsigned output, const Parameter &parameter) const {
        for (const auto &o : outputs) if (o.output == output)
            for (const auto &c : o.channels) if (c.channel == parameter.channel) {
                if (parameter.kind == Parameter::cc7) { int v = value(output, c.channel, parameter.address); return v < 0 ? Parameter::unknown : v; }
                if (parameter.kind == Parameter::cc14) {
                    int hi = value(output, c.channel, parameter.address), lo = value(output, c.channel, parameter.address + 32);
                    return hi < 0 || lo < 0 ? Parameter::unknown : hi * 128 + lo;
                }
                if (parameter.kind == Parameter::bend) return c.pitch_known && c.pitch_bend ? int(*c.pitch_bend) - 8192 : Parameter::unknown;
                if (parameter.kind == Parameter::pressure) return c.pressure ? *c.pressure : Parameter::unknown;
                if (parameter.kind == Parameter::program) return c.program ? *c.program + 1 : Parameter::unknown;
                if (parameter.kind == Parameter::key_pressure) {
                    for (const auto &n : c.notes) if (n.note == parameter.address) return n.pressure ? *n.pressure : Parameter::unknown;
                    return Parameter::unknown;
                }
                for (const auto &p : c.parameters) if (p.parameter == parameter.address && p.registered == (parameter.kind == Parameter::rpn14))
                    return parameter.kind == Parameter::nrpn7 ? p.coarse ? *p.coarse : Parameter::unknown : p.value ? *p.value : Parameter::unknown;
            }
        return Parameter::unknown;
    }
    void forget_parameter(unsigned output, const Parameter &parameter) {
        for (auto &o : outputs) if (o.output == output)
            for (auto &c : o.channels) if (c.channel == parameter.channel) {
                for (auto &v : c.controllers)
                    if ((parameter.kind == Parameter::cc7 && v.controller == parameter.address)
                        || (parameter.kind == Parameter::cc14 && (v.controller == parameter.address || v.controller == parameter.address + 32))) v.known = false;
                if (parameter.kind == Parameter::bend) c.pitch_known = false;
                if (parameter.kind == Parameter::pressure) c.pressure.reset();
                if (parameter.kind == Parameter::program) c.program.reset();
                if (parameter.kind == Parameter::key_pressure) for (auto &n : c.notes) if (n.note == parameter.address) n.pressure.reset();
                if (parameter.kind == Parameter::nrpn7 || parameter.kind == Parameter::nrpn14 || parameter.kind == Parameter::rpn14)
                    for (auto &p : c.parameters) if (p.parameter == parameter.address && p.registered == (parameter.kind == Parameter::rpn14)) { p.value.reset(); p.coarse.reset(); }
                ++revision_;
            }
    }
private:
    friend struct Sink;
    OwnerState *owner_state(const void *identity, unsigned owner, bool create = false) {
        for (auto &entry : owners_) if (entry.identity == identity && entry.owner == owner) return &entry;
        if (!create) return nullptr;
        OwnerState entry; entry.identity = identity; entry.owner = owner;
        return owners_.append(std::move(entry));
    }
    bool retain_owner(const void *identity, unsigned owner) {
        auto *entry = owner_state(identity, owner, true);
        if (!entry || entry->references == UINT32_MAX) return false;
        if (!entry->references) { entry->sealed = false; entry->requirements.clear(); }
        ++entry->references; return true;
    }
    void release_owner(const void *identity) {
        for (auto &entry : owners_) if (entry.identity == identity && entry.references) {
            if (!--entry.references) {
                entry.current = false; entry.sealed = false; entry.requirements.clear();
                if (entry.credit) { entry.credit->active = false; entry.credit->target_keys = entry.credit->target_contexts = entry.credit->target_outputs = 0; }
            }
        }
    }
    void activate_owner(const void *identity, unsigned owner) {
        for (auto &entry : owners_) if (entry.owner == owner) entry.current = entry.identity == identity;
    }
    const void *active_identity(unsigned owner) const {
        for (const auto &entry : owners_) if (entry.owner == owner && entry.current) return entry.identity;
        return nullptr;
    }
    bool owner_sealed(const void *identity, unsigned owner) {
        auto *entry = owner_state(identity, owner); return entry && entry->sealed;
    }
    bool requirement(OwnerState &entry, unsigned output, unsigned channel, unsigned kind, unsigned address) {
        if (kind != 4) for (const auto &r : entry.requirements)
            if (r.output == output && r.channel == channel && r.kind == kind && r.address == address) return true;
        if (entry.sealed) {
            if (kind == 4) for (const auto &r : entry.requirements) if (r.output == output && r.channel == channel && r.kind == 4) return true;
            return false;
        }
        return entry.requirements.append({output, channel, kind, address}) != nullptr;
    }
    bool require_packet(const void *identity, unsigned owner, unsigned output, const uint8_t *bytes, unsigned size, bool generic_parameter) {
        if (!size || bytes[0] < 0x80 || bytes[0] >= 0xf0) return true;
        auto *entry = owner_state(identity, owner, true); if (!entry) return false;
        const unsigned kind = bytes[0] >> 4, channel = bytes[0] & 15;
        if (!requirement(*entry, output, 0, 0, 0) || !requirement(*entry, output, channel, 1, 0)) return false;
        if ((kind == 9 && bytes[2]) || kind == 10) return requirement(*entry, output, channel, 2, bytes[1]);
        if (kind == 11) return requirement(*entry, output, channel, 3, bytes[1])
            && (!generic_parameter || (bytes[1] != 6 && bytes[1] != 38) || requirement(*entry, output, channel, 4, 0));
        return true;
    }
    bool require_parameter(const void *identity, unsigned owner, unsigned output, const Parameter &parameter) {
        if (parameter.kind != Parameter::nrpn7 && parameter.kind != Parameter::nrpn14 && parameter.kind != Parameter::rpn14) return true;
        auto *entry = owner_state(identity, owner, true);
        return entry && requirement(*entry, output, parameter.channel, 5,
            parameter.address + (parameter.kind == Parameter::rpn14 ? 16384 : 0));
    }
    bool refill_owner(OwnerState &entry) {
        if (entry.requirements.empty()) return true;
        if (!entry.credit && !entry.credit.create()) return false;
        auto &credit = *entry.credit;
        unsigned counts[5] = {};
        for (const auto &r : entry.requirements) ++counts[r.kind >= 4 ? 4 : r.kind];
        const unsigned previous[] = {credit.outputs.capacity(), credit.channels.capacity(), credit.notes.capacity(), credit.controllers.capacity(), credit.parameters.capacity()};
        const bool ready = credit.outputs.reserve(counts[0]) && credit.channels.reserve(counts[1])
            && credit.notes.reserve(counts[2]) && credit.controllers.reserve(counts[3]) && credit.parameters.reserve(counts[4]);
        if (!ready) {
            credit.outputs.trim(credit.outputs.capacity() - previous[0]); credit.channels.trim(credit.channels.capacity() - previous[1]);
            credit.notes.trim(credit.notes.capacity() - previous[2]); credit.controllers.trim(credit.controllers.capacity() - previous[3]);
            credit.parameters.trim(credit.parameters.capacity() - previous[4]); return false;
        }
        credit.active = true;
        return true;
    }
    bool finish_requirements(const void *identity, unsigned owner) {
        auto *entry = owner_state(identity, owner);
        return !entry || entry->sealed || refill_owner(*entry);
    }
    bool prepare_owner_cycle(const void *identity) {
        for (auto &entry : owners_) if (entry.identity == identity) {
            if (!refill_owner(entry)) return false;
            entry.sealed = true; return true;
        }
        return true;
    }
    bool begin_stream(unsigned owner) {
        const void *identity = active_identity(owner);
        auto *entry = owner_state(identity, owner, true);
        if (!entry) return false;
        if (!entry->references) {
            entry->requirements.clear(); entry->sealed = false;
        } else entry->sealed = true;
        entry->current = true; return true;
    }
    bool begin_identity_stream(const void *identity, unsigned owner) {
        auto *entry = owner_state(identity, owner, true);
        if (!entry) return false;
        entry->requirements.clear(); entry->sealed = false; entry->current = true;
        return true;
    }
    void end_identity_stream(const void *identity, unsigned owner) {
        if (auto *entry = owner_state(identity, owner)) {
            entry->current = false; entry->requirements.clear(); entry->sealed = false;
            if (entry->credit) entry->credit->active = false;
        }
    }
    void seal_stream(unsigned owner) {
        if (auto *entry = owner_state(active_identity(owner), owner)) entry->sealed = true;
    }
    void end_stream(unsigned owner) {
        if (auto *entry = owner_state(active_identity(owner), owner)) {
            if (!entry->references) {
                entry->current = false; entry->requirements.clear(); entry->sealed = false;
                if (entry->credit) entry->credit->active = false;
            }
        }
    }
    bool reserve_playback(unsigned keys, unsigned output_mask, unsigned channels, unsigned kind) {
        if (kind >= 2) return false;
        auto &credit = credits_[kind];
        if (!keys) {
            credit.active = false;
            credit.target_keys = credit.target_contexts = credit.target_outputs = 0;
            return true;
        }
        unsigned output_count = 0, channel_count = 0;
        for (unsigned mask = output_mask; mask; mask >>= 1) output_count += mask & 1;
        for (unsigned mask = channels; mask; mask >>= 1) channel_count += mask & 1;
        const unsigned old_outputs = credit.outputs.capacity(), old_channels = credit.channels.capacity();
        const unsigned old_notes = credit.notes.capacity(), old_controllers = credit.controllers.capacity(), old_parameters = credit.parameters.capacity();
        const bool ready = credit.outputs.reserve(output_count) && credit.channels.reserve(output_count * channel_count)
            && credit.notes.reserve(keys) && credit.controllers.reserve(keys + 6 * output_count * channel_count)
            && credit.parameters.reserve(keys);
        if (ready) {
            credit.active = true; credit.target_keys = std::max(credit.target_keys, keys);
            credit.target_contexts = std::max(credit.target_contexts, output_count * channel_count);
            credit.target_outputs = std::max(credit.target_outputs, output_count);
        }
        else {
            credit.outputs.trim(credit.outputs.capacity() - old_outputs); credit.channels.trim(credit.channels.capacity() - old_channels);
            credit.notes.trim(credit.notes.capacity() - old_notes); credit.controllers.trim(credit.controllers.capacity() - old_controllers);
            credit.parameters.trim(credit.parameters.capacity() - old_parameters);
        }
        return ready;
    }
    unsigned shrink_playback(unsigned keys, unsigned kind, unsigned budget) {
        if (kind >= 2) return 0;
        auto &credit = credits_[kind];
        credit.target_keys = std::min(credit.target_keys, keys);
        if (!keys) { credit.active = false; credit.target_contexts = credit.target_outputs = 0; }
        unsigned retired = 0;
        auto trim = [&](auto &list, unsigned target) {
            if (list.capacity() > target && retired < budget)
                retired += list.trim(std::min(budget - retired, list.capacity() - target));
        };
        trim(credit.notes, credit.target_keys);
        trim(credit.controllers, credit.target_keys + 6 * credit.target_contexts);
        trim(credit.parameters, credit.target_keys);
        trim(credit.outputs, credit.target_outputs);
        trim(credit.channels, credit.target_contexts);
        return retired;
    }
    unsigned reclaim(unsigned budget) {
        unsigned retired = shrink_playback(credits_[0].target_keys, 0, budget);
        if (retired < budget) retired += shrink_playback(credits_[1].target_keys, 1, budget - retired);
        for (auto i = owners_.begin(); i != owners_.end() && retired < budget;) {
            auto &entry = *i;
            if (entry.references || entry.current) { ++i; continue; }
            if (entry.credit) {
                auto &c = *entry.credit;
                auto trim = [&](auto &list) { if (retired < budget) retired += list.trim(budget - retired); };
                trim(c.notes); trim(c.controllers); trim(c.parameters); trim(c.channels); trim(c.outputs);
                if (c.notes.loans() || c.controllers.loans() || c.parameters.loans() || c.channels.loans() || c.outputs.loans()
                    || c.notes.capacity() || c.controllers.capacity() || c.parameters.capacity() || c.channels.capacity() || c.outputs.capacity()) { ++i; continue; }
                entry.credit.reset(); ++retired;
            }
            if (retired < budget) retired += entry.requirements.trim(budget - retired);
            if (entry.requirements.capacity() || retired >= budget) { ++i; continue; }
            i = owners_.erase(i);
        }
        if (retired < budget) retired += owners_.trim(budget - retired);
        return retired;
    }
    template<class T> static bool obtain(CheckedList<T> &target, CheckedList<T> &credit, unsigned required, bool protected_write) {
        if (required <= target.capacity()) return true;
        const unsigned missing = required - target.capacity();
        return protected_write ? target.take_reserved(credit, missing) : target.reserve(required);
    }
    void return_unused() {
        for (auto &o : outputs) {
            for (auto &c : o.channels) { c.notes.return_reserved(); c.controllers.return_reserved(); c.parameters.return_reserved(); }
            o.channels.return_reserved();
        }
        outputs.return_reserved();
    }
    OutputState *prepared_output_ = nullptr;
    ChannelState *prepared_channel_ = nullptr;
    void finish_preparation(bool commit) {
        if (!commit && prepared_output_) outputs.erase_if([&](const auto &o) { return &o == prepared_output_; });
        else if (!commit && prepared_channel_) {
            for (auto &o : outputs) o.channels.erase_if([&](const auto &c) { return &c == prepared_channel_; });
        }
        prepared_output_ = nullptr; prepared_channel_ = nullptr;
        if (!commit) return_unused();
    }
    bool prepare(unsigned output, const uint8_t *bytes, unsigned size, const Parameter *parameter = nullptr, unsigned kind_of_write = 2, const void *identity = nullptr, unsigned owner = 0) {
        finish_preparation(false);
        auto *owner_entry = kind_of_write == 3 ? owner_state(identity, owner) : nullptr;
        const bool owner_credit = owner_entry && owner_entry->credit && owner_entry->credit->active;
        const bool protected_write = owner_credit || (kind_of_write < 2 && credits_[kind_of_write].active);
        auto &credit = owner_credit ? *owner_entry->credit : credits_[kind_of_write < 2 ? kind_of_write : 0];
        if (!size || bytes[0] < 0x80 || bytes[0] >= 0xf0) return true;
        if (kind_of_write == 3 && !owner_credit) return false;
        const unsigned kind = bytes[0] >> 4;
        if (size != (kind == 12 || kind == 13 ? 2u : 3u)) return true;
        const bool off = kind == 8 || (kind == 9 && !bytes[2]);
        const bool release = off || (kind == 11 && bytes[1] >= 120);
        auto o = std::find_if(outputs.begin(), outputs.end(), [output](const auto &v) { return v.output == output; });
        OutputState *out = o == outputs.end() ? nullptr : &*o;
        if (!out) {
            if (release) return true;
            if (!obtain(outputs, credit.outputs, outputs.size() + 1, protected_write)) return false;
            out = outputs.append({output, {}});
            if (!out) return false;
            prepared_output_ = out;
        }
        const unsigned channel = bytes[0] & 15;
        auto c = std::find_if(out->channels.begin(), out->channels.end(), [channel](const auto &v) { return v.channel == channel; });
        ChannelState *state = c == out->channels.end() ? nullptr : &*c;
        if (!state) {
            if (release) return true;
            ChannelState created{}; created.channel = uint8_t(channel);
            if (!obtain(out->channels, credit.channels, out->channels.size() + 1, protected_write)) { finish_preparation(false); return false; }
            state = out->channels.append(std::move(created));
            if (!state) { finish_preparation(false); return false; }
            prepared_channel_ = state;
        }
        bool ready = true;
        if (parameter) {
            uint8_t missing[6] = {}; unsigned count = 0;
            for (unsigned i = 0; i < parameter->messages(); ++i) {
                uint8_t message[3] = {}; parameter->message(i, message);
                if (message[0] >> 4 != 11) continue;
                bool found = false;
                for (const auto &entry : state->controllers) if (entry.controller == message[1]) found = true;
                for (unsigned j = 0; j < count; ++j) if (missing[j] == message[1]) found = true;
                if (!found) missing[count++] = message[1];
            }
            ready = obtain(state->controllers, credit.controllers, state->controllers.size() + count, protected_write);
            if (parameter->kind == Parameter::nrpn7 || parameter->kind == Parameter::nrpn14 || parameter->kind == Parameter::rpn14) {
                auto existing = std::find_if(state->parameters.begin(), state->parameters.end(), [&](const auto &entry) {
                    return entry.parameter == parameter->address && entry.registered == (parameter->kind == Parameter::rpn14);
                });
                ready = ready && (existing != state->parameters.end() || obtain(state->parameters, credit.parameters, state->parameters.size() + 1, protected_write));
            }
            if (parameter->kind == Parameter::key_pressure) {
                auto existing = std::find_if(state->notes.begin(), state->notes.end(), [&](const auto &entry) { return entry.note == parameter->address; });
                ready = ready && (existing != state->notes.end() || obtain(state->notes, credit.notes, state->notes.size() + 1, protected_write));
            }
        }
        else if ((kind == 9 && bytes[2]) || kind == 10) {
            auto n = std::find_if(state->notes.begin(), state->notes.end(), [&](const auto &v) { return v.note == bytes[1]; });
            ready = n != state->notes.end() || obtain(state->notes, credit.notes, state->notes.size() + 1, protected_write);
        } else if (kind == 11) {
            auto v = std::find_if(state->controllers.begin(), state->controllers.end(), [&](const auto &s) { return s.controller == bytes[1]; });
            ready = v != state->controllers.end() || obtain(state->controllers, credit.controllers, state->controllers.size() + 1, protected_write);
            if (!ready && bytes[1] >= 120) ready = true;
            if (ready && (bytes[1] == 6 || bytes[1] == 38)) {
                int hi = value(output, channel, state->nrpn ? 99 : 101), lo = value(output, channel, state->nrpn ? 98 : 100);
                if (hi >= 0 && lo >= 0 && (hi != 127 || lo != 127)) {
                    auto p = std::find_if(state->parameters.begin(), state->parameters.end(), [&](const auto &entry) {
                        return entry.parameter == hi * 128 + lo && entry.registered == !state->nrpn;
                    });
                    ready = p != state->parameters.end() || obtain(state->parameters, credit.parameters, state->parameters.size() + 1, protected_write);
                }
            }
        }
        if (!ready) finish_preparation(false);
        return ready;
    }
    unsigned revision_ = 0;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_state_record)
#endif
    void record(unsigned output, const uint8_t *bytes, unsigned size) {
        record_data(output, bytes, size); return_unused();
    }
    void record_data(unsigned output, const uint8_t *bytes, unsigned size) {
        finish_preparation(true);
        if (size && (bytes[0] == 0xf0 || bytes[0] == 0xff)) { forget(output); return; }
        if (!size || bytes[0] < 0x80 || (bytes[0] >= 0xf0 && bytes[0] != 0xff)) return;
        auto o = std::find_if(outputs.begin(), outputs.end(), [output](const auto &v) { return v.output == output; });
        if (bytes[0] >= 0xf0) return;
        const unsigned kind = bytes[0] >> 4;
        if (size != (kind == 12 || kind == 13 ? 2u : 3u)) return;
        const uint8_t channel = bytes[0] & 15;
        const bool off = kind == 8 || (kind == 9 && !bytes[2]);
        const bool release = off || (kind == 11 && bytes[1] >= 120);
        if (o == outputs.end()) {
            if (release) return;
            auto *created = outputs.append({output, {}}); if (!created) return;
            o = std::find_if(outputs.begin(), outputs.end(), [&](const auto &v) { return &v == created; });
        }
        auto &channels = o->channels;
        auto c = std::find_if(channels.begin(), channels.end(), [channel](const auto &v) { return v.channel == channel; });
        if (c == channels.end()) {
            if (release) return;
            ChannelState state{}; state.channel = channel;
            auto *created = channels.append(std::move(state)); if (!created) return;
            c = std::find_if(channels.begin(), channels.end(), [&](const auto &v) { return &v == created; });
        }
        ++revision_;
        if (kind <= 10) {
            auto &notes = c->notes;
            auto n = std::find_if(notes.begin(), notes.end(), [bytes](const auto &v) { return v.note == bytes[1]; });
            if (off) {
                if (n != notes.end()) notes.erase(n);
            } else {
                if (n == notes.end()) {
                    auto *created = notes.append({bytes[1], {}, {}}); if (!created) return;
                    n = std::find_if(notes.begin(), notes.end(), [&](const auto &v) { return &v == created; });
                }
                if (kind == 9) n->velocity = bytes[2];
                else n->pressure = bytes[2];
            }
        } else if (kind == 11) {
            const unsigned controller = bytes[1];
            if (controller == 120 || controller >= 123) c->notes.clear();
            if (controller == 121) {
                // Reset values depend on the receiver. Invalidate affected state
                // instead of claiming that the previous values still apply.
                c->controllers.clear(); c->pressure.reset(); c->pitch_bend.reset();
                for (auto &n : c->notes) n.pressure.reset();
                c->parameters.clear();
            }
            auto &controllers = c->controllers;
            auto v = std::find_if(controllers.begin(), controllers.end(), [controller](const auto &s) { return s.controller == controller; });
            if (v == controllers.end()) {
                if (controller >= 120 && controllers.size() == controllers.capacity()) return;
                if (!controllers.append({bytes[1], bytes[2]})) return;
            }
            else { v->value = bytes[2]; v->known = true; }
            if (controller == 99 || controller == 98 || controller == 101 || controller == 100) {
                c->nrpn = controller == 99 || controller == 98;
                for (auto &entry : controllers) if (entry.controller == 6 || entry.controller == 38) entry.known = false;
            }
            if (controller == 96 || controller == 97) {
                c->parameters.clear();
                for (auto &entry : controllers) if (entry.controller == 6 || entry.controller == 38) entry.known = false;
            }
            if (controller == 6 || controller == 38) {
                int high = value(output, channel, c->nrpn ? 99 : 101), low = value(output, channel, c->nrpn ? 98 : 100);
                if (high >= 0 && low >= 0 && (high != 127 || low != 127)) {
                    uint16_t parameter = uint16_t(high * 128 + low);
                    std::optional<uint16_t> data;
                    int msb = -1, lsb = -1;
                    for (const auto &entry : controllers) if (entry.known) {
                        if (entry.controller == 6) msb = entry.value;
                        if (entry.controller == 38) lsb = entry.value;
                    }
                    if (msb >= 0 && lsb >= 0) data = uint16_t(msb * 128 + lsb);
                    auto p = std::find_if(c->parameters.begin(), c->parameters.end(), [&](const auto &v) { return v.parameter == parameter && v.registered == !c->nrpn; });
                    if (p == c->parameters.end()) {
                        auto *created = c->parameters.append({parameter, data, !c->nrpn, {}}); if (!created) return;
                        p = std::find_if(c->parameters.begin(), c->parameters.end(), [&](const auto &v) { return &v == created; });
                    }
                    else p->value = data;
                    p->coarse = msb < 0 ? std::nullopt : std::optional<uint8_t>(uint8_t(msb));
                }
            }
        } else if (kind == 12) {
            int msb = value(output, channel, 0), lsb = value(output, channel, 32);
            c->bank_msb = msb >= 0 ? std::optional<uint8_t>(uint8_t(msb)) : std::nullopt;
            c->bank_lsb = lsb >= 0 ? std::optional<uint8_t>(uint8_t(lsb)) : std::nullopt;
            c->program = bytes[1]; c->parameters.clear(); c->pitch_known = false;
            for (auto &entry : c->controllers) entry.known = entry.controller == 0 || entry.controller == 32;
        }
        else if (kind == 13) c->pressure = bytes[1];
        else { c->pitch_bend = uint16_t(bytes[1] | unsigned(bytes[2]) << 7); c->pitch_known = true; }
    }
};
} // namespace cntrl::midi
