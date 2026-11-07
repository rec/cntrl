from pathlib import Path

from cntrl.patch_switcher import cli, compiler, models, resolved

backup = Path(Path('/private/tmp/cntrl-latest-backup-path.txt').read_text())
output = backup / 'format-30'
output.mkdir(exist_ok=True)
target = models.Target.model_validate(compiler.read_document(Path('cntrl/maker/morningstar/mc/patch-switcher-target.json')))
for source in backup.glob('*.toml'):
    document = compiler.read_document(source)
    assert isinstance(document, dict)
    if (duration := document.pop('transition', None)) is not None:
        document['scrolling'] = {'horizontal_duration': duration, 'vertical_duration': duration}
    assets: dict[int, bytes] = {}
    data = compiler.compile_show(document, {}, [], target, assets)
    (output / f'{source.stem}.bin').write_bytes(data)
    for key, font in assets.items():
        cli.save_font(output, key, font)
    exported = output / source.name
    exported.write_text(resolved.export(data))
    assert compiler.compile_show(compiler.read_document(exported), {}, [], target) == data
    print(f'Prepared {source.stem}: {len(data)} bytes; export round trip verified')
