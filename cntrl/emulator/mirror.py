"""Capture a pedal's active persistent files without recompiling its setups."""

from hashlib import sha256
from pathlib import Path
from tempfile import TemporaryDirectory
from uuid import uuid4

import tomlkit

from ..patch_chooser import binary_format, storage_info, transport
from ..patch_chooser.backups import persistent_prefix


def download(
    port: Path, storage: Path | None, controller: str | None
) -> tuple[str, Path, Path]:
    with transport.SerialConnection(port, timeout=5) as connection:
        client = transport.SetupClient(connection)
        status = client.request('CNTRL STATUS')
        fields = dict(v.split('=', 1) for v in status.split() if '=' in v)
        model = fields.get('model')
        if not model:
            raise ValueError('pedal status does not identify its controller model')
        if controller is not None and controller != model:
            raise ValueError(
                f'pedal model {model} does not match --controller {controller}'
            )
        if fields.get('persistent_format') != str(binary_format.DEVELOPMENT_FORMAT):
            raise ValueError(
                'pedal persistent format differs from this emulator; '
                'update firmware first'
            )
        root = storage or Path('.cntrl-emulator') / model
        root.mkdir(parents=True, exist_ok=True)
        directory = root / f'mirror-{uuid4().hex}'
        with TemporaryDirectory(prefix='.mirror-', dir=root) as staging:
            draft = Path(staging) / 'capture'
            draft.mkdir()
            setup = capture(client, draft, status)
            draft.rename(directory)
    return model, directory / setup.name, directory


def capture(client: transport.SetupClient, directory: Path, status: str) -> Path:
    """Normalize the active deployment root, preserving journal and asset bytes."""
    selected = client.current()
    prefix = persistent_prefix(status)
    manifest = storage_manifest(client, prefix)
    files: list[dict[str, object]] = []
    checksums: dict[str, str] = {}
    for physical, (logical, size) in manifest.items():
        try:
            data = client.download_state(physical)
        except (OSError, ValueError) as error:
            raise ValueError(
                f'{physical}: cannot capture pedal file: {error}'
            ) from error
        if len(data) != size:
            raise ValueError(f'{physical}: storage changed during mirror capture')
        path = directory / logical.lstrip('/')
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        checksums[physical] = sha256(data).hexdigest()
        files.append(dict(path=logical, sha256=checksums[physical]))
    bundle = client.download_bundle()
    if not bundle:
        raise ValueError('pedal has no compiled setup bundle')
    setup = directory / 'pedal-setup.bin'
    setup.write_bytes(bundle)
    if not any(
        (directory / 'cntrl/setups/bundle' / f'{i}.bin').is_file() for i in range(2)
    ):
        raise ValueError('pedal mirror is missing its compiled setup journals')
    for physical, digest in checksums.items():
        if sha256(client.download_state(physical)).hexdigest() != digest:
            raise ValueError(f'{physical}: storage changed during mirror capture')
    if storage_manifest(client, prefix) != manifest:
        raise ValueError('pedal storage listing changed during mirror capture')
    snapshot = client.request_data('CNTRL STATUS SNAPSHOT')
    if len(snapshot) != 111 or snapshot[:4] != b'CSS1':
        raise ValueError('pedal returned an unsupported Status snapshot')
    if client.current() != selected:
        raise ValueError('pedal setup selection changed during mirror capture')
    if client.request('CNTRL STATUS') != status:
        raise ValueError('pedal status changed during mirror capture')
    # Importing a bundle would replace the copied journals and selection.
    (directory / 'setup-source.sha256').write_text(sha256(bundle).hexdigest())
    (directory / 'status-snapshot.bin').write_bytes(snapshot)
    (directory / 'mirror.toml').write_text(
        tomlkit.dumps(dict(status=status, selected=selected, files=files))
    )
    return setup


def storage_manifest(
    client: transport.SetupClient, prefix: str
) -> dict[str, tuple[str, int]]:
    first = True

    def next_line() -> str:
        nonlocal first
        command = 'CNTRL STORAGE SCAN' if first else 'CNTRL STORAGE NEXT'
        first = False
        return client.request(command)

    mutable = ('setups/', 'history/', 'songs/', 'selection/', 'global/', 'languages/')
    manifest: dict[str, tuple[str, int]] = {}
    for entry in storage_info.read_storage_report(next_line).entries:
        if entry.directory or not entry.path.startswith('/cntrl/'):
            continue
        if entry.path.startswith('/cntrl/generations/'):
            if not prefix or not entry.path.startswith(prefix):
                continue
            logical = '/cntrl/' + entry.path[len(prefix) :]
        else:
            logical = entry.path
            if prefix and logical[7:].startswith(mutable):
                continue
        if '..' in Path(logical).parts:
            raise ValueError(f'{entry.path}: invalid mirror storage path')
        manifest[entry.path] = (logical, entry.size_bytes)
    return manifest
