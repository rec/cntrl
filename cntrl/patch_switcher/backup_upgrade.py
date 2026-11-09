"""Convert an immutable device backup before installing a new setup format."""

import hashlib
import shutil
import struct
from pathlib import Path

import tomlkit
from pydantic import BaseModel

from . import compiler, data_files, legacy, migrate, models, resolved


class Upgrade(BaseModel, frozen=True):
    directory: Path
    output: Path
    target: Path


def run(command: Upgrade) -> None:
    errors: list[str] = []
    target = models.Target.model_validate(
        data_files.load_target(
            data_files.DataFile(name='target', paths=[command.target]), True, errors
        )
    )
    if errors:
        raise compiler.CompilationError(errors)
    source = command.directory.resolve()
    output = command.output.resolve()
    if output.is_relative_to(source):
        raise ValueError('converted backup must be outside the source backup')
    movies = {
        int(p.stem, 16): p.read_bytes()
        for p in (source / 'setups/movies').glob('*.cnsa')
    }
    if movies:
        if target.animation is None:
            raise ValueError('saved movies require target animation geometry')
        converted_movies, movie_keys = migrate.upgrade_movies(movies, target.animation)
    else:
        converted_movies, movie_keys = {}, {}
    names = sorted((p.stem for p in (source / 'setups').glob('*.bin')), key=str.lower)
    if not names:
        raise ValueError('backup contains no setup binaries')
    previous_movies: dict[str, list[int]] = {}
    for name in names:
        text = tomlkit.parse(
            (source / 'setups' / f'{name}.toml').read_bytes().decode()
        ).unwrap()
        previous_movies[name] = [
            int(Path(p).stem, 16) for p in text.get('splash_movie', [])
        ]
    selected = (source / 'selected.txt').read_bytes().decode()
    default_movies = previous_movies.get(selected, [])
    records: list[dict[str, object]] = []

    def convert(data: bytes, name: str, path: str) -> bytes:
        source_hash = hashlib.sha256(data).hexdigest()
        version = struct.unpack_from('<H', data, 4)[0]
        if version < 34:
            data = legacy.upgrade(
                data, target, names, previous_movies.get(name, default_movies)
            )
        result = (
            migrate.upgrade_setup(data, target, movie_keys) if version < 35 else data
        )
        resolved.graph_offset(result)
        models.Setup.model_validate(tomlkit.parse(resolved.export(result)).unwrap())
        records.append(
            {
                'path': path,
                'source_version': version,
                'source_sha256': source_hash,
                'converted_sha256': hashlib.sha256(result).hexdigest(),
            }
        )
        return result

    setups = {
        n: convert((source / 'setups' / f'{n}.bin').read_bytes(), n, f'setups/{n}.bin')
        for n in names
    }
    history_document = (
        tomlkit.parse((source / 'history.toml').read_bytes().decode()).unwrap()
        if (source / 'history.toml').is_file()
        else {'revisions': []}
    )
    snapshots: dict[str, bytes] = {}
    for record in history_document.get('revisions', []):
        name, number = record['name'], record['number']
        path = f'history/{name}/{number}.bin'
        converted = convert((source / path).read_bytes(), name, path)
        snapshots[path] = converted
        record['version'] = compiler.DOCUMENT_VERSION
        record['length'] = len(converted)
    library = migrate.upgrade_songs(
        (source / 'songs.bin').read_bytes(), target, movie_keys, names, default_movies
    )

    # Leave the source untouched. Old movie identities live in the source backup;
    # the converted directory contains only assets accepted by the new firmware.
    def exclude_movies(directory: str, entries: list[str]) -> list[str]:
        return entries if Path(directory) == source / 'setups/movies' else []

    shutil.copytree(source, output, ignore=exclude_movies)
    for name, data in setups.items():
        (output / 'setups' / f'{name}.bin').write_bytes(data)
        (output / 'setups' / f'{name}.toml').write_bytes(resolved.export(data).encode())
    for path, data in snapshots.items():
        (output / path).write_bytes(data)
        (output / path).with_suffix('.toml').write_bytes(resolved.export(data).encode())
    for key, data in converted_movies.items():
        (output / 'setups/movies').mkdir(exist_ok=True)
        (output / 'setups/movies' / f'{key:08x}.cnsa').write_bytes(data)
    (output / 'history.toml').write_bytes(tomlkit.dumps(history_document).encode())
    (output / 'songs.bin').write_bytes(library)
    (output / 'conversion.toml').write_bytes(
        tomlkit.dumps(
            {'format': compiler.DOCUMENT_VERSION, 'records': records}
        ).encode()
    )
    print(
        f'Converted {len(setups)} setups and {len(snapshots)} history revisions '
        f'into {output}'
    )
