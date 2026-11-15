"""First-install gates and device-free preparation of packaged firmware."""

from pathlib import Path
from unittest.mock import Mock

import pytest

from cntrl.firmware import distribution
from cntrl.maker.morningstar.mc import flash_target, release


@pytest.mark.parametrize(
    'options',
    [
        {'first_install': True},
        {'first_install': True, 'serial': '12345', 'stock_version': 'old'},
        {'stock_version': '3.14.0'},
    ],
)
def test_first_install_requires_explicit_supported_stock_and_backup(
    options: dict[str, object], tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    prepare = Mock()
    monkeypatch.setattr(release, 'prepare', prepare)
    with pytest.raises(ValueError):
        flash_target.run(
            flash_target.Flash.model_validate(
                {'directory': tmp_path / 'install', **options}
            )
        )
    prepare.assert_not_called()


def test_installation_refuses_to_overwrite_previous_evidence(tmp_path: Path) -> None:
    with pytest.raises(ValueError, match='preserve previous evidence'):
        flash_target.run(flash_target.Flash(directory=tmp_path, prepare_only=True))


def test_manifest_paths_and_hashes_match_committed_payload() -> None:
    resources = Path(release.__file__).with_name('distribution')
    if not resources.exists():
        pytest.skip('compiled resource commit follows implementation commit')
    manifest = distribution.read_manifest(resources)
    assert manifest.stock == release.STOCK
    assert manifest.controller == 'mc6-pro'
    assert not any(n.endswith('.hex') for n in manifest.files)


def test_starter_has_no_musical_actions() -> None:
    from cntrl.patch_switcher import compiler, data_files, models

    root = Path(release.__file__).parent
    errors: list[str] = []
    target = models.Target.model_validate(
        data_files.load_target(
            data_files.DataFile(
                name='target', paths=[root / 'patch-switcher-target.json']
            ),
            True,
            errors,
        )
    )
    assert not errors
    source = compiler.read_document(root / 'starter.toml')
    data = compiler.compile_show(source, {'profiles': []}, [], target)
    result = models.Setup.model_validate(compiler.read_document(root / 'starter.toml'))
    assert not result.patches and not result.instruments
    assert result.midi == {}
    assert not result.usb_device_midi and not result.usb_host_midi
    assert data
