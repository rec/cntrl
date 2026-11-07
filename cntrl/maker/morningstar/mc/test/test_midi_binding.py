"""Physical MIDI sockets retain their binding when logical routes are reordered."""

import subprocess
from pathlib import Path


def test_route_permutation_preserves_socket_and_usb_cable_binding(
    tmp_path: Path,
) -> None:
    adapter = Path(__file__).resolve().parents[1]
    root = adapter.parents[3]
    program = tmp_path / 'midi-binding'
    subprocess.run(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(adapter / 'firmware/test_midi_binding.cpp'),
            '-o',
            str(program),
        ],
        check=True,
    )
    subprocess.run([str(program)], check=True)
