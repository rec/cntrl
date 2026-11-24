#pragma once
#include <cntrl/hardware.h>
#include <cntrl/memon_settings.h>

namespace cntrl::ui {

class Memon {
public:
    enum class Phase { ready, replay_wait, cue, gap, answer, add, next_round, error, over };
    enum class Result { none, wrong, timeout, complete };
    static constexpr unsigned double_click_ms = 250;

    void configure(MemonSettings settings) { settings_ = settings; }
    const MemonSettings &settings() const { return settings_; }
    Phase phase() const { return phase_; }
    Result result() const { return result_; }
    unsigned score() const { return score_; }
    unsigned round() const { return round_; }
    unsigned position() const {
        return answering() ? position_ + 1 : phase_ == Phase::cue || phase_ == Phase::gap ? playback_ + 1 : position_;
    }
    unsigned lit() const { return lit_; }
    uint64_t elapsed_ms() const { return elapsed_; }
    unsigned remaining_ms(uint32_t now) const {
        return answering() && timeout() && !reached(now, deadline_) ? deadline_ - now : 0;
    }
    bool countdown_visible() const { return answering() && timeout() && (settings_.flags & MemonSettings::countdown); }
    bool answering() const { return phase_ == Phase::answer || phase_ == Phase::add; }
    bool playing() const { return phase_ != Phase::ready && phase_ != Phase::over && phase_ != Phase::error; }

#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(memon_start)
#endif
    void start(uint32_t now, uint32_t seed, Hardware &hardware) {
        if (phase_ != Phase::ready && phase_ != Phase::over) return;
        score_ = length_ = position_ = 0; round_ = 1; elapsed_ = 0; updated_ = now;
        result_ = Result::none; random_ = seed ? seed : 1;
        if (settings_.flags & MemonSettings::cooperative) wait_for(Phase::add, now);
        else begin_round(now, hardware);
    }

    void leave(uint32_t now, Hardware &hardware) {
        phase_ = Phase::ready; pending_ = no_tile; lit_ = 0;
        queued_count_ = 0;
        stop_sound(now, hardware);
    }

#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(memon_press)
#endif
    void press(unsigned tile, uint32_t now, Hardware &hardware) {
        tick(now, hardware);
        if (!answering() || tile >= 4) return;
        if (!(settings_.flags & MemonSettings::repeat)) {
            accept(tile, now, hardware);
            return;
        }
        if (pending_ != no_tile) {
            const unsigned previous = pending_;
            pending_ = no_tile;
            if (previous == tile && length_) {
                queued_count_ = 0;
                stop_sound(now, hardware);
                replay_phase_ = phase_;
                phase_ = Phase::replay_wait; deadline_ = now + 1000;
                return;
            }
            accept(previous, now, hardware);
            if (!answering()) return;
        }
        if (!length_) { accept(tile, now, hardware); return; }
        pending_ = tile;
        pending_deadline_ = now + double_click_ms;
    }

#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(memon_tick)
#endif
    void tick(uint32_t now, Hardware &hardware) {
        if (phase_ == Phase::ready && !sound_count_) return;
        if (playing()) elapsed_ += uint32_t(now - updated_);
        updated_ = now;
        poll_sound(now, hardware);
        if (lit_ && reached(now, lit_deadline_)) lit_ = 0;

        if (answering()) {
            if (pending_ != no_tile) {
                if (!reached(now, pending_deadline_)) return;
                const unsigned tile = pending_; pending_ = no_tile;
                accept(tile, now, hardware);
            } else if (timeout() && reached(now, deadline_)) fail(Result::timeout, now, hardware);
            return;
        }
        if (!reached(now, deadline_)) return;
        switch (phase_) {
        case Phase::replay_wait: play_sequence(replay_phase_, now, hardware); break;
        case Phase::cue:
            if (playback_ + 1 == length_) {
                position_ = resume_ == Phase::add ? length_ : 0; wait_for(resume_, now);
            } else {
                phase_ = Phase::gap; deadline_ = now + settings_.gap_ms;
            }
            break;
        case Phase::gap: ++playback_; cue(now, hardware); break;
        case Phase::next_round: ++round_; begin_round(now, hardware); break;
        case Phase::error: phase_ = Phase::over; break;
        default: break;
        }
    }

private:
    static constexpr unsigned max_sequence = 512, no_tile = 4;
    MemonSettings settings_;
    Phase phase_ = Phase::ready, resume_ = Phase::answer, replay_phase_ = Phase::answer;
    Result result_ = Result::none;
    uint8_t sequence_[max_sequence] = {};
    unsigned length_ = 0, position_ = 0, playback_ = 0, round_ = 0, score_ = 0, lit_ = 0;
    unsigned pending_ = no_tile;
    uint32_t deadline_ = 0, pending_deadline_ = 0, sound_deadline_ = 0, lit_deadline_ = 0;
    uint32_t random_ = 1, updated_ = 0;
    uint64_t elapsed_ = 0;
    const char *sound_output_ = nullptr;
    const char *queued_output_ = nullptr;
    uint32_t queued_start_ = 0, queued_deadline_ = 0;
    uint8_t sound_notes_[4] = {}, queued_notes_[4] = {};
    uint8_t sound_count_ = 0, released_ = 0, sound_channel_ = 0;
    uint8_t queued_count_ = 0, next_note_ = 0;
    bool sound_legato_ = false, queued_legato_ = false;

    static bool reached(uint32_t now, uint32_t deadline) { return int32_t(now - deadline) >= 0; }
    unsigned timeout() const { return settings_.flags & MemonSettings::timed ? settings_.timeout_ms : 0; }
    void wait_for(Phase phase, uint32_t now) { phase_ = phase; deadline_ = now + timeout(); }

#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(memon_send_note)
#endif
    static bool send_note(Hardware &hardware, const char *output, unsigned channel,
                          uint8_t note, uint8_t velocity, bool breath, [[maybe_unused]] bool legato) {
        const uint8_t message[] = {uint8_t((velocity ? 0x90 : 0x80) | channel), note, velocity,
                                   uint8_t(0xb0 | channel), 2, uint8_t(velocity ? 100 : 0)};
#ifdef CNTRL_EMULATOR
        return hardware.send_memon(output, message, breath ? 6 : 3, legato);
#else
        return hardware.send(output, message, breath ? 6 : 3);
#endif
    }

#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(memon_stop_sound)
#endif
    void stop_sound(uint32_t now, Hardware &hardware) {
        sound_deadline_ = now;
        while (released_ < sound_count_) {
            const bool breath = !sound_legato_ || released_ + 1 == sound_count_;
            if (!send_note(hardware, sound_output_, sound_channel_, sound_notes_[released_], 0, breath, sound_legato_)) return;
            ++released_;
        }
        sound_count_ = released_ = 0;
    }

#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(memon_sound)
#endif
    void sound(const uint8_t *notes, unsigned count, unsigned duration, uint32_t now, Hardware &hardware,
               bool legato = false) {
        sound_deadline_ = now;
        const auto *output = hardware.midi_outputs().route(settings_.route);
        queued_count_ = 0;
        if (!output) return;
        std::memcpy(queued_notes_, notes, count);
        queued_output_ = output->name; queued_count_ = uint8_t(count); next_note_ = 0;
        queued_legato_ = legato; queued_start_ = now; queued_deadline_ = now + duration;
        poll_sound(now, hardware);
    }

#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(memon_poll_sound)
#endif
    void poll_sound(uint32_t now, Hardware &hardware) {
        if (sound_count_ && reached(now, sound_deadline_)) stop_sound(now, hardware);
        if (queued_count_ && reached(now, queued_deadline_)) queued_count_ = 0;
        if (!queued_count_ || (sound_count_ && !next_note_)) return;
        const unsigned channel = settings_.channel - 1;
        while (next_note_ < queued_count_) {
            if (queued_legato_ && !reached(now, queued_start_ +
                (queued_deadline_ - queued_start_) * next_note_ / queued_count_)) return;
            const uint8_t note = queued_notes_[next_note_];
            if (!send_note(hardware, queued_output_, channel, note, settings_.velocity,
                           !queued_legato_ || !next_note_, queued_legato_)) return;
            sound_notes_[sound_count_++] = note; ++next_note_;
            sound_output_ = queued_output_; sound_channel_ = uint8_t(channel);
            sound_legato_ = queued_legato_; sound_deadline_ = queued_deadline_;
        }
        queued_count_ = 0;
    }

    void flash(unsigned tile, uint32_t now, Hardware &hardware) {
        lit_ = 1u << tile; lit_deadline_ = now + settings_.note_ms;
        sound(&settings_.notes[tile], 1, settings_.note_ms, now, hardware);
    }

#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(memon_cue)
#endif
    void cue(uint32_t now, Hardware &hardware) {
        flash(sequence_[playback_], now, hardware);
        phase_ = Phase::cue; deadline_ = now + settings_.note_ms;
    }

    void play_sequence(Phase resume, uint32_t now, Hardware &hardware) {
        resume_ = resume; playback_ = 0;
        cue(now, hardware);
    }

#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(memon_begin_round)
#endif
    void begin_round(uint32_t now, Hardware &hardware) {
        random_ ^= random_ << 13; random_ ^= random_ >> 17; random_ ^= random_ << 5;
        sequence_[length_++] = uint8_t(random_ & 3);
        play_sequence(Phase::answer, now, hardware);
    }

    void fail(Result reason, uint32_t now, Hardware &hardware) {
        phase_ = Phase::error; result_ = reason; deadline_ = now + settings_.error_ms;
        lit_ = 15; lit_deadline_ = deadline_;
        sound(settings_.chord, settings_.chord_size, settings_.error_ms, now, hardware,
              !(settings_.flags & MemonSettings::polyphonic));
    }

#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(memon_accept)
#endif
    void accept(unsigned tile, uint32_t now, Hardware &hardware) {
        if (phase_ == Phase::answer && sequence_[position_] != tile) {
            fail(Result::wrong, now, hardware); return;
        }
        flash(tile, now, hardware);
        if (phase_ == Phase::add) {
            sequence_[length_++] = uint8_t(tile);
        } else {
            ++score_;
            if (++position_ < length_) { wait_for(Phase::answer, now); return; }
            if (length_ == max_sequence) { phase_ = Phase::over; result_ = Result::complete; return; }
            if ((settings_.flags & MemonSettings::cooperative) && length_ + 1 < max_sequence) {
                wait_for(Phase::add, now); return;
            }
        }
        phase_ = Phase::next_round; deadline_ = now + settings_.note_ms + settings_.gap_ms;
    }
};

} // namespace cntrl::ui
