from pathlib import Path
import sys

from cntrl.patch_switcher import compiler, models

root = Path('firmware/test')
target = models.Target.model_validate(compiler.read_document(root / 'target.json'))
definitions = compiler.read_document(root / 'definitions.json')
for name, suffix in [('show', 'json'), ('display', 'toml')]:
    data = compiler.compile_show(compiler.read_document(root / f'{name}.{suffix}'), definitions, [], target)
    (root / f'{name}.golden').write_text(data.hex() + '\n')

sys.path.insert(0, str(Path('test').resolve()))
from test_actions import compile_switches, fixture_assignments

(root / 'actions.golden').write_text(compile_switches(fixture_assignments()).hex() + '\n')
