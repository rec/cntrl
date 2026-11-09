"""Run the adapter's native display and boot-selection checks on each host."""

from pathlib import Path

import tyro
from pydantic import BaseModel, Field

from scripts.native_tests import compile_native, run_native


class Tests(BaseModel, frozen=True):
    source_root: Path = Field(
        default_factory=lambda: Path(__file__).resolve().parents[4]
    )
    output: Path = Path(__file__).parent / 'firmware/build/native'


def run(options: Tests) -> None:
    root = Path(__file__).resolve().parent / 'firmware'
    for name, sources, defines in TESTS:
        binary = options.output / name
        compile_native(
            [
                'c++',
                '-std=c++17',
                '-Wall',
                '-Wextra',
                '-Werror',
                '-fsanitize=address,undefined',
                f'-I{options.source_root.resolve() / "firmware/include"}',
                f'-I{root / "test_support"}',
                f'-I{root / "usb_probe"}',
                *defines,
                *(str(root / s) for s in sources),
                '-o',
                str(binary),
            ]
        )
        run_native([str(binary)])


TESTS = [
    ('switch-gesture', ['test_switch_gesture.cpp'], []),
    ('boot-selection', ['test_boot_selection.cpp'], []),
    ('pair-timing', ['test_pair_timing.cpp'], []),
    ('renderer', ['test_renderer.cpp'], []),
    ('displays', ['test_displays.cpp', 'usb_probe/displays.cpp'], []),
    (
        'panorama',
        ['test_displays.cpp', 'patch_switcher/displays.cpp'],
        ['-DCNTRL_PANORAMA'],
    ),
    (
        'scroll-displays',
        ['test_displays.cpp', 'scroll_probe/displays.cpp'],
        ['-DCNTRL_SCROLL_PROBE'],
    ),
]


if __name__ == '__main__':
    run(tyro.cli(Tests))
