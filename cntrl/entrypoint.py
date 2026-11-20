"""Install crash reporting before loading the command implementation."""

import sys

from .crashes import install


def main() -> None:
    install('emulator' if sys.argv[1:2] == ['emulate'] else 'cntrl')
    from .patch_chooser.cli import main

    main()
