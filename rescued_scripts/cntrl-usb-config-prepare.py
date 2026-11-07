from pathlib import Path
import shutil
import tomlkit
from cntrl.patch_switcher import compiler, models, resolved

backup = Path('/private/tmp/cntrl-usb-config-before')
output = backup / 'prepared'
output.mkdir()
shutil.copytree('/private/tmp/cntrl-rhp-y_pm9aid/fonts', output / 'fonts')
target = models.Target.model_validate(compiler.read_document(Path('cntrl/maker/morningstar/mc/patch-switcher-target.json')))
for source in backup.glob('*.toml'):
    document = tomlkit.parse(source.read_text())
    document['usb_device_midi'] = True
    document['usb_host_midi'] = True
    exported = output / source.name
    exported.write_text(tomlkit.dumps(document))
    data = compiler.compile_show(compiler.read_document(exported), {}, [], target)
    old = (backup / f'{source.stem}.bin').read_bytes()
    assert data[:4] == old[:4] and data[6:18] == old[6:18] and data[20:] == old[20:], source.name
    assert tomlkit.parse(resolved.export(data)).unwrap() == document.unwrap()
    (output / f'{source.stem}.bin').write_bytes(data)
    print(source.stem, 'only format version and USB flags changed')
