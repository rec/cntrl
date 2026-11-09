"""Exercise helper command forwarding without running a service or device."""

import sys

import tyro

from cntrl.upload import Bootloader, Serve, Upload

if __name__ == '__main__':
    command = tyro.cli(Serve | Upload | Bootloader)
    print(type(command).__name__)
    if isinstance(command, Upload):
        print(command.image)
        print(command.sha256)
    sys.exit(7)
