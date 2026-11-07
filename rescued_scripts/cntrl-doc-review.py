from pathlib import Path
import re
import subprocess
changed=subprocess.check_output(['git','diff','HEAD','--name-only'],text=True).splitlines()
changed += ['doc/formatting.md','doc/midi-interface.md']
broken=[]
for name in dict.fromkeys(changed):
    p=Path(name)
    if not p.is_file() or p.suffix!='.md': continue
    for target in re.findall(r'\]\(([^)]+)\)',p.read_text()):
        target=target.split('#',1)[0]
        if not target or '://' in target or target.startswith('mailto:'): continue
        if not (p.parent/target).exists(): broken.append(f'{p}: {target}')
print('\n'.join(broken) if broken else 'All local file links in changed Markdown resolve.')
