from pathlib import Path
p=Path('firmware/include/cntrl/midi_file.h');s=p.read_text().replace('(!sounding_.empty() || terminate_outputs_)','(!sounding_.empty() || !pedals_.empty() || terminate_outputs_)')
s=s.replace('''            if (!sounding_.empty()) {
                auto n = sounding_.back();
                if (!(available & (1u << n.output)) || sink.submit(n.output, {8, {uint8_t(0x80 | n.channel), n.note, 0}})) sounding_.pop_back();
            }
            return;''','''            release_one(available, sink);
            return;''',1)
i=s.index('                    unsigned kind = packet_.bytes[0] >> 4;');j=s.index('\n                }\n            if (pending_outputs_)',i);s=s[:i]+'                    remember_packet(route);'+s[j:]
s=s.replace('''        if (!sounding_.empty()) {
            auto n = sounding_.back();
            if (sink.submit(n.output, {8, {uint8_t(0x80 | n.channel), n.note, 0}})) sounding_.pop_back();
            return;
        }''','''        if (!release_one(available, sink)) return;''')
s=s.replace('    std::vector<Sounding> sounding_;','''    std::vector<Sounding> sounding_;
    struct Pedal { unsigned output; uint8_t channel, controller; };
    std::vector<Pedal> pedals_;''')
i=s.index('    static uint32_t big(');s=s[:i]+'''#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_remember)
#endif
    void remember_packet(unsigned route) {
        unsigned kind = packet_.bytes[0] >> 4;
        uint8_t channel = packet_.bytes[0] & 15;
        if (kind == 9 && packet_.bytes[2]) sounding_.push_back({route, channel, packet_.bytes[1]});
        if (kind == 8 || (kind == 9 && !packet_.bytes[2])) {
            auto n = std::find_if(sounding_.begin(), sounding_.end(), [&](const auto &v) { return v.output == route && v.channel == channel && v.note == packet_.bytes[1]; });
            if (n != sounding_.end()) sounding_.erase(n);
        }
        if (kind == 11 && (packet_.bytes[1] == 64 || packet_.bytes[1] == 66 || packet_.bytes[1] == 69)) {
            auto p = std::find_if(pedals_.begin(), pedals_.end(), [&](const auto &v) { return v.output == route && v.channel == channel && v.controller == packet_.bytes[1]; });
            if (packet_.bytes[2] >= 64) {
                if (p == pedals_.end()) pedals_.push_back({route, channel, packet_.bytes[1]});
            } else if (p != pedals_.end()) pedals_.erase(p);
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_release)
#endif
    bool release_one(unsigned available, Sink &sink) {
        // Ownership for cleanup is separate from the sink's complete MIDI state.
        if (!pedals_.empty()) {
            auto p = pedals_.back();
            if (!(available & (1u << p.output)) || sink.submit(p.output, {11, {uint8_t(0xb0 | p.channel), p.controller, 0}})) pedals_.pop_back();
            return false;
        }
        if (!sounding_.empty()) {
            auto n = sounding_.back();
            if (!(available & (1u << n.output)) || sink.submit(n.output, {8, {uint8_t(0x80 | n.channel), n.note, 0}})) sounding_.pop_back();
            return false;
        }
        return true;
    }
'''+s[i:];p.write_text(s)
p=Path('firmware/test/test_midi_file.cpp');s=p.read_text();i=s.index('int main(');s=s[:i]+'''void test_playback_releases_held_sustain() {
    auto source = smf({{0,0xb0,64,127, 0,0x90,60,100, 0x83,0x60,0x80,60,0, 0,0xff,0x2f,0}});
    File index; Sink sink; cntrl::midi::MidiFilePlayer player(source,index);
    prepare(player,sink); pump(player,sink,0); pump(player,sink,500000);
    assert((sink.sent[0] == Bytes{0xb0,64,127,0x90,60,100,0x80,60,0,0xb0,64,0}));
}

'''+s[i:];s=s.replace('    test_reverse_releases_a_note_held_at_recording_stop();','    test_playback_releases_held_sustain();\n    test_reverse_releases_a_note_held_at_recording_stop();');p.write_text(s)
p=Path('doc/midi-files.md');s=p.read_text().replace('patch cancellation releases its outstanding notes and terminates any open SysEx.', 'patch cancellation releases its outstanding notes, turns off sustain/sostenuto/\nhold-2 pedals held by the file, and terminates any open SysEx. Held notes and\npedals are also released at each pass end.');p.write_text(s)
