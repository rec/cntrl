"""Capture a pedal's active persistent files without recompiling its setups."""

from hashlib import sha256
from pathlib import Path
from tempfile import TemporaryDirectory
from uuid import uuid4

import tomlkit

from ..patch_chooser import binary_format, storage_info, transport
from ..patch_chooser.backups import persistent_prefix
from .errors import EmulatorError, emulator_files


def download(
    port: Path, storage: Path | None, controller: str | None
) -> tuple[str, Path, Path]:
    with transport.SerialConnection(port, timeout=5) as connection:
        client = transport.SetupClient(connection)
        status = client.request('CNTRL STATUS')
        fields = dict(v.split('=', 1) for v in status.split() if '=' in v)
        model = fields.get('model')
        if not model:
            raise EmulatorError('pedal status does not identify its controller model')
        if controller is not None and controller != model:
            raise EmulatorError(
                f'pedal model {model} does not match --controller {controller}'
            )
        if fields.get('persistent_format') != str(binary_format.DEVELOPMENT_FORMAT):
            raise EmulatorError(
                'pedal persistent format differs from this emulator; '
                'update firmware first'
            )
        root = storage or Path('.cntrl-emulator') / model
        with emulator_files():
            root.mkdir(parents=True, exist_ok=True)
            staging = TemporaryDirectory(prefix='.mirror-', dir=root)
        directory = root / f'mirror-{uuid4().hex}'
        with staging as temporary:
            draft = Path(temporary) / 'capture'
            with emulator_files():
                draft.mkdir()
            setup = capture(client, draft, status)
            with emulator_files():
                draft.rename(directory)
    return model, directory / setup.name, directory


def capture(client: transport.SetupClient, directory: Path, status: str) -> Path:
    """Normalize the active deployment root, preserving journal and asset bytes."""
    generation = storage_generation(client)
    selected = client.current()
    prefix = persistent_prefix(status)
    manifest = storage_manifest(client, prefix)
    files: list[dict[str, object]] = []
    checksums: dict[str, str] = {}
    for physical, (logical, size) in manifest.items():
        try:
            data = client.download_state(physical)
        except transport.USB_ERRORS as error:
            raise EmulatorError(
                f'{physical}: cannot capture pedal file: {error}'
            ) from error
        if len(data) != size:
            raise EmulatorError(f'{physical}: storage changed during mirror capture')
        path = directory / logical.lstrip('/')
        with emulator_files():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        checksums[physical] = sha256(data).hexdigest()
        files.append(dict(path=logical, sha256=checksums[physical]))
    bundle = client.download_bundle()
    if not bundle:
        raise EmulatorError('pedal has no compiled setup bundle')
    setup = directory / 'pedal-setup.bin'
    with emulator_files():
        setup.write_bytes(bundle)
        has_journals = any(
            (directory / 'cntrl/setups/bundle' / f'{i}.bin').is_file() for i in range(2)
        )
    if not has_journals:
        raise EmulatorError('pedal mirror is missing its compiled setup journals')
    for physical, digest in checksums.items():
        if sha256(client.download_state(physical)).hexdigest() != digest:
            raise EmulatorError(f'{physical}: storage changed during mirror capture')
    if storage_manifest(client, prefix) != manifest:
        raise EmulatorError('pedal storage listing changed during mirror capture')
    snapshot = client.request_data('CNTRL STATUS SNAPSHOT')
    if (snapshot[:4], len(snapshot)) not in {(b'CSS1', 111), (b'CSS2', 108)}:
        raise EmulatorError('pedal returned an unsupported Status snapshot')
    if client.current() != selected:
        raise EmulatorError('pedal setup selection changed during mirror capture')
    if stable_status(client.request('CNTRL STATUS')) != stable_status(status):
        raise EmulatorError('pedal status changed during mirror capture')
    if storage_generation(client) != generation:
        raise EmulatorError('pedal storage generation changed during mirror capture')
    # Importing a bundle would replace the copied journals and selection.
    metadata = tomlkit.dumps(dict(status=status, selected=selected, files=files))
    with emulator_files():
        (directory / 'setup-source.sha256').write_text(sha256(bundle).hexdigest())
        (directory / 'status-snapshot.bin').write_bytes(snapshot)
        (directory / 'mirror.toml').write_text(metadata)
    return setup


def stable_status(status: str) -> list[str]:
    # Workload readings change continuously during the capture.
    return [v for v in status.split() if not v.startswith('cpu_')]


def storage_generation(client: transport.SetupClient) -> str:
    try:
        response = client.request('CNTRL MIRROR GENERATION')
    except transport.UsbReplyError as error:
        if str(error) == 'CNTRL ERROR invalid-command':
            raise EmulatorError(
                'pedal firmware lacks the mirror storage check; update firmware first'
            ) from error
        raise
    fields = response.split()
    if (
        len(fields) != 3
        or fields[:2] != ['CNTRL', 'MIRROR']
        or not fields[2].isdecimal()
    ):
        raise EmulatorError(f'invalid pedal storage generation response: {response}')
    return fields[2]


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
            raise EmulatorError(f'{entry.path}: invalid mirror storage path')
        manifest[entry.path] = (logical, entry.size_bytes)
    return manifest
