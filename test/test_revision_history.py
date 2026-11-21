"""Revision comparisons use editable configuration, not compiled layout records."""

import sys
from pathlib import Path
from unittest.mock import Mock

import pytest
import tomlkit

from cntrl.patch_chooser import (
    cli,
    compiler,
    compiler_inputs,
    history,
    models,
    resolved,
    transport,
)


def test_reserved_revisions_follow_the_selected_storage_root(tmp_path: Path) -> None:
    (tmp_path / 'status.txt').write_text('CNTRL READY 41 0 00000000 persistent_root=42')
    (tmp_path / 'storage.toml').write_text(
        tomlkit.dumps(
            {
                'entries': [
                    {
                        'directory': True,
                        'path': '/cntrl/generations/0000002a/history/Evening/'
                        '0000000000000003',
                    },
                    {
                        'directory': True,
                        'path': '/cntrl/generations/00000029/history/Evening/'
                        '0000000000000004',
                    },
                    {
                        'directory': True,
                        'path': '/cntrl/history/Evening/0000000000000005',
                    },
                ]
            }
        )
    )
    assert history.reserved_numbers(tmp_path) == [('Evening', 3)]


def test_comparison_reports_configuration_paths_and_preserves_order() -> None:
    root = Path(__file__).parents[1] / 'firmware/test'
    before = bytes.fromhex((root / 'show.golden').read_text())
    source = tomlkit.parse(resolved.export(before)).unwrap()
    source['brightness'] = 23
    source['patch_cycles']['wind'].reverse()
    target = models.Target.model_validate(
        compiler_inputs.read_document(root / 'target.json')
    )
    after = compiler.compile_show(source, {}, [], target)
    changes = history.compare(before, after)
    assert any(c.path == 'brightness' and c.after == 23 for c in changes)
    assert any('patch_cycles.wind[' in c.path for c in changes)
    assert history.compare(before, before) == []
    source['patch_cycles']['wind'].pop()
    shortened = compiler.compile_show(source, {}, [], target)
    for old, new in ((before, shortened), (shortened, before)):
        changes = history.compare(old, new)
        assert any(
            'patch_cycles.wind[' in c.path and (c.before is None or c.after is None)
            for c in changes
        )
        assert all(not c.path.startswith('.') for c in changes)


def test_history_command_accepts_the_documented_positional_action() -> None:
    import tyro

    command = tyro.cli(history.History, args=['Example', 'list', '--port', '/unused'])
    assert command.action == history.Command.list
    assert command.name == 'Example'


@pytest.mark.parametrize(
    ('action', 'options', 'message'),
    [
        ('download', ['--revision', '1'], 'download requires --output'),
        ('restore', [], 'choose --revision'),
        ('save', ['--label', 'bad/name'], 'invalid setup name'),
    ],
)
def test_history_rejects_bad_arguments_before_usb(
    monkeypatch: pytest.MonkeyPatch, action: str, options: list[str], message: str
) -> None:
    connect = Mock(side_effect=AssertionError('unexpected USB open'))
    monkeypatch.setattr(transport, 'SerialConnection', connect)
    monkeypatch.setattr(
        sys,
        'argv',
        ['cntrl', 'setup', 'history', 'Example', action, '--port', '/unused', *options],
    )
    with pytest.raises(SystemExit, match=message):
        cli.main()
    connect.assert_not_called()


@pytest.mark.parametrize('error_type', [ValueError, OSError])
def test_history_cli_preserves_unexpected_failures(
    monkeypatch: pytest.MonkeyPatch, error_type: type[Exception]
) -> None:
    failure = error_type('unexpected history failure')
    monkeypatch.setattr(history, 'run', Mock(side_effect=failure))
    monkeypatch.setattr(
        sys,
        'argv',
        ['cntrl', 'setup', 'history', 'Example', 'list', '--port', '/unused'],
    )
    with pytest.raises(error_type) as caught:
        cli.main()
    assert caught.value is failure


@pytest.mark.parametrize(
    'reply',
    [
        'CNTRL REVISION broken 1 116 -',
        'CNTRL REVISION 1 0 0 -',
        'CNTRL REVISION 1 1 116 ff',
    ],
)
def test_invalid_revision_is_reported_as_a_pedal_reply_error(
    monkeypatch: pytest.MonkeyPatch, reply: str
) -> None:
    client = transport.SetupClient(transport.SerialConnection(Path('/unused')))
    monkeypatch.setattr(client, 'request', Mock(return_value=reply))
    with pytest.raises(transport.UsbReplyError, match='invalid revision response'):
        history.revisions(client, 'Example')
