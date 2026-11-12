"""Footprint experiments preserve production flags and read section byte counts."""

import pytest

from cntrl.maker.morningstar.mc.code_footprint import size_sections, variant_command


def test_section_report_uses_sizes_and_ignores_headings_and_totals() -> None:
    sections = size_sections(
        'firmware.elf :\nsection size addr\n'
        '.text.code 1234 1611661312\n.text.itcm 2048 0\n'
        '.fini 4 2048\nTotal 3286\n'
    )
    assert len(sections) == 3
    assert sections['.text.code'] == 1234
    assert sections['.text.itcm'] == 2048
    assert sections['.fini'] == 4


@pytest.mark.parametrize('optimization', ['-Os', '-Oz'])
@pytest.mark.parametrize('lto', [False, True])
def test_variants_keep_linker_and_feature_flags(optimization: str, lto: bool) -> None:
    command = [
        'arduino-cli',
        'compile',
        '--fqbn',
        'teensy:avr:teensy41:opt=osstd',
        '--build-property',
        'build.flags.ld=-Wl,--gc-sections -Tcustom.ld',
        '--build-property',
        'build.flags.cpp=-DCNTRL_PANIC=1',
        '/source/sketch',
    ]
    result = variant_command(command, optimization, lto)
    assert result[-1] == command[-1]
    assert command[-2] == 'build.flags.cpp=-DCNTRL_PANIC=1'
    assert 'build.flags.ld=-Wl,--gc-sections -Tcustom.ld' in result
    assert command[-2] in result
    assert ('opt=oslto' in result[3]) == lto
    flags = result[-2]
    assert flags.startswith(f'build.flags.optimize={optimization} --specs=nano.specs')
    assert ('-flto=auto' in flags) == lto
    assert ('-fno-fat-lto-objects' in flags) == lto


def test_diagnostic_link_is_explicit_and_keeps_its_map_beside_the_build() -> None:
    command = [
        'arduino-cli',
        'compile',
        '--fqbn',
        'teensy:avr:teensy41:opt=osstd',
        '--build-path',
        '/measurements/Os-lto/cntrl',
        '--build-property',
        'build.flags.ld=-Tcustom.ld',
        '/source/sketch',
    ]
    ordinary = variant_command(command, '-Os', True)
    assert not any('--noinhibit-exec' in a for a in ordinary)
    diagnostic = variant_command(command, '-Os', True, diagnostic=True)
    assert (
        'build.flags.ld=-Tcustom.ld -Wl,--noinhibit-exec '
        '"-Wl,-Map=/measurements/Os-lto/link.map"' in diagnostic
    )
