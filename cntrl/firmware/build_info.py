"""Source revision embedded by firmware and emulator builds."""

import subprocess
from pathlib import Path


def commit_id(source_root: Path) -> str:
    result = subprocess.run(
        ['git', '-C', str(source_root), 'rev-parse', '--verify', 'HEAD'],
        check=False,
        capture_output=True,
    )
    return result.stdout.decode('ascii').strip()[:7] if result.returncode == 0 else ''
