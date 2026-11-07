from pathlib import Path
from cntrl.patch_switcher import compiler, models, resolved
target = models.Target.model_validate(compiler.read_document(Path('cntrl/maker/morningstar/mc/patch-switcher-target.json')))
source = compiler.read_document(Path('simple-setup.toml'))
source['trs_ports'] = {'configurable-2': 'expression'}
source['utility_pages']['expression'] = {'configurable-2': {'output': 'din', 'channel': 7, 'controller': 11, 'minimum': 10, 'maximum': 100, 'reverse': True, 'curve': 'slow'}}
data = compiler.compile_show(source, {}, [compiler.read_document(Path('instruments/tags.toml'))], target)
Path('/private/tmp/cntrl-expression-probe.bin').write_bytes(data)
print(len(data), 'bytes')
