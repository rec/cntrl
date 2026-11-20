"""Embed the source revision in both wheels and source distributions."""

import subprocess
from pathlib import Path
from typing import cast

from hatchling.builders.hooks.plugin.interface import BuildHookInterface


class GitRefHook(BuildHookInterface):
    def initialize(self, version: str, build_data: dict[str, object]) -> None:
        root = Path(self.root)
        if (root / '.git').exists():
            ref = subprocess.check_output(
                ['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True
            )
        else:
            # Source distributions already contain the stamped package file.
            return
        stamp = root / 'build' / 'git-ref.txt'
        stamp.parent.mkdir(parents=True, exist_ok=True)
        stamp.write_text(ref)
        force_include = cast(dict[str, str], build_data['force_include'])
        force_include[str(stamp)] = 'cntrl/_git_ref.txt'
