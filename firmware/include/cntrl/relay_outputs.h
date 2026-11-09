#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace cntrl {
// A qualified target maps logical engagement to its GPIO polarity and must
// declare only contacts whose circuitry and electrical limits are established.
struct RelayDriver {
    virtual ~RelayDriver() = default;
    virtual bool supports_relay(const char *) const { return false; }
    virtual bool relay_write(const char *, bool) { return false; }
};

class RelayOutputs {
public:
    explicit RelayOutputs(RelayDriver &driver) : driver_(driver) {}
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(relays_configure)
#endif
    bool configure(const uint8_t *records, unsigned count) {
        bool success = reset(); states_.resize(count);
        for (unsigned i = 0; i < count; ++i) {
            auto &state = states_[i]; state = State();
            std::memcpy(state.name, records + i * 33, 32);
            state.resting = records[i * 33 + 32];
            success = set(state, state.resting) && success;
        }
        return success;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(relays_reset)
#endif
    bool reset() {
        bool success = true;
        for (auto &state : states_) success = set(state, state.resting) && success;
        states_.clear(); return success;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(relays_execute)
#endif
    bool execute(const char *name, unsigned action, uint32_t duration, uint32_t now) {
        for (auto &state : states_) if (!std::strcmp(state.name, name)) {
            if (action > 3 || (action == 3 ? !duration || duration > 65535 : duration != 0)) return false;
            bool engaged = action == 0 ? true : action == 1 ? false : action == 2 ? !state.engaged : !state.resting;
            if (!set(state, engaged)) return false;
            state.pulsing = action == 3; state.started = now; state.duration = duration;
            return true;
        }
        return false;
    }
    bool tick(uint32_t now) {
        bool success = true;
        for (auto &state : states_) if (state.pulsing && uint32_t(now - state.started) >= state.duration) {
            state.pulsing = false; success = set(state, state.resting) && success;
        }
        return success;
    }
private:
    struct State {
        char name[32] = {};
        bool resting = false, engaged = false, known = false, pulsing = false;
        uint32_t started = 0, duration = 0;
    };
    RelayDriver &driver_;
    std::vector<State> states_;
    bool set(State &state, bool engaged) {
        if (state.known && state.engaged == engaged) return true;
        if (!driver_.relay_write(state.name, engaged)) return false;
        state.engaged = engaged; state.known = true; return true;
    }
};
} // namespace cntrl
