"""Revision comparisons use editable configuration, not compiled layout records."""

from pathlib import Path

import tomlkit

from cntrl.patch_switcher import compiler, history, models, resolved


def test_comparison_reports_configuration_paths_and_preserves_order() -> None:
    root = Path(__file__).parents[1] / 'firmware/test'
    before = bytes.fromhex((root / 'show.golden').read_text())
    source = tomlkit.parse(resolved.export(before)).unwrap()
    source['brightness'] = 23
    source['patches']['wind'].reverse()
    target = models.Target.model_validate(compiler.read_document(root / 'target.json'))
    after = compiler.compile_show(source, {}, [], target)
    changes = history.compare(before, after)
    assert any(c.path == 'brightness' and c.after == 23 for c in changes)
    assert any('patches.wind[' in c.path for c in changes)
    assert history.compare(before, before) == []
    source['patches']['wind'].pop()
    shortened = compiler.compile_show(source, {}, [], target)
    for old, new in ((before, shortened), (shortened, before)):
        changes = history.compare(old, new)
        assert any(
            'patches.wind[' in c.path and (c.before is None or c.after is None)
            for c in changes
        )
        assert all(not c.path.startswith('.') for c in changes)


def test_history_command_accepts_the_documented_positional_action() -> None:
    import tyro

    command = tyro.cli(history.History, args=['Example', 'list', '--port', '/unused'])
    assert command.action == history.Action.list
    assert command.name == 'Example'
