"""Verify packaged firmware resources and cache their exact stock dependency."""

import hashlib
import os
import sys
from pathlib import Path
from tempfile import NamedTemporaryFile
from typing import Annotated, Literal
from urllib.request import urlopen

import tomlkit
from pydantic import BaseModel, ConfigDict, Field


class Stock(BaseModel, frozen=True):
    version: str
    filename: str
    url: str
    sha256: str = Field(pattern=r'^[0-9a-f]{64}$')

    model_config = ConfigDict(extra='forbid')


class Manifest(BaseModel, frozen=True):
    format: Literal[1] = 1
    controller: str = Field(pattern=r'^[a-z0-9]+(?:-[a-z0-9]+)*$')
    source_commit: str = Field(pattern=r'^[0-9a-f]{40}$')
    inputs_sha256: str = Field(pattern=r'^[0-9a-f]{64}$')
    application: str
    application_origin: int
    application_entry: int
    hook: str
    stock_reader: int
    switch_window_ms: int
    target: str
    starter: str
    stock: Stock
    files: dict[str, Annotated[str, Field(pattern=r'^[0-9a-f]{64}$')]]

    model_config = ConfigDict(extra='forbid')


def cache_directory() -> Path:
    return (
        Path(os.environ.get('LOCALAPPDATA', Path.home() / 'AppData/Local'))
        if os.name == 'nt'
        else Path.home() / 'Library/Caches'
        if sys.platform == 'darwin'
        else Path(os.environ.get('XDG_CACHE_HOME', Path.home() / '.cache'))
    )


def resource(directory: Path, name: str) -> Path:
    path = (directory / name).resolve()
    if (
        '\\' in name
        or Path(name).is_absolute()
        or not path.is_relative_to(directory.resolve())
    ):
        raise ValueError(f'{name}: firmware resource must stay inside its directory')
    return path


def read_manifest(directory: Path) -> Manifest:
    manifest = Manifest.model_validate(
        tomlkit.parse((directory / 'manifest.toml').read_text()).unwrap()
    )
    for name in (
        manifest.application,
        manifest.hook,
        manifest.target,
        manifest.starter + '/bundle.bin',
        manifest.starter + '/songs.bin',
    ):
        if name not in manifest.files:
            raise ValueError(f'{name}: missing firmware resource hash')
    for name, digest in manifest.files.items():
        path = resource(directory, name)
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError(f'{path}: packaged firmware resource checksum mismatch')
    return manifest


def controller_directory(controller: str) -> Path:
    root = Path(__file__).resolve().parents[1]
    matches = [
        p.parent
        for p in (root / 'maker').glob('**/distribution/manifest.toml')
        if tomlkit.parse(p.read_text()).get('controller') == controller
    ]
    if len(matches) != 1:
        raise ValueError(f'unknown or ambiguous physical controller: {controller}')
    return matches[0]


def cached_stock(stock: Stock, directory: Path) -> Path:
    if (
        '\\' in stock.filename
        or Path(stock.filename).name != stock.filename
        or stock.filename in ('', '.', '..')
    ):
        raise ValueError('stock filename must be a plain vendor filename')
    path = directory / stock.filename
    if path.exists():
        if hashlib.sha256(path.read_bytes()).hexdigest() != stock.sha256:
            raise ValueError(
                f'{path}: stock checksum mismatch; expected {stock.version} '
                f'SHA-256 {stock.sha256}. Remove the corrupt cache file explicitly.'
            )
        return path
    directory.mkdir(parents=True, exist_ok=True)
    with NamedTemporaryFile(prefix='download-', dir=directory, delete=False) as stream:
        temporary = Path(stream.name)
        try:
            with urlopen(stock.url, timeout=60) as response:
                while chunk := response.read(1024 * 1024):
                    stream.write(chunk)
            stream.flush()
            if hashlib.sha256(temporary.read_bytes()).hexdigest() != stock.sha256:
                raise ValueError(
                    f'{stock.filename}: downloaded stock checksum mismatch; '
                    f'expected {stock.version} SHA-256 {stock.sha256}'
                )
        except (OSError, ValueError):
            stream.close()
            temporary.unlink()
            raise
    temporary.replace(path)
    return path
