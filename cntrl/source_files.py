"""Read and retain the exact source bytes used by one compilation."""

from pathlib import Path


def write_if_changed(path: Path, data: bytes) -> None:
    """Keep the timestamp of an unchanged generated dependency."""
    if not path.is_file() or path.read_bytes() != data:
        path.write_bytes(data)


def read(path: Path, snapshots: dict[Path, bytes] | None = None) -> bytes:
    resolved = path.resolve()
    if snapshots is None:
        return path.read_bytes()
    if resolved not in snapshots:
        snapshots[resolved] = path.read_bytes()
    return snapshots[resolved]
