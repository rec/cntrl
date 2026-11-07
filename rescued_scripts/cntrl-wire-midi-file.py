from pathlib import Path
p=Path('firmware/include/cntrl/patch_executor.h');s=p.read_text().replace('#include "document.h"','#include "document.h"\n#include "midi_file.h"');s=s.replace('    virtual bool state(unsigned operation, uint32_t value) = 0;','''    virtual bool state(unsigned operation, uint32_t value) = 0;
    virtual bool play_midi_file(unsigned, const midi::PlayMidiFilePatch &) { return false; }
    virtual midi::Bridge::Delivery midi_file_delivery(unsigned) const { return midi::Bridge::Delivery::cancelled; }
    virtual void cancel_midi_file(unsigned) {}
    virtual bool record_midi_file(const midi::RecordMidiFilePatch &) { return false; }''');s=s.replace('        if (waiting_) {\n            if (!outputs_', '''        if (file_waiting_) {
            auto delivery = outputs_.midi_file_delivery(owner_);
            if (delivery == midi::Bridge::Delivery::pending) return;
            file_waiting_ = false;
            if (delivery != midi::Bridge::Delivery::complete) { fail("MIDI file playback failed"); return; }
            complete_leaf();
        }
        if (waiting_) {
            if (!outputs_''',1);i=s.index('        case 7:',s.index('    void tick('));s=s[:i]+'''        case 8: {
            midi::PlayMidiFilePatch settings;
            std::memcpy(settings.filename, p, 96); settings.outputs = u16(p + 96);
            settings.loops = u32(p + 98); settings.pre_delay = u32(p + 102); settings.post_delay = u32(p + 106);
            std::memcpy(&settings.speed, p + 110, 8); settings.reverse = p[118]; settings.transpose = int8_t(p[119]);
            if (!outputs_.play_midi_file(owner_, settings)) { fail("Cannot open MIDI file"); return; }
            file_waiting_ = started_ = true; return;
        }
        case 9:
            if (!outputs_.record_midi_file({u16(p), u16(p + 2)})) { fail("MIDI recording failed"); return; }
            started_ = true; complete_leaf(); return;
'''+s[i:];s=s.replace('    void cancel() {\n', '    void cancel() {\n        if (file_waiting_) outputs_.cancel_midi_file(owner_);\n        file_waiting_ = false;\n',1).replace('    bool waiting_ = false,','    bool file_waiting_ = false;\n    bool waiting_ = false,');s=s.replace('    bool fail(const char *message) {\n','    bool fail(const char *message) {\n        if (file_waiting_) outputs_.cancel_midi_file(owner_);\n        file_waiting_ = false;\n',1);s=s.replace('        } else if (node[0] != 7) state_complete_ = false;','''        } else if (node[0] == 8) {
            state_complete_ = false;
            unsigned mask = u16(p + 96);
            for (const auto &out : midi::output_connections) if ((mask & (1u << out.route)) && !resource(out.name, 256, 256)) return false;
            for (const auto &out : midi::usb_output_connections) if ((mask & (1u << out.route)) && !resource(out.name, 256, 256)) return false;
        } else if (node[0] != 7) state_complete_ = false;''');p.write_text(s)
p=Path('firmware/include/cntrl/document.h');s=p.read_text().replace('#include <cstdio>','#include <cstdio>\n#include <cmath>');i=s.index('            case 7:',s.index('static bool graph_valid'));s=s[:i]+'''            case 8: {
                double speed = 0;
                if (n[1] || length != 120 || (p[0] && !text_valid(p, 96)) || !u16(p + 96) || (u16(p + 96) & ~0x17f)
                    || p[118] > 1 || int8_t(p[119]) == -128) return false;
                for (unsigned j = 0; j < 96 && p[j]; ++j) if (p[j] == '/' || p[j] == '\\\\') return false;
                std::memcpy(&speed, p + 110, 8);
                if (!std::isfinite(speed) || speed <= 0) return false;
                for (const auto &out : midi::output_connections)
                    if ((u16(p + 96) & (1u << out.route)) && !output_valid(bytes + metadata + trs_modes_offset, reinterpret_cast<const uint8_t *>(out.name), hardware)) return false;
                for (const auto &out : midi::usb_output_connections)
                    if ((u16(p + 96) & (1u << out.route)) && !hardware.supports(out.name)) return false;
                break;
            }
            case 9:
                if (n[1] || length != 4 || !u16(p) || !u16(p + 2) || (u16(p + 2) & ~0x143)) return false;
                break;
'''+s[i:];p.write_text(s)
