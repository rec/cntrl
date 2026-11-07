from pathlib import Path
import shutil
import tomlkit
from cntrl.patch_switcher import compiler,models,resolved,songs,transport
backup=Path('/private/tmp/cntrl-trs-ports-device-backup')
out=backup/'prepared'
out.mkdir(exist_ok=True)
shutil.copytree('/private/tmp/cntrl-rhp-y_pm9aid/fonts',out/'fonts',dirs_exist_ok=True)
target=models.Target.model_validate(compiler.read_document(Path('cntrl/maker/morningstar/mc/patch-switcher-target.json')))
with transport.SerialConnection(Path('/dev/cu.usbmodem144682801')) as connection:
 client=transport.SetupClient(connection)
 assert client.current()==(backup/'selected.txt').read_text()
 assert '\n'.join(f'{e.name} {e.valid}' for e in client.catalogue())==(backup/'catalogue.txt').read_text()
 lib=songs.download(client)
 (backup/'songs.bin').write_bytes(lib)
 assert len(lib)==16 and not songs.decode(lib).songs
 (backup/'songs-status.txt').write_text(client.request('CNTRL SONGS SIZE'))
 for p in backup.glob('*.toml'):
  assert client.download(p.stem)==p.with_suffix('.bin').read_bytes()
  dest=out/p.name
  shutil.copy(p,dest)
  data=compiler.compile_show(compiler.read_document(dest),{},[],target)
  assert tomlkit.parse(resolved.export(data)).unwrap()==tomlkit.parse(dest.read_text()).unwrap()
  dest.with_suffix('.bin').write_bytes(data)
  print(p.stem,len(data),flush=True)
