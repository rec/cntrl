from pathlib import Path
from shutil import copyfile
import subprocess
root=Path('/private/tmp/cntrl-expr-apply')
root.mkdir(exist_ok=True)
patch=Path('/private/tmp/cntrl-expr-feature.patch')
files=[line[6:] for line in patch.read_text().splitlines() if line.startswith('+++ b/')]
for name in files:
    p=root/name
    p.parent.mkdir(parents=True, exist_ok=True)
    blob=subprocess.run(['git','show',f'HEAD:{name}'],capture_output=True)
    if blob.returncode==0: p.write_bytes(blob.stdout)
subprocess.run(['git','apply','--check',str(patch)],cwd=root,check=True)
subprocess.run(['git','apply',str(patch)],cwd=root,check=True)
for name in files:
    copyfile(root/name, name)
subprocess.run(['git','add','--',*files],check=True)
