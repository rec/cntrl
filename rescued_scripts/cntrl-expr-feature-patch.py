from pathlib import Path
from difflib import unified_diff
import subprocess
snapshot = Path('/private/tmp/cntrl-expr-trs-base')
files = '''
cntrl/maker/morningstar/mc/firmware/expression-pedal.md
cntrl/maker/morningstar/mc/firmware/patch_switcher/expression_pedals.h
cntrl/maker/morningstar/mc/firmware/patch_switcher/target.cpp
cntrl/maker/morningstar/mc/firmware/patch_switcher/trs_ports.h
cntrl/maker/morningstar/mc/firmware/patch_switcher/page_settings.h
cntrl/maker/morningstar/mc/firmware/test_renderer.cpp
cntrl/patch_switcher/compiler.py
cntrl/patch_switcher/models.py
cntrl/patch_switcher/resolved.py
cntrl/patch_switcher/page_settings.py
firmware/Makefile
firmware/README.md
firmware/include/cntrl/document.h
firmware/include/cntrl/documents.h
firmware/include/cntrl/expression.h
firmware/include/cntrl/page_settings.h
firmware/test/test_expression.cpp
plan/feature-set.md
test/test_expression.py
'''.split()
with open('/private/tmp/cntrl-expr-feature.patch','w') as out:
    for name in files:
        original=snapshot/name
        if original.is_file():
            old=original.read_text()
        else:
            proc=subprocess.run(['git','show',f'92a6218:{name}'],capture_output=True)
            old=proc.stdout.decode() if proc.returncode==0 else ''
        diff=unified_diff(old.splitlines(True),Path(name).read_text().splitlines(True),'a/'+name if old else '/dev/null','b/'+name)
        out.writelines(diff)
