from pathlib import Path
p=Path('cntrl/maker/morningstar/mc/firmware/patch_switcher/target.cpp');s=p.read_text().replace('#include "expression_pedals.h"','#include "expression_pedals.h"\n#include "midi_files.h"');s=s.replace('StartupCounter startup_counter;', 'StartupCounter startup_counter;\nMidiFiles midi_files;');i=s.index('    bool state(unsigned operation, uint32_t value) override');s=s[:i]+'''    bool play_midi_file(unsigned owner, const cntrl::midi::PlayMidiFilePatch &settings) override {
        return midi_files.play(owner, settings);
    }
    cntrl::midi::Bridge::Delivery midi_file_delivery(unsigned owner) const override {
        if (owner != midi_files.owner) return cntrl::midi::Bridge::Delivery::cancelled;
        if (midi_files.player.active() || midi_files.player.cleaning()) return cntrl::midi::Bridge::Delivery::pending;
        return midi_files.player.status() == cntrl::midi::MidiFilePlayer::Status::complete ? cntrl::midi::Bridge::Delivery::complete : cntrl::midi::Bridge::Delivery::cancelled;
    }
    void cancel_midi_file(unsigned owner) override { if (owner == midi_files.owner) midi_files.player.cancel(); }
    bool record_midi_file(const cntrl::midi::RecordMidiFilePatch &settings) override {
        return midi_files.record(settings, startup_counter, micros());
    }
'''+s[i:];s=s.replace('                    if (!bridge_.usb_enabled(6)) continue;', '''                    if (!bridge_.usb_enabled(6)) continue;
                    midi_files.receive(6, cntrl::midi::Packet{uint8_t(packet & 15), {uint8_t(packet >> 8), uint8_t(packet >> 16), uint8_t(packet >> 24)}}, micros());''');s=s.replace('                    if (byte == 0xf8) pages_', '                    midi_files.receive(source - 1, uint8_t(byte), micros());\n                    if (byte == 0xf8) pages_');s=s.replace('''        if (host_midi.read() && bridge_.usb_enabled(8) && host_midi.getType() == 0xf8)
            pages_.clock(18 + host_midi.getCable(), micros());''','''        if (host_midi.read() && bridge_.usb_enabled(8)) {
            unsigned type = host_midi.getType();
            if (type == 0xf8) pages_.clock(18 + host_midi.getCable(), micros());
            if (type == 0xf0) {
                for (unsigned i = 0; i < host_midi.getSysExArrayLength(); ++i) midi_files.receive(8, host_midi.getSysExArray()[i], micros());
            } else {
                cntrl::midi::Packet packet{uint8_t(type < 0xf0 ? type >> 4 : 15), {uint8_t(type < 0xf0 ? type | (host_midi.getChannel() - 1) : type), host_midi.getData1(), host_midi.getData2()}};
                midi_files.receive(8, packet, micros());
            }
        }
        midi_files.recorder.poll(micros());
        midi_files.player.poll(micros(), clock_available(), *this);''');s=s.replace('    if (card.ready) board.load_expression_calibrations();','    if (card.ready) { board.load_expression_calibrations(); midi_files.boot(); }');p.write_text(s)
