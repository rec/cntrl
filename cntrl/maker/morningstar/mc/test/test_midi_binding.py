"""Physical MIDI sockets retain their binding when logical routes are reordered."""

from pathlib import Path

from scripts import native_tests


def test_route_permutation_preserves_socket_and_usb_cable_binding(
    tmp_path: Path,
) -> None:
    adapter = Path(__file__).resolve().parents[1]
    root = adapter.parents[3]
    program = tmp_path / 'midi-binding'
    native_tests.compile_native(
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
    native_tests.run_native([str(program)], check=True)
