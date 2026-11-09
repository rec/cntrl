"""History backup metadata retains full revision identities and ordering."""

from pathlib import Path

import pytest

from cntrl.patch_switcher import compiler, history, models, transport
from scripts import native_tests


class Client(transport.SetupClient):
    def __init__(self, responses: list[str]) -> None:
        self.responses = iter(responses)
        self.commands: list[str] = []

    def request(self, command: str) -> str:
        self.commands.append(command)
        return next(self.responses)


def test_revision_metadata_preserves_labels_times_and_full_width_numbers() -> None:
    client = Client(
        [
            'CNTRL REVISION 18446744073709551614 4294967295 1856 52656865617273616c',
            'CNTRL REVISION 3 1234 1854 -',
            'CNTRL END',
        ]
    )
    assert history.revisions(client, 'Evening') == [
        history.Revision(
            number=2**64 - 2, modified=2**32 - 1, length=1856, label='Rehearsal'
        ),
        history.Revision(number=3, modified=1234, length=1854),
    ]
    assert client.commands[-1].endswith(' 3')


def test_repeated_revision_cannot_make_backup_loop_forever() -> None:
    client = Client(['CNTRL REVISION 3 1234 1854 -'] * 2)
    with pytest.raises(ValueError, match='revision order'):
        history.revisions(client, 'Evening')


def test_history_overlay_preserves_old_slots_and_reserved_numbers(
    tmp_path: Path,
) -> None:
    root = Path(__file__).parents[1]
    target = models.Target.model_validate(
        compiler.read_document(root / 'firmware/test/target.json')
    )
    data = compiler.compile_show({}, {}, [], target)
    record = history.pack_revision(
        'Evening',
        history.Revision(number=42, modified=123, length=len(data), label='Soundcheck'),
        data,
    )
    image = tmp_path / 'revision.bin'
    image.write_bytes(record)
    program = tmp_path / 'history-import'
    native_tests.compile_native(
        [
            'c++',
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-fsanitize=address,undefined',
            f'-I{root / "firmware/include"}',
            str(root / 'firmware/test/test_history_import.cpp'),
            '-o',
            str(program),
        ],
        check=True,
    )
    native_tests.run_native([str(program), str(image)], check=True)
