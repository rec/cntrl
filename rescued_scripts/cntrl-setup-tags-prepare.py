from pathlib import Path
import shutil
import struct
import tomlkit
from cntrl.patch_switcher import compiler, display, models, resolved

backup = Path('/private/tmp/cntrl-setup-tags-before')
output = backup / 'prepared'
output.mkdir()
shutil.copytree('/private/tmp/cntrl-rhp-y_pm9aid/fonts', output / 'fonts')
target = models.Target.model_validate(compiler.read_document(Path('cntrl/maker/morningstar/mc/patch-switcher-target.json')))
for source in backup.glob('*.toml'):
    document = tomlkit.parse(source.read_text())
    document['tag_styles'] = document.pop('tags', {})
    document['tags'] = []
    exported = output / source.name
    exported.write_text(tomlkit.dumps(document))
    data = compiler.compile_show(compiler.read_document(exported), {}, [], target)
    old = (backup / f'{source.stem}.bin').read_bytes()
    normalized = []
    for value in [old, data]:
        graph = resolved.graph_offset(value)
        cdsp = graph + struct.unpack_from('<I', value, graph + 4)[0]
        source_size = display.HEADER.unpack_from(value, cdsp)[-1]
        body = bytearray(value[:-source_size])
        body[8:12] = bytes(4)
        body[20:24] = bytes(4)
        body[cdsp+12:cdsp+16] = bytes(4)
        normalized.append(body)
    assert normalized[0] == normalized[1], source.name
    assert tomlkit.parse(resolved.export(data)).unwrap() == document.unwrap()
    (output / f'{source.stem}.bin').write_bytes(data)
    print(source.stem, 'only authoring metadata and length/checksum changed')
