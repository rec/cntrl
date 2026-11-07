from pathlib import Path
import shutil
import struct
from zlib import crc32
import tomlkit
from cntrl.patch_switcher import compiler, models, resolved

backup = Path('/private/tmp/cntrl-tap-held-before-flash')
output = backup / 'prepared'
output.mkdir()
shutil.copytree('/private/tmp/cntrl-rhp-y_pm9aid/fonts', output / 'fonts')
target = models.Target.model_validate(compiler.read_document(Path('cntrl/maker/morningstar/mc/patch-switcher-target.json')))
for source in backup.glob('*.bin'):
    old = source.read_bytes()
    document = tomlkit.parse(resolved.export(old))
    document['right_pages']['off_color'] = '0x600000'
    exported = output / f'{source.stem}.toml'
    exported.write_text(tomlkit.dumps(document))
    data = compiler.compile_show(compiler.read_document(exported), {}, [], target)
    expected = bytearray(old)
    ni, nc, np = struct.unpack_from('<HHH', expected, 12)
    offset = 76 + ni * 68 + nc * 68 + np * 82 + 48
    assert struct.unpack_from('<H', expected, offset)[0] == 0x4000
    struct.pack_into('<H', expected, offset, 0x6000)
    struct.pack_into('<I', expected, 20, crc32(expected[28:]))
    assert data == expected
    (output / source.name).write_bytes(data)
    print(source.stem, 'only off color and checksum changed')
