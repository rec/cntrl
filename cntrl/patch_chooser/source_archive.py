"""Immutable authoring snapshots referenced by small setup envelope records."""

from hashlib import sha256
from importlib.metadata import version
from io import BytesIO
from pathlib import Path
from typing import Literal
from zipfile import ZIP_DEFLATED, BadZipFile, ZipFile, ZipInfo

import tomlkit
from pydantic import BaseModel, ConfigDict, Field, ValidationError
from tomlkit.exceptions import ParseError

from .. import source_files
from . import binary_format, data_files, device_globals, global_overlay, transport


class RecoveredSourceError(ValueError):
    """A source archive or recovered session could not be used."""


def compiler_identity() -> str:
    """Identify the installed compiler sources, including unreleased builds."""
    root = Path(__file__).resolve().parents[1]
    digest = sha256()
    for path in sorted(root.rglob('*.py')):
        relative = path.relative_to(root)
        if 'archive' not in relative.parts:
            digest.update(relative.as_posix().encode() + b'\0' + path.read_bytes())
    return digest.hexdigest()


class Manifest(BaseModel, frozen=True):
    format: int = Field(ge=1, le=1)
    compiler_version: str
    compiler_identity: str
    persistent_format: int
    setup_format: int
    kind: Literal['setups', 'songs']
    recipe: data_files.Sources
    files: dict[str, str]
    origins: dict[str, dict[str, str]] = Field(default_factory=dict)
    defaults: dict[str, str]
    baselines: dict[str, str]
    baseline_hashes: dict[str, str]

    model_config = ConfigDict(extra='forbid')


def create(
    recipe: data_files.Sources,
    trace: data_files.Trace,
    baselines: dict[str, bytes],
    *,
    kind: Literal['setups', 'songs'] = 'setups',
) -> bytes:
    """Capture bytes already read by compilation, without rereading source files."""
    from . import global_config, setup_sync

    recipe = setup_sync.recipe(recipe, trace)
    entries: dict[str, bytes] = {}
    files: dict[str, str] = {}
    for path, content in trace.files.items():
        key = 'files/' + sha256(content).hexdigest()
        files[str(path.resolve())] = key
        entries[key] = content
    baseline_files: dict[str, str] = {}
    hashes: dict[str, str] = {}
    for name, data in baselines.items():
        data_files.validate_name(name)
        baseline = detach(data)
        key = f'baselines/{name}.bin'
        baseline_files[name] = key
        hashes[name] = sha256(baseline).hexdigest()
        entries[key] = baseline
    documents = list(recipe.document)
    names = {e.name for e in data_files.expressions(documents)}
    for name, path in trace.roots.items():
        if name not in names:
            documents.extend(
                global_config.expression_arguments(
                    data_files.DataFile(name=name, paths=[path.resolve()])
                )
            )

    manifest = Manifest(
        format=1,
        compiler_version=version('cntrl'),
        compiler_identity=compiler_identity(),
        persistent_format=binary_format.DEVELOPMENT_FORMAT,
        setup_format=max(binary_format.SETUP_VERSIONS),
        kind=kind,
        recipe=recipe.model_copy(
            update={
                'document': documents,
            }
        ),
        files=files,
        origins={
            n: {
                str(o): str(p.resolve())
                for o, p in r.items()
                if str(p.resolve()) in files
            }
            for n, r in trace.relocations_by_setup.items()
        },
        defaults={
            n: str(p.resolve()) if (p := trace.defaults.get(n)) is not None else ''
            for n in baselines
        },
        baselines=baseline_files,
        baseline_hashes=hashes,
    )
    entries['manifest.toml'] = tomlkit.dumps(manifest.model_dump(mode='json')).encode()
    stream = BytesIO()
    with ZipFile(stream, 'w') as archive:
        for name, content in sorted(entries.items()):
            info = ZipInfo(name)
            info.compress_type = ZIP_DEFLATED
            archive.writestr(info, content)
    return stream.getvalue()


def identifier(data: bytes) -> str | None:
    if not data.startswith(binary_format.MAGIC):
        return None
    for record in binary_format.unpack(data, binary_format.Kind.setup).records[1:]:
        if record.tag == binary_format.SOURCE:
            return record.data.hex()
    return None


def attach(data: bytes, archive: bytes) -> bytes:
    document = binary_format.unpack(binary_format.wrap(data), binary_format.Kind.setup)
    return document.model_copy(
        update={
            'records': [
                *(r for r in document.records if r.tag != binary_format.SOURCE),
                binary_format.Record(
                    tag=binary_format.SOURCE, data=sha256(archive).digest()
                ),
            ]
        }
    ).pack()


def detach(data: bytes) -> bytes:
    if not data.startswith(binary_format.MAGIC):
        return data
    document = binary_format.unpack(data, binary_format.Kind.setup)
    return document.model_copy(
        update={
            'records': [r for r in document.records if r.tag != binary_format.SOURCE]
        }
    ).pack()


def upload(client: transport.SetupClient, archive: bytes) -> None:
    client.copy_source(sha256(archive).hexdigest(), archive)


def read(data: bytes) -> tuple[Manifest, dict[str, bytes]]:
    try:
        with ZipFile(BytesIO(data)) as archive:
            names = archive.namelist()
            if len(names) != len(set(names)):
                raise RecoveredSourceError('duplicate source archive entries')
            manifest = Manifest.model_validate(
                tomlkit.parse(archive.read('manifest.toml').decode()).unwrap()
            )
            entries = {
                n: archive.read(n)
                for n in set(manifest.files.values()) | set(manifest.baselines.values())
            }
    except (BadZipFile, KeyError, UnicodeError, ParseError, ValidationError) as error:
        raise RecoveredSourceError(f'invalid source archive: {error}') from error
    for key in manifest.files.values():
        if key != 'files/' + sha256(entries[key]).hexdigest():
            raise RecoveredSourceError('source archive file checksum mismatch')
    for name, key in manifest.baselines.items():
        data_files.validate_name(name)
        if sha256(entries[key]).hexdigest() != manifest.baseline_hashes.get(name):
            raise RecoveredSourceError(f'{name}: source baseline checksum mismatch')
        binary_format.core(entries[key])
    return manifest, entries


def download(
    client: transport.SetupClient,
    setups: dict[str, bytes],
    *,
    live_globals: global_overlay.Overlay | None = None,
) -> dict[Path, bytes]:
    """Return portable recovered sources and saved images for publish_download."""
    result: dict[Path, bytes] = {}
    language: tuple[str, bytes] | None = None
    if live_globals is not None and live_globals.language not in (None, 'en'):
        try:
            language = (
                live_globals.language,
                client.download_language(live_globals.language),
            )
        except transport.UsbFileMissing:
            pass
    snapshots = {v for d in setups.values() if (v := identifier(d)) is not None}
    for snapshot in sorted(snapshots):
        data = client.download_source(snapshot)
        if sha256(data).hexdigest() != snapshot:
            raise transport.UsbReplyError(
                f'{snapshot}: source archive checksum mismatch'
            )
        manifest, entries = read(data)
        base = Path('sessions') / snapshot
        files: dict[str, str] = {}
        roots = data_files.expressions(manifest.recipe.document)
        root_names = {str(e.paths[0]): e.name + e.paths[0].suffix for e in roots}
        global_paths = {p for p in manifest.defaults.values() if p}
        for original, key in manifest.files.items():
            name = root_names.get(original)
            if name in ('cntrl-session.toml', 'originals.zip'):
                name = (
                    'setup-'
                    + sha256(original.encode()).hexdigest()
                    + Path(original).suffix
                )
            if name is None:
                suffix = Path(original).suffix or '.bin'
                stem = sha256(original.encode()).hexdigest()
                name = (
                    f'global-{stem}.toml'
                    if original in global_paths
                    else f'source-{stem}{suffix}'
                )
            files[original] = name
            result[base / name] = entries[key]
        baselines = {n: f'baselines/{n}.bin' for n in manifest.baselines}
        recovered = manifest.model_copy(update={'files': files, 'baselines': baselines})
        result[base / 'cntrl-session.toml'] = tomlkit.dumps(
            recovered.model_dump(mode='json')
        ).encode()
        result[base / 'originals.zip'] = data
        if live_globals is not None:
            result[base / 'device-globals.toml'] = device_globals.dump(live_globals)
            result[base / 'pedal/device-globals.toml'] = device_globals.dump(
                live_globals
            )
        if language is not None:
            result[base / 'languages' / f'{language[0]}.bin'] = language[1]
        for name, key in manifest.baselines.items():
            result[base / baselines[name]] = entries[key]
        for name, current in setups.items():
            if identifier(current) == snapshot:
                result[base / 'pedal' / f'{name}.bin'] = current
    return result


def session_context(
    expression: data_files.DataFile,
    *,
    snapshots: dict[Path, bytes] | None = None,
    overrides: dict[Path, bytes] | None = None,
) -> tuple[dict[str, Path | None], dict[Path, Path]]:
    directory = expression.paths[0].resolve().parent
    path = directory / 'cntrl-session.toml'
    if not path.exists():
        return {}, {}
    manifest = _read_manifest(directory, snapshots, overrides)
    local_files = {Path(n): _local(directory, p) for n, p in manifest.files.items()}
    if expression.paths[0].resolve() not in local_files.values():
        raise RecoveredSourceError(
            f'{expression.name}: setup is not part of this recovered session'
        )
    matches = [
        e.name
        for e in data_files.expressions(manifest.recipe.document)
        if local_files.get(e.paths[0]) == expression.paths[0].resolve()
    ]
    if expression.name in matches:
        context_name = expression.name
    elif len(matches) == 1:
        context_name = matches[0]
    else:
        raise RecoveredSourceError(
            f'{expression.name}: ambiguous recovered setup source'
        )
    relocations = {
        Path(n): local_files[Path(p)]
        for n, p in manifest.origins.get(context_name, {}).items()
    }
    relocations.update({n: p for n, p in local_files.items() if n not in relocations})
    defaults: dict[str, Path | None] = {}
    for name, original in manifest.defaults.items():
        if not original:
            defaults[name] = None
        elif Path(original) not in local_files:
            raise RecoveredSourceError(
                f'{name}: recorded global defaults source is missing'
            )
        else:
            defaults[name] = local_files[Path(original)]
    if context_name not in defaults:
        raise RecoveredSourceError(
            f'{context_name}: recorded global defaults binding is missing'
        )
    defaults[expression.name] = defaults[context_name]
    return defaults, relocations


def recovered_recipe(
    directory: Path,
    *,
    snapshots: dict[Path, bytes] | None = None,
    overrides: dict[Path, bytes] | None = None,
) -> data_files.Sources:
    from .global_config import expression_arguments

    manifest = _read_manifest(directory, snapshots, overrides)
    relocations = {Path(n): _local(directory, p) for n, p in manifest.files.items()}

    def relocate(values: list[str]) -> list[str]:
        result: list[str] = []
        for expression in data_files.expressions(values):
            paths: list[Path] = []
            for path in expression.paths:
                if path not in relocations:
                    raise RecoveredSourceError(f'{path}: recorded source is missing')
                paths.append(relocations[path])
            result.extend(
                expression_arguments(
                    data_files.DataFile(name=expression.name, paths=paths)
                )
            )
        return result

    return manifest.recipe.model_copy(
        update={
            'document': relocate(manifest.recipe.document),
            'target': relocate(manifest.recipe.target),
            'definitions': relocate(manifest.recipe.definitions),
            'table': [relocate(t) for t in manifest.recipe.table],
            'global_defaults': {
                n: relocations[Path(p)] if p else None
                for n, p in manifest.defaults.items()
            },
        }
    )


def recovered_baselines(directory: Path) -> dict[str, bytes]:
    path = directory / 'cntrl-session.toml'
    if not path.exists():
        return {}
    manifest = Manifest.model_validate(tomlkit.parse(path.read_text()).unwrap())
    result: dict[str, bytes] = {}
    for name, relative in manifest.baselines.items():
        content = _local(directory, relative).read_bytes()
        if sha256(content).hexdigest() != manifest.baseline_hashes.get(name):
            raise ValueError(f'{name}: recovered baseline checksum mismatch')
        result[name] = content
    return result


def source_relocations(
    path: Path,
    *,
    snapshots: dict[Path, bytes] | None = None,
    overrides: dict[Path, bytes] | None = None,
) -> dict[Path, Path]:
    """Resolve a declaring file without assuming it is a root setup document."""
    directory = path.resolve().parent
    if not (directory / 'cntrl-session.toml').exists():
        return {}
    manifest = _read_manifest(directory, snapshots, overrides)
    files = {Path(n): _local(directory, p) for n, p in manifest.files.items()}
    captured = next((n for n, p in files.items() if p == path.resolve()), None)
    if captured is None:
        return {}
    contexts = [r for r in manifest.origins.values() if str(captured) in r.values()]
    if not contexts:
        return files
    mappings = [{Path(n): files[Path(p)] for n, p in r.items()} for r in contexts]
    common = {
        n: p for n, p in mappings[0].items() if all(r.get(n) == p for r in mappings[1:])
    }
    conflicts = {n for r in mappings for n in r} - common.keys()
    # Captured-path aliases let source_reference recognize relocated absolute paths.
    common.update(
        {n: p for n, p in files.items() if n not in common and n not in conflicts}
    )
    return common


def _read_manifest(
    directory: Path,
    snapshots: dict[Path, bytes] | None = None,
    overrides: dict[Path, bytes] | None = None,
) -> Manifest:
    path = (directory / 'cntrl-session.toml').resolve()
    try:
        content = (
            overrides[path]
            if overrides is not None and path in overrides
            else source_files.read(path, snapshots)
        )
        return Manifest.model_validate(tomlkit.parse(content.decode()).unwrap())
    except (OSError, UnicodeDecodeError, ParseError, ValidationError) as error:
        raise RecoveredSourceError(f'{path}: {error}') from error


def _local(directory: Path, relative: str) -> Path:
    path = (directory / relative).resolve()
    if Path(relative).is_absolute() or not path.is_relative_to(directory.resolve()):
        raise RecoveredSourceError(f'{relative}: invalid recovered source path')
    if not path.is_file():
        raise RecoveredSourceError(f'{relative}: recorded source is missing')
    return path
