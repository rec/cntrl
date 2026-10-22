# Morningstar MC

All Morningstar-specific code, tests, data, firmware and analysis live here.
The hardware catalogue is [mc.toml](mc.toml):

```python
from importlib.resources import files

from cntrl.catalogue import load_catalogue

catalogue = load_catalogue(files('cntrl.maker.morningstar.mc').joinpath('mc.toml'))
controller = catalogue['mc6_pro']
```

| Model key | Buttons | Displays | Wired ports |
| --- | ---: | --- | ---: |
| `mc3` | 3 | 1 character LCD, 20 x 2 | 7 |
| `mc6_mkii` | 6 | 1 character LCD, 24 x 2 | 5 |
| `mc8` | 8 | 1 character LCD, 40 x 4 | 7 |
| `mc4_pro` | 4 | 1 colour LCD | 9 |
| `mc6_pro` | 6 | 3 colour LCDs | 12 |
| `mc8_pro` | 8 | 4 colour LCDs | 12 |

Sources are the manufacturer's specifications for
[MC3](https://www.morningstar.io/mc3),
[MC6 MkII](https://www.morningstar.io/mc6-mkii),
[MC8](https://www.morningstar.io/mc8-midi-controller),
[MC4 Pro](https://www.morningstar.io/mc4-pro),
[MC6 Pro](https://www.morningstar.io/mc6-pro), and
[MC8 Pro](https://www.morningstar.io/mc8-pro).
Further detail comes from the manuals for
[MC3](https://help.morningstar.io/en/article/mc3-user-manual-19hvw2w/),
[MC4 Pro](https://help.morningstar.io/en/article/mc4-pro-user-manual-1p5o2cp/),
[MC6 Pro](https://help.morningstar.io/en/article/mc6-pro-user-manual-psevjy/), and
[MC8 Pro](https://help.morningstar.io/en/article/mc8-pro-user-manual-1yqqr1r/).
The MC3's third and fourth TRS MIDI outputs have DIP-selectable wiring.
MC4 Pro has separate rear and side TRS MIDI inputs and two different
configurable-port jack sizes.

MC6 Pro uses the user-supplied 200 x 200 display dimensions. MC4 Pro and MC8 Pro
dimensions and all Pro palette sizes remain unknown, with TODOs in the data.
Physical DIN contact counts also remain unknown pending confirmation.
External expression pedals and switches are not built-in controls. No firmware
restrictions, presets, calibration, or live device state are represented.

## Experimental firmware and analysis

See the [firmware build and inspection instructions](firmware/README.md),
[patch-switcher build](firmware/patch-switcher.md),
[upload-only helper](firmware/upload-helper.md),
[firmware analysis](firmware-analysis.md) and
[pin/peripheral map](mc6-pro-pin-map.md).

Run from the repository root:

```bash
make -C cntrl/maker/morningstar/mc/firmware test inspect
uv run --frozen pytest
```

After this relocation, stop any old upload helper between uploads and restart
it using the module command in its updated instructions. An already-running
helper retains its old image and socket paths. Its original socket is left
undisturbed until that process closes it; it is not used by the relocated helper.
