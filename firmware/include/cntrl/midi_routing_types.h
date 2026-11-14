#pragma once
#include "midi_bridge.h"
#include "checked_array.h"
#include "checked_list.h"
#include <algorithm>
#include <cstdint>

namespace cntrl::midi {
struct Emit {
    virtual ~Emit() = default;
    virtual bool emit(const Packet &packet, uint64_t delay = 0) = 0;
};
struct Processor {
    virtual ~Processor() = default;
    // A processor callback is one atomic preparation unit. Implementations
    // must bound their own work and honour a refused Emit; the graph can
    // yield between callbacks, not inside user processor code.
    virtual bool push(const Packet &packet, unsigned source, Emit &output) = 0;
    virtual void service(uint64_t, unsigned, Emit &) {}
    virtual void stop() {}
};
struct Field { uint8_t id = 0; CheckedArray<int32_t> values; };
struct Assignment { uint8_t field = 0, source = 255; int32_t value = 0; };
struct Input { uint8_t kind = 0; uint16_t index = 0; };
struct Work {
    Packet packet; uint64_t due, token; unsigned input; uint64_t owner;
    Parameter parameter = {}; uint64_t lfo = 0; uint8_t sample = 1;
    uint64_t delay = 0;
    uint64_t epoch = 0;
};
struct ParameterBinding { unsigned output; uint64_t owner; Parameter target; };
struct ParameterDelivery {
    unsigned output; uint64_t owner, lfo, identity;
    Parameter target;
    uint8_t off = 0, sample = 1;
    int final_value = 0, snapshot = Parameter::unknown, last = Parameter::unknown;
    int values[3] = {Parameter::unknown, Parameter::unknown, Parameter::unknown};
    ParameterTransaction flight = {};
    bool initial = false, touched = false, stopping = false, finished = false;
};
struct Capture {
    Work work;
    unsigned route, step;
    uint64_t start, end = 0, delay = 0, frame = 0;
    bool closed = false;
};
struct Visit { uint64_t token, owner; unsigned route, step, input; Packet onset; };
class BasicProcessing {
public:
    static constexpr bool advanced = false;
    struct Step {
        uint8_t kind = 0, field = 0;
        CheckedArray<Field> match;
        CheckedArray<Assignment> assignments;
    };
    struct Route {
        char name[64] = {};
        uint16_t scope = 65535, outputs = 0, failed_outputs = 0;
        CheckedArray<Input> inputs;
        CheckedArray<Step> steps;
        bool enabled = true, failed = false;
        uint64_t next = 0;
        bool started = false, foreign = false, note_admission_closed = false, activated = false;
        uint64_t preparation_epoch = 0;
    };
    template<class Core> struct State {};
    template<class Core> static bool numeric_fields(const typename Core::Step &step, typename Core::Work &work) {
        if (step.kind != 5 || !Core::matches(step,work)) return true;
        for (const auto &assignment : step.assignments) {
            if (work.parameter.kind) {
                if (!work.parameter.set(1,assignment.value)) return false;
            } else {
                // A channel remap leaves SysEx and system/realtime messages alone.
                if (Core::field(work.packet,1,work.input) < 0) return true;
                if (!Core::set(work.packet,1,assignment.value)) return false;
            }
        }
        return true;
    }
    template<class Core, class Reader>
    static bool decode_step(Core &, typename Core::Route &, typename Core::Step &step,
                            Reader &payload, unsigned, unsigned reserved, unsigned, unsigned) {
        if (reserved || (step.kind != 0 && step.kind != 1 && step.kind != 5) || !payload.match(step)) return false;
        for (const auto &match : step.match) {
            if (match.id != 1) return false;
            for (int value : match.values) if (value < 1 || value > 16) return false;
        }
        if (step.kind == 5) {
            if (payload.byte() != 1) return false;
            Assignment assignment;
            assignment.field = uint8_t(payload.byte()); assignment.source = uint8_t(payload.byte());
            assignment.value = int32_t(payload.integer(4));
            if (assignment.field != 1 || assignment.source != 255 || assignment.value < 1 || assignment.value > 16
                || !step.assignments.reserve(1) || !step.assignments.append(assignment)) return false;
        }
        return payload.ok;
    }
    template<class Core> static void process_step(Core &core, typename Core::Processing next) {
        const auto &step = core.routes_[next.route].steps[next.position];
        const bool matched = Core::matches(step,next.work);
        if ((step.kind == 0 && !matched) || (step.kind == 1 && matched)) return;
        if (!Core::numeric_fields(step,next.work)) return;
        core.process(next.route,next.position + 1,next.work);
    }
};
} // namespace cntrl::midi
