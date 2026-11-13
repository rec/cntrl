# Morningstar emulator profiles

The layouts reference `../mc.toml`; generic emulator code does not know about
Morningstar hardware. `mc6-pro.toml` places six controls and three displays.
`mc8-pro.toml` places eight controls and four displays and is explicitly
provisional. Its four 160×128 virtual viewports exercise cntrl without asserting
the physical MC8 Pro display specifications.

Switch rows and display placement follow the manufacturers' front-panel diagrams:
[MC6 Pro manual](https://help.morningstar.io/en/article/mc6-pro-user-manual-psevjy/)
and [MC8 Pro manual](https://help.morningstar.io/en/article/mc8-pro-user-manual-1yqqr1r/).
The canvas coordinates are schematic, not measured enclosure dimensions.

An execution target is separate from the hardware catalogue and screen layout.
Export a catalogue-bound virtual target from the checked-in target description.
The exporter copies the neutral font sources and prepares the bundled UI faces;
no connected pedal or ARM firmware build is required:

```sh
uv run --no-sync python -m cntrl.maker.morningstar.mc.emulator.profiles --target cntrl/maker/morningstar/mc/patch-switcher-target.json --controller mc6_pro --output build/virtual-mc6
uv run --no-sync python -m cntrl.maker.morningstar.mc.emulator.profiles --target cntrl/maker/morningstar/mc/patch-switcher-target.json --controller mc8_pro --output build/virtual-mc8
```

The exported targets use catalogue control IDs `a` through `f`/`h`, and display
IDs `lcd-1` through `lcd-3`/`lcd-4`. Compile the desired setup against that
exported `target.toml`, not the original physical target. The normal compiler
continues to enforce its format and resource constraints.

The source MC6 Pro target supplies A–F action bindings and usable display
viewports. The catalogue's 200×200 declaration is not used to override the
physical adapter's 160×128 execution viewports. It remains a hardware-data
discrepancy requiring confirmation. The virtual MC8 Pro retains A–F actions and
assigns G to previous page, H to next page; this does not increase the existing
five configurable patch-slot limit. Physical firmware handoff and unsupported
diagnostic/USB-keyboard functions are excluded from these desktop targets.
Animations use one stream per virtual display, so compile their assets for the
exported target rather than copying the physical adapter's two-stream movies.

Launch either layout with its matching target and compiled CSTB setup bundle:

```sh
uv run --no-sync cntrl emulate --layout cntrl/maker/morningstar/mc/emulator/mc8-pro.toml --target build/virtual-mc8/target.toml --setup setup.bin --storage build/mc8-session --source-root .
```

Session directories are independent. Switching profiles does not overwrite the
other controller's data. See the [generic emulator guide](../../../../../doc/emulator.md)
for virtual input, replay and host MIDI use. Neither profile establishes that
cntrl firmware boots or works electrically on an MC8 Pro.
