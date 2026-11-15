"""Read and retain the exact source bytes used by one compilation."""

from pathlib import Path


def read(path: Path, snapshots: dict[Path, bytes] | None = None) -> bytes:
    resolved = path.resolve()
    if snapshots is None:
        return path.read_bytes()
    if resolved not in snapshots:
        snapshots[resolved] = path.read_bytes()
    return snapshots[resolved]
