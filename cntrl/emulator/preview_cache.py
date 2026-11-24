"""Reuse generated setup preview files after resolving the current inputs."""

import sys
from hashlib import sha256
from importlib.metadata import distributions
from pathlib import Path

import tomlkit
from pydantic import BaseModel, ValidationError
from tomlkit.exceptions import ParseError

from ..patch_chooser import data_files, setup_compiler, source_archive


class PreviewRecord(BaseModel, frozen=True):
    key: str
    inputs: dict[str, str]
    outputs: dict[str, str]

    def matches(self, key: str, output: Path, trace: data_files.Trace) -> bool:
        if self.key != key or output.name not in self.outputs:
            return False
        try:
            for name, digest in self.inputs.items():
                path = Path(name)
                data = trace.files.get(path)
                if data is None:
                    data = path.read_bytes()
                if sha256(data).hexdigest() != digest:
                    return False
            return output_digests(output) == self.outputs
        except OSError:
            return False


def key_for(inputs: setup_compiler.SetupInputs, trace: data_files.Trace) -> str:
    # Hash the resolved recipe as well as assets loaded during compilation.
    # Resolving it again catches new defaults and changed reference destinations.
    packages = sorted((d.name, d.version) for d in distributions() if d.name)
    return sha256(
        repr(
            (
                source_archive.compiler_identity(),
                sys.version,
                packages,
                inputs.model_dump(),
                trace.setting_overrides,
                {str(p): sha256(b).hexdigest() for p, b in trace.files.items()},
            )
        ).encode()
    ).hexdigest()


def read_record(path: Path) -> PreviewRecord | None:
    try:
        return PreviewRecord.model_validate(tomlkit.parse(path.read_text()).unwrap())
    except (OSError, UnicodeError, ParseError, ValidationError):
        return None


def output_digests(output: Path) -> dict[str, str]:
    directory = output.parent
    return {
        p.relative_to(directory).as_posix(): sha256(p.read_bytes()).hexdigest()
        for p in (
            output,
            *(directory / 'fonts').glob('*.cfont'),
            *(directory / 'movies').glob('*.cnsa'),
        )
    }
