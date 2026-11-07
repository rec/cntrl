import struct
from pathlib import Path
from zlib import crc32
from cntrl.patch_switcher import cli, compiler, models, resolved
backup = Path('/private/tmp/cntrl-rhp-y_pm9aid')
output = backup / 'prepared'
output.mkdir(exist_ok=True)
target = models.Target.model_validate(compiler.read_document(Path('cntrl/maker/morningstar/mc/patch-switcher-target.json')))
for source in backup.glob('*.toml'):
    assets: dict[int, bytes] = {}
    data = compiler.compile_show(compiler.read_document(source), {}, [], target, assets)
    old = bytearray((backup / f'{source.stem}.bin').read_bytes())
    ni, nc, np = struct.unpack_from('<HHH', old, 12)
    offset = 76 + ni * 68 + nc * 68 + np * 82 + 60
    old[offset:offset] = bytes(2)
    struct.pack_into('<H', old, 4, 27)
    struct.pack_into('<I', old, 8, len(old))
    struct.pack_into('<I', old, 20, crc32(old[28:]))
    assert data == old, source.name
    (output / f'{source.stem}.bin').write_bytes(data)
    for key, font in assets.items():
        cli.save_font(output, key, font)
    exported = output / source.name
    exported.write_text(resolved.export(data))
    assert compiler.compile_show(compiler.read_document(exported), {}, [], target) == data
    print('Prepared', source.stem, len(data), 'bytes; only format header and default title font changed')
