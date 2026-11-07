from pathlib import Path

from cntrl.patch_switcher import compiler, models
from test.test_actions import compile_switches, fixture_assignments

root = Path('firmware/test')
target = models.Target.model_validate(compiler.read_document(root / 'target.json'))
for name, extension in (('show', 'json'), ('display', 'toml')):
    data = compiler.compile_show(compiler.read_document(root / f'{name}.{extension}'), compiler.read_document(root / 'definitions.json'), [], target)
    (root / f'{name}.golden').write_text(data.hex() + '\n')
(root / 'actions.golden').write_text(compile_switches(fixture_assignments()).hex() + '\n')
