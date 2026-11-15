#pragma once
#include "checked_array.h"
#include "midi_packet.h"
#include "midi_parameter.h"

namespace cntrl::midi {
// Only enabled loops retain touched controls. Neutral declarations remain in
// the immutable setup document; ordinary performance uses no reset storage.
class LoopControlReset {
public:
    void configure(const uint8_t *settings, int enabled = -1) {
        enabled_ = enabled < 0 ? settings && settings[4] : enabled != 0;
        settings_ = settings; touched_.release(); serial_ = 0; active_ = false;
    }
    bool prepare(unsigned output, const Packet &packet, const Parameter *logical = nullptr) {
        Parameter parameter;
        unsigned needed = 0;
        for (unsigned i = 0; neutral(output,packet,logical,parameter,i); ++i)
            if (!contains(output,parameter)) ++needed;
        if (needed > UINT32_MAX - serial_) return false;
        return !needed || touched_.size() + needed <= touched_.capacity()
            || touched_.reserve((touched_.size() + needed + 7) / 8 * 8);
    }
    void accepted(unsigned output, const Packet &packet, const Parameter *logical = nullptr) {
        Parameter parameter;
        for (unsigned i = 0; neutral(output,packet,logical,parameter,i); ++i)
            if (!contains(output,parameter)) (void)touched_.append({parameter,++serial_,uint8_t(output),0});
    }
    void begin() { active_ = !touched_.empty(); }
    bool pending() const { return active_; }
    template<class Send> bool poll(unsigned available, Send send) {
        if (!active_) return true;
        for (unsigned i = 0; i < touched_.size(); ++i) {
            auto &entry = touched_[i];
            if (!(available & (1u << entry.output))) continue;
            ParameterTransaction transaction{entry.parameter,(uint64_t(1) << 62) | entry.serial,entry.position,0};
            if (!send(entry.output,transaction)) continue;
            entry.position = uint8_t(transaction.position);
            if (transaction.position == transaction.parameter.messages()) {
                remove(i);
            }
            active_ = !touched_.empty();
            return !active_;
        }
        return false;
    }
    void forget(unsigned output) {
        for (unsigned i = touched_.size(); i--;) if (touched_[i].output == output) remove(i);
        if (active_) active_ = !touched_.empty();
    }
private:
    struct Touch { Parameter parameter; uint32_t serial; uint8_t output, position; };
    const uint8_t *settings_ = nullptr;
    bool enabled_ = false;
    CheckedArray<Touch> touched_;
    uint32_t serial_ = 0;
    bool active_ = false;
    static unsigned u16(const uint8_t *p) { return p[0] | unsigned(p[1]) << 8; }
    bool contains(unsigned output,const Parameter &parameter) const {
        for (const auto &entry : touched_)
            if (entry.output == output && entry.parameter.same(parameter)) return true;
        return false;
    }
    void remove(unsigned index) {
        for (unsigned i = index + 1; i < touched_.size(); ++i) touched_[i - 1] = touched_[i];
        touched_.resize(touched_.size() - 1);
    }
    bool neutral(unsigned output,const Packet &packet,const Parameter *logical,Parameter &parameter,unsigned match) const {
        if (!enabled_ || output >= 16) return false;
        const unsigned kind = packet.bytes[0] >> 4, channel = packet.bytes[0] & 15;
        if (logical) {
            // Selector writes alone do not touch the parameter. A partially
            // delivered data write does, even if final selector nulling fails.
            if (kind != 11 || (logical->kind == Parameter::cc14
                ? packet.bytes[1] != logical->address && packet.bytes[1] != logical->address + 32
                : packet.bytes[1] != 6 && packet.bytes[1] != 38)) return false;
            parameter = *logical;
            if (logical->kind == Parameter::cc14) {
                parameter.kind = Parameter::cc7;
                parameter.address = packet.bytes[1];
            }
        } else {
            parameter.channel = uint8_t(channel);
            if (kind == 14) { parameter.kind = Parameter::bend; parameter.value = 0; return !match; }
            if (kind == 13) { parameter.kind = Parameter::pressure; parameter.value = 0; return !match; }
            if (kind == 10) {
                parameter.kind = Parameter::key_pressure; parameter.address = packet.bytes[1]; parameter.value = 0; return !match;
            }
            if (kind != 11) return false;
            parameter.kind = Parameter::cc7; parameter.address = packet.bytes[1];
        }
        for (unsigned i = 0; settings_ && i < u16(settings_ + 5); ++i) {
            const auto *p = settings_ + 7 + i * 7;
            Parameter target{p[1],p[2],uint16_t(u16(p + 3)),int32_t(u16(p + 5))};
            if (p[0] == output && target.overlaps(parameter) && !match--) {
                parameter = target; return true;
            }
        }
        return false;
    }
};
}
