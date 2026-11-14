import subprocess
from pathlib import Path

import pytest

from cntrl.maker.morningstar.mc import toolchain


@pytest.mark.parametrize('code_bytes', [192504, 192508])
def test_itcm_budget_counts_unwind_bytes_at_the_four_kib_boundary(
    tmp_path: Path, code_bytes: int
) -> None:
    root = Path(__file__).parents[1] / 'firmware'
    assembler = toolchain.arm_tool(root, 'as')
    linker = toolchain.arm_tool(root, 'ld')
    if not assembler.is_file() or not linker.is_file():
        pytest.skip('pinned ARM toolchain is not provisioned')
    object_file = tmp_path / 'budget.o'
    subprocess.run(
        [
            str(assembler),
            '--defsym',
            f'memory_fixture_bytes={code_bytes}',
            str(root / 'test_memory_budget.s'),
            '-o',
            str(object_file),
        ],
        capture_output=True,
        check=True,
        timeout=30,
    )
    result = subprocess.run(
        [
            str(linker),
            '-T',
            str(root / 'test_memory_budget.ld'),
            '-T',
            str(root / 'memory.ld'),
            str(object_file),
            '-o',
            str(tmp_path / 'budget.elf'),
        ],
        capture_output=True,
        text=True,
        check=False,
        timeout=30,
    )
    # Eight unwind bytes leave exactly 4096 bytes spare in the passing case;
    # the second image fits without those bytes but must fail when counted.
    if code_bytes == 192504:
        assert result.returncode == 0, result.stderr
    else:
        assert result.returncode != 0
        assert 'ITCM budget exceeded' in result.stderr
