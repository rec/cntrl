from pathlib import Path
import re
for p in map(Path,['cntrl/patch_switcher/models.py','cntrl/patch_switcher/utility_pages.py','cntrl/maker/morningstar/mc/firmware/test_renderer.cpp','cntrl/maker/morningstar/mc/firmware/patch_switcher/utility_pages.h']):
 def merge(m):
  a,b=m.groups()
  if 'enum class Page' in a:return b.replace('ports, clock','ports, expression, clock')
  if 'unsigned(page_)' in a:return b
  if 'const char *heading' in a:return b.replace(': "Firmware";',': page_ == Page::expression ? "Expression" : "Firmware";')
  if 'browse(true' in a:return b+a
  return a+b
 p.write_text(re.sub(r'^<<<<<<< HEAD\n(.*?)^\|{7}[^\n]*\n.*?^=======\n(.*?)^>>>>>>>[^\n]*\n',merge,p.read_text(),flags=re.M|re.S))
p=Path('cntrl/patch_switcher/utility_pages.py');s=p.read_text()
i=s.index('    if len(set');j=s.index('    return [',i); validation=s[i:j];s=s[:i]+s[j:]
i=s.index('        sum(1 <<');j=s.index('    ]',i);args=s[i:j];s=s[:i]+s[j:]
i=s.index('def unpack(');s=s[:i]+'''def pack_clock(show: models.Setup, target: models.Target) -> bytes:
    r = show.utility_pages
    if not r.clock_outputs and r.clock_source == 'internal' and r.clock_bpms == [120]:
        return b''
'''+validation+'    return CLOCK.pack(\n'+args+'    )\n\n\n'+s[i:]
s=s.replace("'<29H'","'<23H'")
i=s.index("    result['clock_outputs']");j=s.index('    return result',i);s=s[:i]+s[j:]
i=s.index('def pack_expression');s=s[:i]+'''def unpack_clock(data: bytes, offset: int) -> dict[str, object]:
    mask, external, *slots = CLOCK.unpack_from(data, offset)
    return {
        'clock_outputs': [n for n, i in CLOCK_ROUTES.items() if mask & (1 << i)],
        'clock_source': 'external' if external else 'internal',
        'clock_bpms': [b for b in slots if b],
    }


'''+s[i:]
p.write_text(s.replace('CLOCK_ROUTES =', "CLOCK_FLAG = 1 << 14\nCLOCK = struct.Struct('<6H')\nCLOCK_ROUTES ="))
p=Path('cntrl/patch_switcher/compiler.py');s=p.read_text().replace('DOCUMENT_VERSION = 30','DOCUMENT_VERSION = 29').replace('29H4s','23H4s').replace('        expression = utility_pages.pack_expression','        clock = utility_pages.pack_clock(show, target)\n        expression = utility_pages.pack_expression').replace("        expression\n        + b'CNAM'","        expression\n        + clock\n        + b'CNAM'").replace('| (utility_pages.EXPRESSION_FLAG if expression else 0),','| (utility_pages.EXPRESSION_FLAG if expression else 0)\n            | (utility_pages.CLOCK_FLAG if clock else 0),');p.write_text(s)
p=Path('cntrl/patch_switcher/resolved.py');s=p.read_text().replace('0x1FFF | utility_pages.EXPRESSION_FLAG','0x1FFF | utility_pages.EXPRESSION_FLAG | utility_pages.CLOCK_FLAG').replace("    if data[cursor : cursor + 4] != b'CNAM':","    if flags & utility_pages.CLOCK_FLAG:\n        cursor += utility_pages.CLOCK.size\n    if data[cursor : cursor + 4] != b'CNAM':");i=s.index('    if flags & utility_pages.EXPRESSION_FLAG:',s.index('    pages ='));j=s.index("    result['utility_pages']",i);p.write_text(s[:i]+'''    cursor = meta + METADATA.size + ni * 64 + nb * 8 + np * 4
    if flags & utility_pages.EXPRESSION_FLAG:
        pages['expression'] = utility_pages.unpack_expression(data, cursor, TRS_JACKS)
        cursor += utility_pages.EXPRESSION.size * 4
    if flags & utility_pages.CLOCK_FLAG:
        pages.update(utility_pages.unpack_clock(data, cursor))
'''+s[j:])
p=Path('firmware/include/cntrl/document.h');s=p.read_text().replace('2 * UtilityPageSettings::count','2 * UtilityPageSettings::clock_outputs').replace('document_version = 30','clock_section_size = 2 * (UtilityPageSettings::count - UtilityPageSettings::clock_outputs);\nconstexpr unsigned document_version = 29').replace('~0x9f','~0xdf').replace('i < UtilityPageSettings::count','i < UtilityPageSettings::clock_outputs')
i=s.index('        if (right[R::clock_external]');j=s.index('        unsigned names',i);validation=s[i:j];s=s[:i]+s[j:]
i=s.index('        if (names + 4');s=s[:i]+'''        if (bytes[19] & 0x40) {
            if (names + clock_section_size > length) return false;
            read_clock(right, bytes + names);
            names += clock_section_size;
'''+''.join('    '+l for l in validation.splitlines(True))+'        }\n'+s[i:]
s=s.replace('        return settings;','        settings.words[UtilityPageSettings::slot1] = 120;\n        if (data_[19] & 0x40) read_clock(settings, bank(patches()) + ((data_[19] & 0x80) ? expression_section_size : 0));\n        return settings;')
s=s.replace('unsigned expression_size() const { return (data_[19] & 0x80) ? expression_section_size : 0; }','unsigned utility_section_size() const { return ((data_[19] & 0x80) ? expression_section_size : 0) + ((data_[19] & 0x40) ? clock_section_size : 0); }')
s=s.replace('private:\n','private:\n    static void read_clock(UtilityPageSettings &settings, const uint8_t *p) {\n        for (unsigned i = UtilityPageSettings::clock_outputs; i < UtilityPageSettings::count; ++i, p += 2) settings.words[i] = u16(p);\n    }\n',1);p.write_text(s)
p=Path('firmware/include/cntrl/documents.h');p.write_text(p.read_text().replace('expression_size()', 'utility_section_size()'))
