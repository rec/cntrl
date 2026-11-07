from pathlib import Path

from cntrl.patch_switcher import cli, compiler, models, resolved

backup = Path('/private/tmp/cntrl-before-right-pages-20261104')
output = Path('/private/tmp/cntrl-right-pages-ready-20261104')
output.mkdir(exist_ok=False)
target = models.Target.model_validate(compiler.read_document(Path('cntrl/maker/morningstar/mc/patch-switcher-target.json')))
for source in backup.glob('*.toml'):
    assets: dict[int, bytes] = {}
    document = compiler.read_document(source)
    document['right_pages']['background_color'] = '0xFEFCE8'
    document['right_pages']['annulus'] = 5
    data = compiler.compile_show(document, {}, [], target, assets)
    (output / f'{source.stem}.bin').write_bytes(data)
    for key, font in assets.items():
        cli.save_font(output, key, font)
    exported = output / source.name
    exported.write_text(resolved.export(data))
    assert compiler.compile_show(compiler.read_document(exported), {}, [], target) == data
    print(f'Prepared {source.stem}: {len(data)} bytes, {len(assets)} font assets')
