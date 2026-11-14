"""Keep host operating-system headers out of the shared firmware core."""

from pathlib import Path
from shutil import copyfile

import pytest

from scripts.qualify_portable import check_dependencies


@pytest.mark.parametrize(
    ('location', 'headers', 'allowed'),
    [
        ('firmware/emulator/main.cpp', ['fcntl.h', 'io.h'], True),
        ('firmware/test/peer.cpp', ['poll.h', 'unistd.h', 'windows.h'], True),
        ('firmware/include/cntrl/core.h', ['fcntl.h', 'io.h'], False),
        ('firmware/emulator/main.cpp', ['Arduino.h'], False),
    ],
)
def test_host_headers_stay_at_host_boundaries(
    tmp_path: Path, location: str, headers: list[str], allowed: bool
) -> None:
    source = tmp_path / location
    source.parent.mkdir(parents=True)
    source.write_text(''.join(f'#include <{h}>\n' for h in headers))
    if allowed:
        check_dependencies(tmp_path)
    else:
        with pytest.raises(ValueError, match='nonportable external include'):
            check_dependencies(tmp_path)


@pytest.mark.parametrize(
    'fixture,allowed', [('replace_input.py', True), ('replace_output.py', False)]
)
def test_replacement_output_is_checked_but_search_patterns_are_not(
    tmp_path: Path, fixture: str, allowed: bool
) -> None:
    destination = tmp_path / 'scripts/generator.py'
    destination.parent.mkdir()
    copyfile(Path(__file__).parent / 'fixtures/portable' / fixture, destination)
    if allowed:
        check_dependencies(tmp_path)
    else:
        with pytest.raises(ValueError, match='nonportable external include'):
            check_dependencies(tmp_path)
