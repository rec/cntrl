from pathlib import Path
p=Path('firmware/include/cntrl/midi_file.h');s=p.read_text()
s=s.replace('    unsigned sysex_outputs() const {','    unsigned clock_outputs() const { return active() && has_clock_ ? settings_.outputs : 0; }\n    unsigned sysex_outputs() const {')
s=s.replace('        cancel(); settings_ = settings;', '        cancel(); settings_ = settings; has_clock_ = false;')
s=s.replace('        bool complete_sysex = false;', '        bool complete_sysex = false, meta = false;')
s=s.replace('    bool seeking_group_ = false;', '    bool seeking_group_ = false, has_clock_ = false;')
s=s.replace('''            track.running = 0;
            if (status == 0xff''','''            track.running = 0; event.meta = status == 0xff;
            if (status == 0xff''')
s=s.replace('''                event.complete_sysex = last == 0xf7;
            }
        }
        track.head''','''                event.complete_sysex = last == 0xf7;
            }
            if (status == 0xf7 && event.length && event.length <= 3) {
                uint8_t raw[3] = {};
                if (!source_.read(event.offset, raw, event.length)) return false;
                if (raw[0] != 0xf0 && raw[0] != 0xf7 && message_valid(raw, event.length)) {
                    event.status = raw[0]; event.first = raw[1]; event.second = raw[2];
                }
            }
        }
        track.head''')
s=s.replace('        if (event.status == 0xff) {', '        if (event.meta) {')
s=s.replace('        event.time = time_;\n        unsigned kind', '        event.time = time_;\n        if (event.status == 0xf8) has_clock_ = true;\n        unsigned kind')
s=s.replace('''            packet_ = {uint8_t(event.status >> 4), {event.status, event.first, event.second}};''','''            uint8_t cin = event.status < 0xf0 ? event.status >> 4
                : event.status == 0xf1 || event.status == 0xf3 ? 2 : event.status == 0xf2 ? 3 : event.status == 0xf6 ? 5 : 15;
            packet_ = {cin, {event.status, event.first, event.second}};''')
s=s.replace('''        // SMF stores system-exclusive chunks with explicit lengths. Real-time
        // timing is represented by the event clock, not embedded clock bytes.
        if (packet.cin == 15 || packet.bytes[0] == 0xf1 || packet.bytes[0] == 0xf2 || packet.bytes[0] == 0xf3 || packet.bytes[0] == 0xf6) return;
''','')
s=s.replace('''            if (event.packet.cin >= 4 && event.packet.cin <= 7) {''','''            if (event.packet.cin >= 4 && event.packet.cin <= 7 && event.packet.bytes[0] != 0xf6) {''')
s=s.replace('''            } else {
                for (unsigned i = 0; i < event.packet.size(); ++i) data[size++] = event.packet.bytes[i];
            }
            if (size''','''            } else {
                if (event.packet.bytes[0] >= 0xf0) { data[size++] = 0xf7; data[size++] = uint8_t(event.packet.size()); }
                for (unsigned i = 0; i < event.packet.size(); ++i) data[size++] = event.packet.bytes[i];
            }
            if (size''')
p.write_text(s)
p=Path('cntrl/maker/morningstar/mc/firmware/patch_switcher/target.cpp');s=p.read_text().replace('bridge_.clock_owner(pages_.clock_owner());', 'bridge_.clock_owner(pages_.clock_owner() | midi_files.player.clock_outputs());').replace('pages_.generator.poll(micros(), clock_routes, *this);', 'pages_.generator.poll(micros(), clock_routes & ~midi_files.player.clock_outputs(), *this);');p.write_text(s)
p=Path('doc/midi-files.md');s=p.read_text().replace('filter; SysEx has no channel and obeys the input filter. SMF event timing\nrepresents elapsed time; incoming MIDI Clock and system-common messages are\nexcluded from the recorded channel/SysEx sequence.', 'filter; SysEx and system messages have no channel and obey the input filter.\nSystem-common and real-time messages, including Clock, Start, Continue, Stop\nand time code, are retained as SMF escape events. A file containing MIDI Clock\nowns clock delivery on its selected outputs while active. The internal generator\nand forwarded clock are suppressed there. Start the internal generator again\nafter playback if it should resume on those outputs.');p.write_text(s)
p=Path('firmware/test/test_midi_file.cpp');s=p.read_text();i=s.index('int main(');s=s[:i]+'''void test_recorded_system_messages_and_clock_are_preserved() {
    File source; cntrl::midi::MidiFileRecorder recorder(source);
    assert(recorder.start({},0));
    recorder.receive(0,{15,{0xfa,0,0}},0);
    recorder.receive(0,{15,{0xf8,0,0}},20000);
    recorder.receive(0,{2,{0xf1,0x12,0}},30000);
    recorder.receive(0,{3,{0xf2,1,2}},40000);
    recorder.receive(0,{5,{0xf6,0,0}},50000);
    recorder.receive(0,{15,{0xff,0,0}},60000);
    recorder.receive(0,{15,{0xfc,0,0}},70000);
    assert(recorder.finish(70000));
    File index; Sink sink; cntrl::midi::MidiFilePlayer player(source,index);
    prepare(player,sink); assert(player.clock_outputs() == 1);
    pump(player,sink,0); for (unsigned t = 10000; t <= 70000; t += 10000) pump(player,sink,t);
    assert((sink.sent[0] == Bytes{0xfa,0xf8,0xf1,0x12,0xf2,1,2,0xf6,0xff,0xfc}));
}

'''+s[i:];s=s.replace('    test_playback_releases_held_sustain();', '    test_recorded_system_messages_and_clock_are_preserved();\n    test_playback_releases_held_sustain();');p.write_text(s)
