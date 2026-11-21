"""Use the pinned Teensy display library for SDK font qualification."""

from pathlib import Path

import pytest

from cntrl.maker.morningstar.mc.build_font_metrics import Options


@pytest.fixture(scope='session')
def sdk_font_source(pytestconfig: pytest.Config) -> Path:
    source = Options().source / 'st7735_t3_font_ComicSansMS.c'
    if source.is_file():
        return source
    message = (
        'Pinned Teensy font source is missing. Run cntrl build with the '
        'Morningstar MC target to provision the SDK.'
    )
    if pytestconfig.getoption('--require-morningstar-sdk'):
        pytest.fail(message)
    pytest.skip(message)
