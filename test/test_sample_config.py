from typing import TYPE_CHECKING

from cntrl.patch_chooser import models

if TYPE_CHECKING:
    from reccy.pytest_plugin import SampleConfigRegression

pytest_plugins = ['reccy.pytest_plugin']


class ExamplePageSettings(models.PageSettings, frozen=True):
    contact_style: models.ContactStyle | None = models.ContactStyle(
        image='contact.png',
        foreground_color='white',
        background_color='navy',
        font_name='Arimo',
    )
    arpeggiator: models.ArpeggiatorSettings | None = models.ArpeggiatorSettings(
        inputs=['usb'], outputs=['usb']
    )
    looper: models.LooperSettings | None = models.LooperSettings(
        inputs=['usb'], outputs=['usb']
    )


class ExampleSetup(models.Setup, frozen=True):
    page_settings: models.PageSettings = ExamplePageSettings()
    regions: dict[str, models.PageRegion] = {
        'performance': models.PageRegion(pages=['patch-chooser']),
        'tools': models.PageRegion(
            pages=[n for n in models.Setup().pages if n != 'patch-chooser']
        ),
    }
    layout: models.PageLayout | None = models.PageLayout(
        order=['performance', 'tools'], focus='tools'
    )
    style: models.Style | None = models.Style(horizontal_scale=1.0)
    displays: models.Displays = models.Displays(
        next=models.Display(style=models.Style(horizontal_scale=1.0)),
        sent=models.Display(style=models.Style(horizontal_scale=1.0)),
        undo=models.Display(style=models.Style(horizontal_scale=1.0)),
    )
    patch_views: dict[str, models.PatchView] = {
        'patch': models.PatchView(style=models.Style(horizontal_scale=1.0))
    }


def test_sample_config(sample_config_regression: 'SampleConfigRegression') -> None:
    sample_config_regression(ExampleSetup)
