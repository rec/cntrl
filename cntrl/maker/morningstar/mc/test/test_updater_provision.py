import hashlib
import sys
from pathlib import Path
from zipfile import ZipFile

import pytest

from cntrl.maker.morningstar.mc import updater_provision, upload_helper


@pytest.fixture
def package(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    directory = tmp_path / 'tytools-0.9.9'
    directory.mkdir()
    archive = directory / 'release.zip'
    with ZipFile(archive, 'w') as output:
        output.writestr('tycmd.exe', b'pinned uploader')
    monkeypatch.setattr(sys, 'platform', 'win32')
    monkeypatch.setattr(
        updater_provision,
        'ARCHIVES',
        {'win32': (archive.name, hashlib.sha256(archive.read_bytes()).hexdigest())},
    )
    monkeypatch.setattr(
        updater_provision,
        'BINARIES',
        {'win32': hashlib.sha256(b'pinned uploader').hexdigest()},
    )
    return directory


def test_provision_installs_verified_binary_and_keeps_matching_installation(
    package: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    path = updater_provision.provision(package)
    assert path.read_bytes() == b'pinned uploader'
    before = path.stat().st_mtime_ns
    assert updater_provision.provision(package) == path
    assert path.stat().st_mtime_ns == before
    monkeypatch.delenv('CNTRL_TYCMD', raising=False)
    monkeypatch.setattr(upload_helper, 'CACHE_DIRECTORY', package.parent)
    assert upload_helper.updater_path() == path
    path.write_bytes(b'changed')
    with pytest.raises(ValueError, match='executable checksum mismatch'):
        upload_helper.updater_path()


@pytest.mark.parametrize('corruption', ['archive', 'executable'])
def test_checksum_failure_does_not_publish_an_uploader(
    package: Path, corruption: str, monkeypatch: pytest.MonkeyPatch
) -> None:
    if corruption == 'archive':
        (package / 'release.zip').write_bytes(b'changed archive')
    else:
        monkeypatch.setattr(updater_provision, 'BINARIES', {'win32': '0' * 64})
    with pytest.raises(ValueError, match=f'{corruption} checksum mismatch'):
        updater_provision.provision(package)
    assert not (package / 'tycmd.exe').exists()
