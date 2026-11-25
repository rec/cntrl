# Standalone Doom demo for the MC6 Pro

The demo boots directly into Freedoom Phase 1 E1M1 and renders a 160×128
image on the centre display. The firmware runs silently, reads game data
from the pedal's microSD card and uses USB Serial for diagnostics. It has no MIDI runtime
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

The MC6 Pro linker reserves 6,912 KiB for the relocated applications.
The Doom engine has a 256 KiB slot at the end of that region. The generated
4 MiB asset image lives on the card at `/cntrl/doom/e1m1.img`. Doom reads
it in place, using a 64 KiB cache for small reads.
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
  check assets firmware
```

`check` runs the same engine, storage reader, input mapping and pixel
conversion on the host. It checks startup, rendering through tic 64, all six
logical pedal inputs, and release of forward and use. `firmware` builds an ARM
ELF and an Intel HEX in the output directory's `elf/` subdirectory, then
compares the decoded HEX bytes against the ELF flash bytes. The build disables
the Teensy Loader and encryption hooks.

Use a fresh output directory when moving from the old build that embedded
the asset image in firmware, so the generated assembly from that build
stays out of the new sketch.

## Launch from CNTRL

Compile the setup against the Doom-enabled target. In the default utility
rotation ends with **Pong**, **Doom**, and **Memon**. Setups with
explicit page rotations can add a page of type `doom` to their tools region.
Cycle to the illustrated **Doom** page, then press C to start the game.
Tap F to open doors and activate switches. Hold F for two seconds to
restart into the ordinary CNTRL startup without cycling power. A power
cycle also returns to CNTRL because the boot selector consumes the
one-time Doom request. The launcher requires CNTRL to be the default
firmware.

Use the same DMC100 inputs as above and replace `firmware` with
`relocated` to build the separate Doom application at `0x60780000`.
From the repository root, export the two separately linked applications
and then use the standard MC6 Pro flash workflow.

```sh
cntrl build --target-dir cntrl/maker/morningstar/mc \
  --output /absolute/path/to/doom-export \
  --doom-image /absolute/path/to/build/relocated/elf/doom_demo.ino.hex
cntrl flash --controller mc6-pro \
  --resources /absolute/path/to/doom-export/distribution \
  --doom-assets /absolute/path/to/build/e1m1.img
```

The export contains the CNTRL application, the relocated Doom application
and the boot hook, with image bounds checked against the reserved flash
region. After the usual backup, installation and restore, the installer
copies the asset to the pedal's microSD over USB and reads it back to verify
the copy. The pedal checks that the asset is present before launching Doom.
[MC6 Pro upload and recovery](../firmware/upload-helper.md)

## Play in the emulator

With the same prepared DMC100 checkout, run `make` as above with the
`host` target. This builds `doom-host` and places `e1m1.img` beside it in
the output directory. When built in the default `doom/build` directory,
`cntrl emulate simple-setup.toml --controller mc6-pro` finds the game
and includes Doom in the default utility rotation. For a custom build
directory, pass `--doom-host /absolute/path/to/doom-host`.

Press C on the Doom launcher to switch to the game on the center
display. The same A–F controls work by mouse or keyboard. Tap F to use
a door or switch, or hold F for two seconds to restart into CNTRL.

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
