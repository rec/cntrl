from typing import TYPE_CHECKING

from cntrl.patch_switcher import models

if TYPE_CHECKING:
    from reccy.pytest_plugin import SampleConfigRegression

pytest_plugins = ['reccy.pytest_plugin']


class ExampleSetup(models.Setup, frozen=True):
    style: models.Style | None = models.Style(horizontal_scale=1.0)
    windows: models.Windows = models.Windows(
        next=models.Window(style=models.Style(horizontal_scale=1.0)),
        sent=models.Window(style=models.Style(horizontal_scale=1.0)),
        undo=models.Window(style=models.Style(horizontal_scale=1.0)),
    )
    pages: dict[str, models.Page] = {
        'patch': models.Page(style=models.Style(horizontal_scale=1.0))
    }


def test_sample_config(sample_config_regression: 'SampleConfigRegression') -> None:
    sample_config_regression(ExampleSetup)
