"""Exact stock caching and integrity of installed firmware resource sets."""

import hashlib
from io import BytesIO
from pathlib import Path
from unittest.mock import Mock

import pytest
import tomlkit

from cntrl.firmware import distribution


@pytest.fixture
def stock() -> distribution.Stock:
    return distribution.Stock(
        version='1.0',
        filename='vendor-1.0.hex',
        url='https://example.test/vendor-1.0.hex',
        sha256=hashlib.sha256(b'verified stock').hexdigest(),
    )


def test_verified_stock_is_reused_without_a_network_request(
    stock: distribution.Stock, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    download = Mock(return_value=BytesIO(b'verified stock'))
    monkeypatch.setattr(distribution, 'urlopen', download)
    path = distribution.cached_stock(stock, tmp_path / 'cache')
    assert path.name == stock.filename and path.read_bytes() == b'verified stock'
    assert distribution.cached_stock(stock, tmp_path / 'cache') == path
    download.assert_called_once_with(stock.url, timeout=60)


def test_changed_stock_is_never_published_or_silently_downloaded_again(
    stock: distribution.Stock, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    download = Mock(return_value=BytesIO(b'changed bytes at same URL'))
    monkeypatch.setattr(distribution, 'urlopen', download)
    with pytest.raises(ValueError, match='downloaded stock checksum mismatch'):
        distribution.cached_stock(stock, tmp_path)
    assert list(tmp_path.iterdir()) == []
    path = tmp_path / stock.filename
    path.write_bytes(b'corrupt cache')
    with pytest.raises(ValueError, match='Remove the corrupt cache file explicitly'):
        distribution.cached_stock(stock, tmp_path)
    assert download.call_count == 1
    assert path.read_bytes() == b'corrupt cache'


def test_interrupted_download_does_not_leave_a_cache_entry(
    stock: distribution.Stock, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    stream = Mock()
    stream.__enter__ = Mock(return_value=stream)
    stream.__exit__ = Mock(return_value=False)
    stream.read.side_effect = [b'partial bytes', OSError('interrupted')]
    monkeypatch.setattr(distribution, 'urlopen', Mock(return_value=stream))
    with pytest.raises(OSError, match='interrupted'):
        distribution.cached_stock(stock, tmp_path)
    assert list(tmp_path.iterdir()) == []


@pytest.fixture
def resources(tmp_path: Path, stock: distribution.Stock) -> Path:
    (tmp_path / 'starter').mkdir()
    files = [
        'application.bin',
        'hook.bin',
        'target.toml',
        'starter/bundle.bin',
        'starter/songs.bin',
    ]
    for name in files:
        (tmp_path / name).write_bytes(name.encode())
    manifest = distribution.Manifest(
        controller='test',
        source_commit='a' * 40,
        inputs_sha256='b' * 64,
        application='application.bin',
        application_origin=0,
        application_entry=4,
        hook='hook.bin',
        stock_reader=8,
        switch_window_ms=50,
        target='target.toml',
        starter='starter',
        stock=stock,
        files={
            n: hashlib.sha256((tmp_path / n).read_bytes()).hexdigest() for n in files
        },
    )
    (tmp_path / 'manifest.toml').write_text(tomlkit.dumps(manifest.model_dump()))
    return tmp_path


def test_all_packaged_resources_are_verified_before_use(resources: Path) -> None:
    assert distribution.read_manifest(resources).controller == 'test'
    (resources / 'hook.bin').write_bytes(b'wrong hook')
    with pytest.raises(ValueError, match='checksum mismatch'):
        distribution.read_manifest(resources)


@pytest.mark.parametrize('name', ['../outside.bin', '/outside.bin'])
def test_resources_cannot_escape_the_package(resources: Path, name: str) -> None:
    with pytest.raises(ValueError, match='inside its directory'):
        distribution.resource(resources, name)
