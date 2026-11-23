# Standalone Doom demo for the MC6 Pro

The demo boots directly into Freedoom Phase 1 E1M1 and renders a 160×128
image on the centre display. The firmware runs silently, reads game data
from flash and uses USB Serial for diagnostics. It has no MIDI runtime
or writable game storage.
[DMC100 Doom](https://github.com/ArturBieniek4/DMC100-Doom)

| Switch | Action |
| --- | --- |
| A | Turn left |
| B | Move forward |
| C | Turn right |
| D | Fire |
| E | Move backward |
| F | Use doors and switches |

## Reproduce the asset check

1. Extract the [DMC100 source at `0cdc78e`](https://github.com/ArturBieniek4/DMC100-Doom/tree/0cdc78eb66cdddacc43112d5f79c258c4a5440b7)
   into a scratch directory. Extract [DoomGeneric at `dcb7a8d`](https://github.com/ozkl/doomgeneric/tree/dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284)
   under its `oracle/src/doomgeneric-repo`. Keep the upstream notices.
2. Put [Freedoom 0.13.0](https://github.com/freedoom/freedoom/releases/tag/v0.13.0)
   next to the DMC100 checkout. The converter verifies the release archive's
   SHA-256 before extracting the Phase 1 IWAD.
3. On macOS, apply [the symbol parsing patch](dmc100-macos.patch) to the
   DMC100 source with `git apply`. The converter invokes Clang, `llvm-nm` and
   `llvm-dwarfdump`. If the LLVM tools are outside `PATH`, pass their
   absolute paths using the upstream `NM` and `DWARFDUMP` settings.
4. From the DMC100 source directory, run the upstream converter and checks.

   ```sh
   python3 -I tools/conv/__main__.py --zip ../freedoom-0.13.0.zip --packs E1M1 all
   python3 -I tools/dmccheck.py build/packs/E1M1
   python3 -I tools/dmcview.py --zip ../freedoom-0.13.0.zip build/packs/E1M1
   python3 -I tools/conv/__main__.py --zip ../freedoom-0.13.0.zip --packs E1M1 --out build/repeat packs
   cmp build/packs/E1M1/DOOM.DAT build/repeat/E1M1/DOOM.DAT
   ```

The E1M1 pack is 2,802,000 bytes, including shared graphics and sound data.
The map accounts for 120,592 bytes. Both validators passed on macOS and the
two packs matched byte for byte. The pack's SHA-256 is
`6b25a4f1adb6f1f1f5f42226c272b93ccff4f356ac8d7034cf83712b2da70c08`.
The generated files and downloaded upstream archives are disposable
qualification inputs and outputs.

The MC6 Pro linker reserves 6,912 KiB for the relocated application. The
checked-in CNTRL application image used 2,614,272 bytes when this comparison
was made. Approximately 1.58 MiB would remain after adding the measured
pack, before the engine, any extra buffers represented in flash, alignment
and other build overhead. Use a fresh linker map to decide the real budget.
[Linker region](../boot_switch.py) · [Packaged application manifest](../distribution/manifest.toml)

## Build and check the demo

With the E1M1 pack and generated sources in the pinned DMC100 checkout,
apply [the arena patch](dmc100-arena.patch) there with `git apply`. The
patch allows this build to size the level arena at 64 KiB. The engine's
native overlay tables link directly into the standalone executable.

Run this from the repository root with absolute paths for the DMC100
checkout and output directory. Set `TOOLS` to the installed Teensy SDK
directory if it lives outside this worktree. The build uses the patched
Teensy 1.62.0 core and the validated MC6 Pro FlexSPI hardware configuration.
[Toolchain setup](../firmware/README.md#historical-diagnostic-build)

```sh
make -C cntrl/maker/morningstar/mc/doom \
  DMC=/absolute/path/to/DMC100-Doom OUT=/absolute/path/to/build \
  TOOLS=/absolute/path/to/cntrl/cntrl/maker/morningstar/mc/firmware/.tools \
  check firmware
```

`check` runs the same engine, storage reader, input mapping and pixel
conversion on the host. It checks startup, rendering through tic 64, all six
logical pedal inputs, and release of forward and use. `firmware` builds an ARM
ELF and an Intel HEX in the output directory's `elf/` subdirectory, then
compares the decoded HEX bytes against the ELF flash bytes. The build disables
the Teensy Loader and encryption hooks.

The first build produced a 4,345,856-byte flash image and passed the MC6 Pro
upload helper's structural image validation. The linker reports 125,568 bytes
for RAM1 stack and local variables and 511,840 bytes for the RAM2 heap. The
demo's boot, display, switches and frame rate on the pedal still need live
verification.

## Launch from CNTRL

Compile the setup against the Doom-enabled target. In the default utility
rotation, **Doom** appears immediately before the empty page. Setups with
explicit page rotations can add a page of type `doom` to their tools region.
Cycle to **C Doom**, then press C to start the game. The boot selector
consumes the request before entering Doom, so a power cycle returns to the
ordinary CNTRL startup. The launcher requires CNTRL to be the default
firmware.

Use the same DMC100 inputs as above and replace `firmware` with
`relocated` to build the separate Doom application at `0x60390000`.
From the repository root, export the two separately linked applications
and then use the standard MC6 Pro flash workflow.

```sh
cntrl build --target-dir cntrl/maker/morningstar/mc \
  --output /absolute/path/to/doom-export \
  --doom-image /absolute/path/to/build/relocated/elf/doom_demo.ino.hex
cntrl flash --controller mc6-pro \
  --resources /absolute/path/to/doom-export/distribution
```

The export contains the CNTRL application, the relocated Doom application
and the boot hook, with image bounds checked against the reserved flash
region. The installer uses the existing MC6 Pro backup and restore workflow.
[MC6 Pro upload and recovery](../firmware/upload-helper.md)

## Standalone installation and source

The demo occupies the application flash from `0x60000000`. Installing it
would replace the current boot image. Power cycling restarts the demo.
Returning to CNTRL requires reinstalling the previous verified image, so
keep that image and a way to enter PROGRAM mode available for hardware
bring-up. [MC6 Pro upload and recovery](../firmware/upload-helper.md)

The demo adapter and DMC100 Doom use GPL-2.0-or-later. The standalone
build uses the open Teensy core and Freedoom assets. Keep their respective
notices and the corresponding source with any distributed build. The
firmware image and generated game pack are local build outputs.
