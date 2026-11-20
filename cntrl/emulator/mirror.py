"""Capture a pedal's active persistent files without recompiling its setups."""

from hashlib import sha256
from pathlib import Path
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
        directory = (
            storage or Path('.cntrl-emulator') / model
        ) / f'mirror-{uuid4().hex}'
        directory.mkdir(parents=True)
        setup = capture(client, directory, status)
    return model, setup, directory


def capture(client: transport.SetupClient, directory: Path, status: str) -> Path:
    """Normalize the active deployment root, preserving journal and asset bytes."""
    first = True
    selected = client.current()
    snapshot = client.request_data('CNTRL STATUS SNAPSHOT')
    if len(snapshot) != 111 or snapshot[:4] != b'CSS1':
        raise ValueError('pedal returned an unsupported Status snapshot')

    def next_line() -> str:
        nonlocal first
        command = 'CNTRL STORAGE SCAN' if first else 'CNTRL STORAGE NEXT'
        first = False
        return client.request(command)

    report = storage_info.read_storage_report(next_line)
    prefix = persistent_prefix(status)
    mutable = ('setups/', 'history/', 'songs/', 'selection/', 'global/', 'languages/')
    files: list[dict[str, object]] = []
    journals: dict[str, str] = {}
    for entry in report.entries:
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
        try:
            data = client.download_state(entry.path)
        except (OSError, ValueError) as error:
            raise ValueError(
                f'{entry.path}: cannot capture pedal file: {error}'
            ) from error
        if len(data) != entry.size_bytes:
            raise ValueError(f'{entry.path}: storage changed during mirror capture')
        path = directory / logical.lstrip('/')
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        files.append(dict(path=logical, sha256=sha256(data).hexdigest()))
        if logical[7:].startswith(mutable):
            journals[entry.path] = sha256(data).hexdigest()
    bundle = client.download_bundle()
    if not bundle:
        raise ValueError('pedal has no compiled setup bundle')
    setup = directory / 'pedal-setup.bin'
    setup.write_bytes(bundle)
    if not any(
        (directory / 'cntrl/setups/bundle' / f'{i}.bin').is_file() for i in range(2)
    ):
        raise ValueError('pedal mirror is missing its compiled setup journals')
    for path, digest in journals.items():
        if sha256(client.download_state(path)).hexdigest() != digest:
            raise ValueError(
                'pedal settings changed during mirror capture; capture again while idle'
            )
    if client.current() != selected:
        raise ValueError('pedal setup selection changed during mirror capture')
    if persistent_prefix(client.request('CNTRL STATUS')) != prefix:
        raise ValueError('pedal deployment changed during mirror capture')
    # Importing a bundle would replace the copied journals and selection.
    (directory / 'setup-source.sha256').write_text(sha256(bundle).hexdigest())
    (directory / 'mirror.toml').write_text(
        tomlkit.dumps(dict(status=status, selected=selected, files=files))
    )
    (directory / 'status-snapshot.bin').write_bytes(snapshot)
    return setup
