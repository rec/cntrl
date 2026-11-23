# MC6 Pro Doom engine qualification

DMC100 Doom is the first engine candidate. It already supports single-map
Freedoom packs and renders the 3D view at 160 logical columns. Its reported
32 KiB RAM figure applies to the upstream build and its storage and overlay
scheme. The MC6 Pro port still needs its own memory and timing measurements.
[DMC100 Doom](https://github.com/ArturBieniek4/DMC100-Doom)

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

## Integration decision

DMC100 Doom's engine is GPL-2.0-or-later. CNTRL's first-party licensing is
still open in the issue inventory. Decide whether to use a standalone GPL
demo build or approve GPL-compatible licensing for a combined CNTRL build
before linking the engine into CNTRL. A standalone demo would return to
normal controller operation by booting CNTRL again. Returning directly to
the previous page requires in-process integration.
[Open licensing issue](../../../../../plan/issues.md)
