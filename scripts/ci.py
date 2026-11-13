"""Manually dispatched host qualification, logs and reproducible provenance."""

import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
from importlib.metadata import distribution
from pathlib import Path
from tempfile import TemporaryDirectory

import freetype
import tomlkit
import tyro
from PIL import features
from pydantic import BaseModel, Field

from scripts.qualify_portable import Qualification, qualify


class QualificationRun(BaseModel, frozen=True):
    profile: str = os.environ.get('CNTRL_CI_PROFILE', 'core')
    target_dir: Path | None = Field(
        default_factory=lambda: (
            Path(os.environ['CNTRL_CI_TARGET_DIR'])
            if os.environ.get('CNTRL_CI_TARGET_DIR')
            else None
        )
    )
    output: Path = Path('reports')


def run(options: QualificationRun) -> None:
    root = Path(__file__).resolve().parents[1]
    output = options.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if options.profile not in ('core', 'full'):
        raise ValueError('profile must be core or full')
    if options.profile == 'full' and options.target_dir is None:
        raise ValueError('full qualification requires --target-dir')
    provenance = {
        'revision': subprocess.check_output(
            ['git', 'rev-parse', 'HEAD'], text=True
        ).strip(),
        'dependency_revision': dependency_revision('reccy'),
        'os': platform.platform(),
        'architecture': platform.machine(),
        'python': sys.version,
        'profile': options.profile,
        'compiler': subprocess.check_output(
            ['cl'] if os.name == 'nt' else [os.environ.get('CXX', 'c++'), '--version'],
            stderr=subprocess.STDOUT,
            text=True,
        ).strip()
        if os.name != 'nt'
        else os.environ.get('VCToolsVersion', ''),
        'lock_sha256': hashlib.sha256((root / 'uv.lock').read_bytes()).hexdigest(),
        'freetype': '.'.join(str(n) for n in freetype.version()),
        'freetype_raster_patch_sha256': hashlib.sha256(
            (root / 'cntrl/fonts/freetype-mono64.patch').read_bytes()
        ).hexdigest(),
        'pillow_freetype': features.version('freetype2') or '',
    }
    (output / 'provenance.toml').write_text(tomlkit.dumps(provenance))
    execute([sys.executable, 'scripts/native_tests.py'], output / 'native.log', root)
    if options.target_dir is not None and options.profile == 'full':
        execute(
            [
                sys.executable,
                '-m',
                'cntrl.patch_switcher.cli',
                'build',
                '--target-dir',
                str(options.target_dir.resolve()),
                '--source-root',
                str(root),
                '--output',
                str(output / 'export with spaces'),
            ],
            output / 'build.log',
            root,
        )
    commands = {
        'pytest': [
            sys.executable,
            '-m',
            'pytest',
            '-q',
            '-o',
            'faulthandler_timeout=90',
            '-o',
            'junit_logging=all',
            f'--basetemp={output / "pytest-temp"}',
            *(['test'] if options.profile == 'core' else []),
            f'--junitxml={output / "pytest.xml"}',
        ],
        'lint': [
            'uv',
            'run',
            '--no-sync',
            'ruff',
            'check',
            '--select',
            'B,E,F,I',
            'cntrl',
            'test',
            'scripts',
        ],
        'typing': ['uv', 'run', '--no-sync', 'ty', 'check', 'cntrl'],
    }
    for name, command in commands.items():
        execute(command, output / f'{name}.log', root)
    wheel_smoke(root, output)
    if options.target_dir is not None and options.profile == 'full':
        qualify(
            Qualification(
                export=output / 'export with spaces',
                output=output / 'portable',
                source_root=root,
            )
        )
    (output / 'result.toml').write_text(tomlkit.dumps({'success': True}))


def execute(
    command: list[str], log: Path, cwd: Path, environment: dict[str, str] | None = None
) -> None:
    with log.open('w') as stream:
        result = subprocess.run(
            command,
            cwd=cwd,
            env=environment,
            stdout=stream,
            stderr=subprocess.STDOUT,
            timeout=1200,
            check=False,
        )
    if result.returncode:
        print(log.read_text(errors='replace')[-24000:], file=sys.stderr)
        raise subprocess.CalledProcessError(result.returncode, command)


def dependency_revision(name: str) -> str:
    metadata = distribution(name).read_text('direct_url.json')
    if metadata is None:
        raise ValueError(f'{name}: qualification requires a pinned Git installation')
    record = json.loads(metadata)
    if (
        not isinstance(record, dict)
        or not isinstance(vcs := record.get('vcs_info'), dict)
        or not isinstance(revision := vcs.get('commit_id'), str)
    ):
        raise ValueError(f'{name}: installed Git revision metadata is missing')
    return revision


def wheel_smoke(root: Path, output: Path) -> None:
    wheels = output / 'wheels'
    wheels.mkdir(exist_ok=True)
    execute(
        ['uv', 'build', '--wheel', str(root), '--out-dir', str(wheels)],
        output / 'wheel-cntrl.log',
        root,
    )
    requirements = output / 'requirements.txt'
    execute(
        [
            'uv',
            'export',
            '--frozen',
            '--no-dev',
            '--no-emit-project',
            '--output-file',
            str(requirements),
        ],
        output / 'export-lock.log',
        root,
    )
    with TemporaryDirectory(prefix='cntrl-installed-') as temporary:
        directory = Path(temporary)
        execute(
            ['uv', 'venv', '--python', sys.executable, str(directory / 'venv')],
            output / 'venv.log',
            root,
        )
        python = directory / (
            'venv/Scripts/python.exe' if os.name == 'nt' else 'venv/bin/python'
        )
        execute(
            [
                'uv',
                'pip',
                'install',
                '--python',
                str(python),
                '-r',
                str(requirements),
                *(str(p) for p in wheels.glob('*.whl')),
            ],
            output / 'install-wheel.log',
            directory,
        )
        fixtures = directory / 'fixtures'
        shutil.copytree(root / 'firmware/test', fixtures)
        environment = dict(os.environ)
        environment.pop('PYTHONPATH', None)
        execute(
            [str(python), '-m', 'cntrl.patch_switcher.cli', '--help'],
            output / 'installed-help.log',
            directory,
            environment,
        )
        execute(
            [
                str(python),
                '-m',
                'cntrl.fonts.provision',
                '--build-directory',
                str(root / 'build/freetype'),
            ],
            output / 'installed-rasterizer.log',
            directory,
            environment,
        )
        execute(
            [
                str(python),
                str(root / 'scripts/installed_smoke.py'),
                '--fixtures',
                str(fixtures),
                '--output',
                str(output / 'installed artifacts'),
            ],
            output / 'installed-smoke.log',
            directory,
            environment,
        )


if __name__ == '__main__':
    run(tyro.cli(QualificationRun))
