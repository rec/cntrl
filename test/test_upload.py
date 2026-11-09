"""Public helper commands delegate to the selected target without hardware."""

import sys
from pathlib import Path

import pytest

from cntrl.patch_switcher import cli


@pytest.mark.parametrize(
    ('arguments', 'expected'),
    [
        (['serve'], 'Serve\n'),
        (['bootloader'], 'Bootloader\n'),
        (
            [
                'upload',
                '--image',
                '/worktree/image with spaces.hex',
                '--sha256',
                'a' * 64,
            ],
            'Upload\n/worktree/image with spaces.hex\n' + 'a' * 64 + '\n',
        ),
    ],
)
def test_public_helper_commands_preserve_arguments_output_and_exit_status(
    arguments: list[str],
    expected: str,
    monkeypatch: pytest.MonkeyPatch,
    capfd: pytest.CaptureFixture[str],
) -> None:
    target = Path(__file__).parent / 'fixtures/build_target'
    monkeypatch.setattr(
        sys, 'argv', ['cntrl', 'upload-helper', '--target-dir', str(target), *arguments]
    )
    with pytest.raises(SystemExit) as error:
        cli.main()
    assert error.value.code == 7
    assert capfd.readouterr().out == expected


def test_public_helper_command_rejects_a_target_without_an_entry(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(
        sys,
        'argv',
        ['cntrl', 'upload-helper', '--target-dir', str(tmp_path), 'serve'],
    )
    with pytest.raises(SystemExit, match='does not provide an upload helper entry'):
        cli.main()
