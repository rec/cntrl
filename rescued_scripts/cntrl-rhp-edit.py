from pathlib import Path
p=Path('cntrl/patch_switcher/models.py');s=p.read_text().replace("    status_font: Literal['classic', 'Arial', 'ComicSansMS'] = 'classic'", "    status_font: Literal['classic', 'Arial', 'ComicSansMS'] = 'classic'\n    title_font: Literal['classic', 'Arial', 'ComicSansMS'] = 'classic'");p.write_text(s)
p=Path('cntrl/patch_switcher/right_pages.py');s=p.read_text().replace('r.title_height * 3 // 2\n','r.title_height * 3 // 2\n        + 4\n').replace('graphics.rgb565(graphics.color(r.background_color)),','graphics.rgb565(graphics.color(r.background_color)),\n        [\'classic\', \'Arial\', \'ComicSansMS\'].index(r.title_font),').replace("'<22H'", "'<23H'").replace("    result['status_font'] = ['classic', 'Arial', 'ComicSansMS'][w[15]]", "    result['status_font'] = ['classic', 'Arial', 'ComicSansMS'][w[15]]\n    result['title_font'] = ['classic', 'Arial', 'ComicSansMS'][w[22]]");p.write_text(s)
p=Path('cntrl/patch_switcher/compiler.py');s=p.read_text().replace("'<4sHHHBBHH22H'", "'<4sHHHBBHH23H'").replace('DOCUMENT_VERSION = 26','DOCUMENT_VERSION = 27');p.write_text(s)
p=Path('firmware/include/cntrl/right_page_settings.h');s=p.read_text().replace('decimals, background, count','decimals, background, title_font, count');p.write_text(s)
p=Path('firmware/include/cntrl/document.h');s=p.read_text().replace('document_version = 26','document_version = 27').replace('unsigned(right[R::title_height]) * 3 / 2 + right', 'unsigned(right[R::title_height]) * 3 / 2 + 4 + right').replace('right[R::status_font] > 2','right[R::status_font] > 2 || right[R::title_font] > 2');p.write_text(s)
p=Path('firmware/include/cntrl/panic.h');s=p.read_text().replace('    Panic(Hardware', '    enum class Status { ready, sending, incomplete };\n    Status status() const { return active() ? Status::sending : incomplete_ ? Status::incomplete : Status::ready; }\n    Panic(Hardware').replace('open_ = true; held_ = false; phase_', 'open_ = true; held_ = incomplete_ = false; phase_').replace('button(false); phase_', 'button(false, 0); phase_').replace('void button(bool down)', 'void button(bool down, uint32_t now)').replace('held_ = down; dirty_ = true;', 'held_ = down; dirty_ = true;\n        if (!down) released_ = now;').replace('first_ = true; phase_', 'incomplete_ = false; first_ = true; phase_').replace('void tick() {\n        if (!active()) return;', '''void tick(uint32_t now) {
        if (!active()) return;
        if (!held_ && uint32_t(now - released_) >= 1000) {
            phase_ = Phase::idle; incomplete_ = dirty_ = true; return;
        }''').replace('bool dirty_ = true;', 'bool dirty_ = true, incomplete_ = false;\n    uint32_t released_ = 0;');p.write_text(s)
p=Path('cntrl/maker/morningstar/mc/firmware/patch_switcher/target.cpp');s=p.read_text().replace('panic.button(true);','panic.button(true, now);').replace('panic.tick();','panic.tick(now);').replace('board.panic.button(false);','board.panic.button(false, now);').replace(', panic.active()', ', panic.status()').replace(', false\n#endif\n#if CNTRL_MIDI_INTERFACE', ', cntrl::Panic::Status::ready\n#endif\n#if CNTRL_MIDI_INTERFACE');p.write_text(s)
p=Path('cntrl/maker/morningstar/mc/firmware/patch_switcher/performance_pages.h');s=p.read_text().replace('#include <cntrl/midi_interface.h>','#include <cntrl/midi_interface.h>\n#include <cntrl/panic.h>').replace('bool panicking = false', 'cntrl::Panic::Status panic = cntrl::Panic::Status::ready').replace('height, 0, true);','height, settings_[R::title_font], true);',1).replace('height, 0, true, nullptr','height, settings_[R::title_font], true, nullptr',1)
s=s.replace('const unsigned heading_height = std::min(title * 3 / 2, (128 - 2 * pad) / 2);','const unsigned heading_text_height = std::min(title * 3 / 2, (128 - 2 * pad) / 2);\n        const unsigned heading_height = heading_text_height + 4;').replace('heading, pad, heading_height, 0, true,','heading, pad + 2, heading_text_height, settings_[R::title_font], true,').replace('auto &header = pane.lines[0];','auto &header = pane.lines[0];\n        header.graphics.line_height += 4;').replace('panicking ? "Sending" : "Ready"','panic == cntrl::Panic::Status::sending ? "Sending" : panic == cntrl::Panic::Status::incomplete ? "Incomplete" : "Ready"')
s=s.replace('char rows[6][64]', 'char rows[5][64]').replace('"Disk: %lu.%lu/%luG"', '"Disk:%lu.%lu/%luG"').replace('"Disk: --/--G"','"Disk:--/--G"')
s=s.replace('std::snprintf(rows[5], sizeof(rows[5]), "Uptime: %lu:%02u:%02u",', 'unsigned disk_length = std::strlen(rows[4]);\n            std::snprintf(rows[4] + disk_length, sizeof(rows[4]) - disk_length, " Up:%lu:%02u:%02u",')
s=s.replace('i < 6; ++i','i < 5; ++i').replace('body_height * i / 6','body_height * i / 5').replace('body_height * (i + 1) / 6', 'body_height * (i + 1) / 5').replace('i <= 7; ++i','i <= 6; ++i').replace("if (i == 2 && l.text[j] == ' ' && l.text[j + 1] == ' ') value = false;", "if ((i == 2 && l.text[j] == ' ' && l.text[j + 1] == ' ') || (i == 6 && l.text[j] == ' ')) value = false;")
p.write_text(s)
# Update C++ callers to supply the clock explicitly.
p=Path('firmware/test/test_panic.cpp');s=p.read_text().replace('panic.tick();','panic.tick(0);').replace('panic.button(true)', 'panic.button(true, 0)').replace('panic.button(false)', 'panic.button(false, 0)');p.write_text(s)
# Upgrade shared golden binaries without changing their authoring.
import struct
from zlib import crc32
for p in Path('firmware/test').glob('*.golden'):
 data=bytearray.fromhex(p.read_text()); ni,nc,np=struct.unpack_from('<HHH',data,12)
 offset=76+ni*68+nc*68+np*82+60
 data[offset:offset]=bytes(2)
 struct.pack_into('<H',data,4,27);struct.pack_into('<I',data,8,len(data));struct.pack_into('<I',data,20,crc32(data[28:]))
 p.write_text(data.hex()+'\n')
