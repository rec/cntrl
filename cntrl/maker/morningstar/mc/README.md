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

Independent MC6 Pro bring-up established three 160 x 128 RGB565 drawing areas.
The catalogue still carries the earlier user-supplied 200 x 200 declarations;
that is a known data discrepancy, not the verified display geometry. The firmware
target uses 160 x 128. This documentation update does not change catalogue data.
MC4 Pro and MC8 Pro dimensions, palette counts and DIN contact counts remain
unspecified in the catalogue; missing fields are not verified specifications.
External expression pedals and switches are not built-in controls. No firmware
restrictions, presets, calibration, or live device state are represented.

## Current application and qualification, 2026-10-25

The independent patch-switcher is running on the photographed MC6 Pro, not just
on host test hardware. Latest deployed source: `48b2693`, with version 9 data.
The selected `pman` setup contains 256 patches in 15 categories, routed on
channel 1 to DIN. It was backed up, recompiled and restored after the firmware
update; read-back matched the prepared setup exactly. No empty setup was
substituted. The USB MIDI bridge remains off in this setup.

Boot, USB serial, software bootloader entry/upload/reboot, all six buttons,
display text and fast redraws have been exercised. USB MIDI discovery shows
two inputs and six outputs with useful names; USB-to-DIN output passed exact
short-message and large-SysEx capture through a CME U6MIDI Pro. The two physical
inputs and the other five outputs still need individual loopback qualification.
Physical SD power-loss recovery and complete release qualification are not proven.

See [deployment details](firmware/patch-switcher.md),
[USB MIDI qualification](usb-midi-interface-plan.md), and
[splash behavior](splash/README.md).

## Build and historical analysis

See the [firmware build and inspection instructions](firmware/README.md),
[patch-switcher build](firmware/patch-switcher.md),
[upload-only helper](firmware/upload-helper.md),
[firmware analysis](firmware-analysis.md) and
[pin/peripheral map](mc6-pro-pin-map.md).

Run from the repository root:

```bash
make -C cntrl/maker/morningstar/mc/firmware test inspect-patch-switcher
uv run --frozen pytest
```

The separate `inspect` target builds the historical USB/button/display diagnostic,
not the patch-switcher. Neither build flashes anything. The helper uploads the
fixed `firmware/build/patch-switcher.hex` path only. Keep an existing helper
running between uploads; restart it when its source or fixed paths change. An
address-in-use error can mean it is already serving, not that its socket is stale.
