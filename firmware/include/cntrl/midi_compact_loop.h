#pragma once
#include "midi_file.h"
#include <cstring>

namespace cntrl::midi {
// The byte stream carries complete channel messages. Times are relative musical
// ticks; sparse checkpoints bound random access to at most 31 message lengths.
class CompactLoop {
public:
    static constexpr uint64_t beat = uint64_t(1) << 32;
    static constexpr unsigned ticks_per_beat = 1u << 24;
    struct Group { uint16_t first; uint8_t count, reserved = 0; };
    bool reserve(unsigned events, unsigned bytes) {
        const unsigned event_capacity = (events + 31) / 32 * 32;
        const unsigned byte_capacity = (bytes + 95) / 96 * 96;
        return grow(times_,event_capacity * 4) && grow(bytes_,byte_capacity)
            && grow(checkpoints_,((events + 31) / 32) * 4);
    }
    bool append(const Packet *packets, unsigned count, uint64_t time, unsigned terminal = 0) {
        unsigned bytes = 0;
        for (unsigned i = 0; i < count; ++i) bytes += packets[i].size();
        if (!reserve(count_ + count + terminal,used_ + bytes + terminal * 3)
            || (count > 1 && !grow(groups_,((groups_count_ + 8) / 8) * 8 * sizeof(Group)))) return false;
        unsigned row = count_;
        const uint32_t tick = uint32_t((time + 128) >> 8);
        while (row && times()[row - 1] > tick) --row;
        const unsigned offset = byte_offset(row);
        std::memmove(bytes_.data + offset + bytes,bytes_.data + offset,used_ - offset);
        std::memmove(times() + row + count,times() + row,(count_ - row) * 4);
        unsigned cursor = offset;
        for (unsigned i = 0; i < count; ++i) {
            std::memcpy(bytes_.data + cursor,packets[i].bytes,packets[i].size()); cursor += packets[i].size();
            times()[row + i] = tick;
        }
        for (unsigned i = 0; i < groups_count_; ++i) if (groups()[i].first >= row) groups()[i].first += uint16_t(count);
        if (count > 1) {
            unsigned at = 0;
            while (at < groups_count_ && groups()[at].first < row) ++at;
            std::memmove(groups() + at + 1,groups() + at,(groups_count_ - at) * sizeof(Group));
            groups()[at] = {uint16_t(row),uint8_t(count),0}; ++groups_count_;
        }
        if (!(row % 32)) reinterpret_cast<uint32_t *>(checkpoints_.data)[row / 32] = offset;
        count_ += count; used_ += bytes;
        rebuild_checkpoints(row / 32);
        reverse_.clear();
        return true;
    }
    bool event(unsigned row, MidiFilePlayer::Event &event) const {
        if (row >= count_) return false;
        event = {};
        event.time = uint64_t(times()[row]) << 8;
        const uint8_t *bytes = bytes_.data + byte_offset(row);
        event.status = bytes[0]; event.first = bytes[1];
        event.second = message_size(bytes[0]) == 3 ? bytes[2] : 0;
        if (reverse_.size) event.reverse_value = reverse_.data[row];
        unsigned first = 0, end = groups_count_;
        while (first < end) {
            unsigned middle = first + (end - first) / 2;
            if (groups()[middle].first <= row) first = middle + 1;
            else end = middle;
        }
        if (first) {
            const auto &group = groups()[first - 1];
            if (row < unsigned(group.first) + group.count) {
                event.previous = unsigned(group.first) + 1; event.next = unsigned(group.first) + group.count + 1;
            }
        }
        return true;
    }
    bool reversed(unsigned row, MidiFilePlayer::Event &event) const {
        if (row >= count_ || !event_at(count_ - row - 1,event)) return false;
        if (event.previous) {
            unsigned first = event.previous - 1, end = event.next - 1;
            unsigned part = row - (count_ - end);
            if (!event_at(first + part,event)) return false;
            event.previous = count_ - end + 1; event.next = count_ - first + 1;
        }
        if (event.time > length) return false;
        event.time = length - event.time;
        Packet packet{uint8_t(event.status >> 4),{event.status,event.first,event.second}};
        if (!reverse_note(packet,event.reverse_value)) return false;
        event.status = packet.bytes[0]; event.second = packet.bytes[2];
        if (packet.cin == 11 && event.first >= 64 && event.first <= 69) std::swap(event.second,event.reverse_value);
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(compact_loop_prepare_reverse)
#endif
    bool prepare_reverse() {
        if (!count_) return true;
        if (reverse_.size == count_) return true;
        MemoryBuffer values;
        if (!values.resize(count_)) return false;
        std::memset(values.data,0,count_);
        struct Onset { uint8_t channel, note, velocity, padding; };
        CheckedArray<Onset> onsets;
        uint8_t holds[16][6] = {};
        for (unsigned i = 0; i < count_; ++i) {
            MidiFilePlayer::Event e; if (!event(i,e)) return false;
            unsigned kind = e.status >> 4, channel = e.status & 15;
            if (kind == 9 && e.second) {
                if (onsets.size() == onsets.capacity() && !onsets.reserve(onsets.size() + 8)) return false;
                if (!onsets.append({uint8_t(channel),e.first,e.second,0})) return false;
            } else if (kind == 8 || (kind == 9 && !e.second)) {
                unsigned j = 0;
                while (j < onsets.size() && (onsets[j].channel != channel || onsets[j].note != e.first)) ++j;
                if (j == onsets.size()) return false;
                values.data[i] = onsets[j].velocity;
                for (unsigned k = j + 1; k < onsets.size(); ++k) onsets[k - 1] = onsets[k];
                onsets.resize(onsets.size() - 1);
            } else if (kind == 11 && e.first >= 64 && e.first <= 69) {
                values.data[i] = holds[channel][e.first - 64]; holds[channel][e.first - 64] = e.second;
            }
        }
        reverse_.swap(values); return true;
    }
    void release_reverse() { reverse_.clear(); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(compact_loop_trim)
#endif
    bool trim() {
        return times_.resize(count_ * 4) && bytes_.resize(used_)
            && groups_.resize(groups_count_ * sizeof(Group)) && checkpoints_.resize(((count_ + 31) / 32) * 4);
    }
    void clear() {
        times_.clear(); bytes_.clear(); groups_.clear(); checkpoints_.clear(); reverse_.clear();
        count_ = used_ = groups_count_ = 0; length = 0;
    }
    void swap(CompactLoop &other) {
        times_.swap(other.times_); bytes_.swap(other.bytes_); groups_.swap(other.groups_);
        checkpoints_.swap(other.checkpoints_); reverse_.swap(other.reverse_);
        std::swap(count_,other.count_); std::swap(used_,other.used_); std::swap(groups_count_,other.groups_count_); std::swap(length,other.length);
    }
    unsigned count() const { return count_; }
    unsigned payload_bytes() const { return used_ + count_ * 4; }
    unsigned storage_bytes() const {
        unsigned bytes = 0;
        for (const auto *buffer : {&times_,&bytes_,&groups_,&checkpoints_,&reverse_})
            bytes += unsigned(memory_allocation_charge(buffer->data,buffer->size));
        return bytes;
    }
    uint64_t length = 0;
private:
    MemoryBuffer times_, bytes_, groups_, checkpoints_, reverse_;
    unsigned count_ = 0, used_ = 0, groups_count_ = 0;
    uint32_t *times() { return reinterpret_cast<uint32_t *>(times_.data); }
    const uint32_t *times() const { return reinterpret_cast<const uint32_t *>(times_.data); }
    Group *groups() { return reinterpret_cast<Group *>(groups_.data); }
    const Group *groups() const { return reinterpret_cast<const Group *>(groups_.data); }
    static unsigned message_size(uint8_t status) { return (status >> 4 == 12 || status >> 4 == 13) ? 2 : 3; }
    static bool grow(MemoryBuffer &storage, unsigned size) { return size <= storage.size || allocate(storage,size); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(compact_loop_allocate)
#endif
    static bool allocate(MemoryBuffer &storage, unsigned size) { return storage.resize(size); }
    unsigned byte_offset(unsigned row) const {
        if (row == count_) return used_;
        const auto *checkpoints = reinterpret_cast<const uint32_t *>(checkpoints_.data);
        unsigned first = row / 32 * 32, offset = checkpoints[first / 32];
        for (unsigned i = first; i < row; ++i) offset += message_size(bytes_.data[offset]);
        return offset;
    }
    void rebuild_checkpoints(unsigned block) {
        auto *checkpoints = reinterpret_cast<uint32_t *>(checkpoints_.data);
        unsigned first = block * 32, offset = first ? checkpoints[block] : 0;
        for (unsigned i = first; i < count_; ++i) {
            if (!(i % 32)) checkpoints[i / 32] = offset;
            offset += message_size(bytes_.data[offset]);
        }
    }
    bool event_at(unsigned row, MidiFilePlayer::Event &result) const { return event(row,result); }
};

// A read-only SMF view is used only for explicit Save. It streams conversion
// without retaining a second copy of the phrase in file representation.
class CompactLoopSmf final : public MidiFile {
public:
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(compact_loop_smf_bind)
#endif
    void bind(const CompactLoop &loop) {
        loop_ = &loop; size_ = 29; uint64_t previous = 0;
        for (unsigned i = 0; i < loop.count(); ++i) {
            MidiFilePlayer::Event event; loop.event(i,event);
            uint64_t now = ticks(event.time);
            size_ += variable_size(now - previous) + (event.status >> 4 == 12 || event.status >> 4 == 13 ? 2 : 3); previous = now;
        }
        size_ += variable_size(ticks(loop.length) - previous) + 3;
        reset();
    }
    uint32_t size() const override { return size_; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(compact_loop_smf_read)
#endif
    bool read(uint32_t offset, uint8_t *data, unsigned count) override {
        if (!loop_ || offset > size_ || count > size_ - offset) return false;
        if (offset < offset_) reset();
        while (count) {
            if (offset >= offset_ + part_size_) { if (!next()) return false; continue; }
            unsigned start = offset - offset_, n = std::min(count,part_size_ - start);
            std::memcpy(data,part_ + start,n); data += n; offset += n; count -= n;
        }
        return true;
    }
    bool write(uint32_t, const uint8_t *, unsigned) override { return false; }
    bool sync() override { return true; }
private:
    const CompactLoop *loop_ = nullptr;
    unsigned size_ = 0, row_ = 0, offset_ = 0, part_size_ = 0;
    uint64_t previous_ = 0;
    uint8_t part_[32] = {};
    static uint64_t ticks(uint64_t time) { return time / CompactLoop::beat * 3840 + (time % CompactLoop::beat * 3840 + CompactLoop::beat - 1) / CompactLoop::beat; }
    static unsigned variable_size(uint64_t value) { unsigned size = 1; while (value >>= 7) ++size; return size; }
    static unsigned variable(uint8_t *data, uint64_t value) {
        unsigned size = variable_size(value);
        for (unsigned i = 0; i < size; ++i) data[i] = uint8_t((value >> (7 * (size - i - 1))) & 127) | uint8_t(i + 1 < size ? 128 : 0);
        return size;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(compact_loop_smf_reset)
#endif
    void reset() {
        const uint8_t header[] = {'M','T','h','d',0,0,0,6,0,0,0,1,15,0,'M','T','r','k',0,0,0,0,0,255,81,3,7,161,32};
        std::memcpy(part_,header,sizeof(header));
        unsigned track = size_ - 22;
        for (unsigned i = 0; i < 4; ++i) part_[18 + i] = uint8_t(track >> (24 - i * 8));
        row_ = offset_ = 0; previous_ = 0; part_size_ = sizeof(header);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(compact_loop_smf_next)
#endif
    bool next() {
        offset_ += part_size_; part_size_ = 0;
        if (row_ < loop_->count()) {
            MidiFilePlayer::Event event; if (!loop_->event(row_++,event)) return false;
            uint64_t now = ticks(event.time);
            part_size_ = variable(part_,now - previous_); previous_ = now;
            part_[part_size_++] = event.status; part_[part_size_++] = event.first;
            if (event.status >> 4 != 12 && event.status >> 4 != 13) part_[part_size_++] = event.second;
        } else if (row_ == loop_->count()) {
            ++row_; part_size_ = variable(part_,ticks(loop_->length) - previous_);
            part_[part_size_++] = 255; part_[part_size_++] = 47; part_[part_size_++] = 0;
        } else return false;
        return true;
    }
};

class CompactLoopPlayback {
public:
    bool bind(CompactLoop &loop, bool reverse) {
        if (reverse && !loop.prepare_reverse()) return false;
        if (!reverse) loop.release_reverse();
        loop_ = &loop; reverse_ = reverse; rewind(); return true;
    }
    void rewind() { std::fill(std::begin(positions_),std::end(positions_),0); }
    void release(Sink &sink, unsigned outputs = 0xffff) {
        for (unsigned i = 0; i < 16; ++i) if (outputs & (1u << i)) {
            if (tokens_[i]) sink.abandon_group(i,tokens_[i]);
            tokens_[i] = 0;
        }
    }
    bool done(unsigned output) const { return !loop_ || positions_[output] >= loop_->count(); }
    bool finish(unsigned output, Sink &sink) {
        if (!loop_) return true;
        for (unsigned row = positions_[output]; row < loop_->count(); ++row) {
            MidiFilePlayer::Event event; if (!at(row,event)) return false;
            unsigned kind = event.status >> 4;
            if (event.time < loop_->length && kind != 8 && !(kind == 9 && !event.second)
                && !(kind == 11 && event.first >= 64 && event.first <= 69 && event.second < 64)) return false;
        }
        poll(loop_->length,1u << output,sink); return done(output);
    }
    void poll(uint64_t time, unsigned outputs, Sink &sink) {
        if (!loop_) return;
        unsigned blocked = 0;
        for (unsigned work = 0; work < 64;) {
            bool progress = false;
            for (unsigned output = 0; output < 16 && work < 64; ++output) if ((outputs & ~blocked) & (1u << output)) {
                ++work; auto &row = positions_[output];
                if (row == loop_->count()) { blocked |= 1u << output; continue; }
                MidiFilePlayer::Event event;
                if (!at(row,event) || event.time > time) { blocked |= 1u << output; continue; }
                Packet packet{uint8_t(event.status >> 4),{event.status,event.first,event.second}};
                bool accepted = false;
                if (!event.previous) accepted = sink.submit_loop(output,packet);
                else {
                    unsigned first = event.previous - 1, end = event.next - 1;
                    MidiFilePlayer::Event a, b;
                    if (!at(first,a) || !at(first + 1,b)) return;
                    Parameter parameter; parameter.channel = a.status & 15;
                    parameter.kind = end - first == 2 ? Parameter::cc14 : a.first == 99 ? (end - first == 5 ? Parameter::nrpn7 : Parameter::nrpn14) : Parameter::rpn14;
                    parameter.address = uint16_t(end - first == 2 ? a.first : a.second * 128 + b.second);
                    auto &token = tokens_[output];
                    if (!token) token = (uint64_t(1) << 63) | ++serial_;
                    accepted = sink.submit_group(output,packet,parameter,token,row == first,row + 1 == end,0);
                    if (accepted && row + 1 == end) token = 0;
                }
                if (accepted) { ++row; progress = true; } else blocked |= 1u << output;
            }
            if (!progress) break;
        }
    }
private:
    CompactLoop *loop_ = nullptr;
    bool reverse_ = false;
    unsigned positions_[16] = {};
    uint64_t tokens_[16] = {}, serial_ = 0;
    bool at(unsigned row, MidiFilePlayer::Event &event) const { return reverse_ ? loop_->reversed(row,event) : loop_->event(row,event); }
};
}
