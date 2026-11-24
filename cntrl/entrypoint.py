"""Install crash reporting before loading the command implementation."""

import sys

from .crashes import install


def main() -> None:
    command = next((a for a in sys.argv[1:] if not a.startswith('-')), '')
    install('emulator' if command == 'emulate' else 'cntrl')
    from .patch_chooser.cli import main

    main()
