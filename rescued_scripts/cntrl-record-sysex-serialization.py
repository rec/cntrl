from pathlib import Path
p=Path('firmware/include/cntrl/midi_file.h');s=p.read_text().replace('failed_ = sysex_ = false;', 'failed_ = sysex_ = finishing_ = false;')
s=s.replace('queue_.push_back({elapsed_ / 1000, packet});', 'queue_.push_back({elapsed_ / 1000, packet, input});')
s=s.replace('''            Pending event = queue_.front(); queue_.erase(queue_.begin());
            uint8_t data[16]; unsigned size = variable(data, event.time - last_);''','''            auto chosen = queue_.begin();
            if (sysex_) {
                chosen = std::find_if(queue_.begin(), queue_.end(), [&](const auto &v) { return v.input == sysex_input_ || v.packet.bytes[0] >= 0xf8; });
                if (chosen == queue_.end()) {
                    if (finishing_ && !close_sysex(elapsed_ / 1000)) return;
                    if (sysex_) return;
                    chosen = queue_.begin();
                }
            }
            Pending event = *chosen; queue_.erase(chosen);
            event.time = std::max(event.time, last_);
            bool sysex_packet = event.packet.cin >= 4 && event.packet.cin <= 7 && event.packet.bytes[0] != 0xf6;
            if (sysex_ && event.packet.bytes[0] >= 0x80 && event.packet.bytes[0] < 0xf8
                && (!sysex_packet || event.packet.bytes[0] == 0xf0) && !close_sysex(event.time)) return;
            uint8_t data[16]; unsigned size = variable(data, event.time - last_);''')
s=s.replace('''            if (event.packet.cin >= 4 && event.packet.cin <= 7 && event.packet.bytes[0] != 0xf6) {''','''            if (sysex_packet) {''')
s=s.replace('                if (begin) sysex_ = true;', '                if (begin) { sysex_ = true; sysex_input_ = event.input; }')
s=s.replace('''    bool finish(uint32_t now) {
        while''','''    bool finish(uint32_t now) {
        finishing_ = true;
        while''')
i=s.index('        if (sysex_) {\n            uint8_t close[7];',s.index('    bool finish('));j=s.index('        uint8_t end[7];',i);s=s[:i]+'        if (sysex_ && !close_sysex(elapsed_ / 1000)) return false;\n'+s[j:]
s=s.replace('    struct Pending { uint64_t time; Packet packet; };', '    struct Pending { uint64_t time; Packet packet; unsigned input; };')
s=s.replace('    bool active_ = false, failed_ = false, sysex_ = false;', '    bool active_ = false, failed_ = false, sysex_ = false, finishing_ = false;\n    unsigned sysex_input_ = 0;')
i=s.index('    void advance(',s.index('class MidiFileRecorder'));s=s[:i]+'''#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_record_close_sysex)
#endif
    bool close_sysex(uint64_t time) {
        uint8_t close[7]; unsigned n = variable(close, time - last_);
        if (!n) { active_ = false; failed_ = true; return false; }
        close[n++] = 0xf7; close[n++] = 1; close[n++] = 0xf7;
        if (n > UINT32_MAX - length_ || !file_.write(length_, close, n)) { active_ = false; failed_ = true; return false; }
        length_ += n; last_ = time; sysex_ = false; return true;
    }
'''+s[i:]
# Place file-only processing alongside existing file routines in flash.
for signature,name in [('    void append_event(', 'midi_file_append'), ('    bool load_event(', 'midi_file_load_event'), ('    void stream_event(', 'midi_file_stream'), ('    bool finish(uint32_t now)', 'midi_record_finish')]:
 s=s.replace(signature, '#ifdef CNTRL_FLASH_ROUTINE\n    CNTRL_FLASH_ROUTINE('+name+')\n#endif\n'+signature)
p.write_text(s)
p=Path('firmware/test/test_midi_file.cpp');s=p.read_text();i=s.index('int main(');s=s[:i]+'''void test_recording_serializes_independent_input_sysex_streams() {
    File source; cntrl::midi::MidiFileRecorder recorder(source);
    assert(recorder.start({},0));
    recorder.receive(0,{4,{0xf0,1,2}},0);
    recorder.receive(1,{9,{0x90,72,90}},1000);
    recorder.receive(1,{4,{0xf0,3,4}},2000);
    recorder.receive(0,{5,{0xf7,0,0}},3000);
    recorder.receive(1,{5,{0xf7,0,0}},4000);
    assert(recorder.finish(5000));
    File index; Sink sink; cntrl::midi::MidiFilePlayer player(source,index);
    prepare(player,sink); for (unsigned t = 0; t <= 5000; t += 1000) pump(player,sink,t);
    assert((sink.sent[0] == Bytes{0xf0,1,2,0xf7,0x90,72,90,0xf0,3,4,0xf7,0x80,72,0}));
    // Stopping during a message closes it before deferred input is drained.
    File partial; cntrl::midi::MidiFileRecorder incomplete(partial);
    assert(incomplete.start({},0));
    incomplete.receive(0,{4,{0xf0,1,2}},0); incomplete.receive(1,{12,{0xc0,12,0}},1000);
    assert(incomplete.finish(2000));
    File partial_index; Sink partial_sink; cntrl::midi::MidiFilePlayer partial_player(partial,partial_index);
    prepare(partial_player,partial_sink); pump(partial_player,partial_sink,0); pump(partial_player,partial_sink,2000);
    assert((partial_sink.sent[0] == Bytes{0xf0,1,2,0xf7,0xc0,12}));
}

'''+s[i:];s=s.replace('    test_recorded_system_messages_and_clock_are_preserved();', '    test_recording_serializes_independent_input_sysex_streams();\n    test_recorded_system_messages_and_clock_are_preserved();');p.write_text(s)
p=Path('doc/midi-files.md');s=p.read_text().replace('input-reading loop. During a playback SysEx message, ordinary bridge traffic on', 'input-reading loop. Concurrent input streams are serialized around SysEx\nmessages; deferred channel events follow the complete message. An unfinished\nSysEx is closed when recording stops. During a playback SysEx message, ordinary bridge traffic on');p.write_text(s)
