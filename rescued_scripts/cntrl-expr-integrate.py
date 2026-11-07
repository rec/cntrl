from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory

main = Path('/Users/tom/code/cntrl')
def git(*args, directory=main):
    return subprocess.check_output(['git', *args], cwd=directory)
for name in git('diff', '--name-only', 'HEAD').decode().splitlines():
    path = Path(name)
    base = git('show', f'HEAD:{name}')
    theirs = (main / name).read_bytes()
    ours = path.read_bytes()
    if ours == base:
        path.write_bytes(theirs)
    elif theirs != ours:
        with TemporaryDirectory() as d:
            b = Path(d, 'base'); b.write_bytes(base)
            t = Path(d, 'item1'); t.write_bytes(theirs)
            res = subprocess.run(['git', 'merge-file', '-p', '-L', 'expression', '-L', 'base', '-L', 'trs', name, str(b), str(t)], capture_output=True)
            if res.returncode > 1:
                print(name, res.returncode)
            path.write_bytes(res.stdout)
            if res.returncode: print('MERGE', name)
for name in git('ls-files', '--others', '--exclude-standard').decode().splitlines():
    if name.endswith(('/trs_ports.h', '/flex-serial-release.patch')):
        Path(name).write_bytes((main / name).read_bytes())
