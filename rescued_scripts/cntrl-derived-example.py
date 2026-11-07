from pathlib import Path
import tomlkit
from cntrl.patch_switcher import compiler, models, songs, resolved
target = models.Target.model_validate(compiler.read_document(Path('firmware/test/target.json')))
defs = {'A′': {'parent': 'A', 'volume': 100, 'display_name': "A'"}, 'A': {'type':'program-change', 'instrument':'piano', 'program':7, 'volume':80}}
instruments = {'piano': {'output':'din','channel':1}}
library = songs.Library.model_validate({'instruments':instruments, 'patches':defs, 'songs':[{'id':1,'name':'Example','patches':[{'patch':k} for k in ('A′','A','A′')]}]})
data = songs.encode(library,target)
download = songs.decode(data)
print(download.patches, download.songs[0].patches)
assert songs.encode(download,target)==data
library.setlists.append(songs.Setlist(id=2,name='Set',songs=[1],entry_ids=[3]))
Path('/private/tmp/cntrl-derived-song.bin').write_bytes(songs.encode(library,target))
