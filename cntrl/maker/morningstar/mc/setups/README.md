# Saved pedal setups

These are the three saved pedal setups from the October 28, 2026 backup,
converted to setup format 20 authoring. The saved selection was `pman`. The
original backup files in `/private/tmp/cntrl-actions-device-backup` are preserved.
These files have not been compared with a fresh read-back from the pedal.

- `pman.toml` preserves all 256 patches in their 15 patch cycles, the program names,
  bank mappings and display settings. It embeds its instrument definitions.
- `font-demo.toml` preserves the built-in handwriting and Courier demonstration.
- `sd-font-demo.toml` preserves the SD font aliases and display configuration,
  with font paths pointing to the repository's original TTF files. Compilation
  regenerates compatible font assets instead of reusing old bitmap files.
- `songs.toml` preserves the empty saved song library and its next occurrence ID.
  It compiles to the current CSG4 song format without inventing any songs.

Compile a setup from the repository root, for example:

```bash
mkdir -p build/converted-setups
uv run cntrl compile cntrl/maker/morningstar/mc/setups/pman.toml \
  --target cntrl/maker/morningstar/mc/patch-switcher-target.json \
  --output build/converted-setups/pman.bin
```

Use the same command with `font-demo` or `sd-font-demo` to compile either font
setup. Font assets are written beside the compiled setup in `fonts/`.

The generic examples remain under `examples/patch-switcher/`; their pman example
uses `instruments/definitions.toml`, `instruments/patchman.toml`, and
`instruments/tags.toml`. Instrument
catalogues use numbered bank tables containing ordered program names and metadata.
They are program descriptions, not executable Patch values.
The documented program catalogues contain fixed voice information; personal tag
assignments live separately in `instruments/tags.toml`.

The shared channel-1 Internal-bank setup is in
`examples/patch-switcher/dual-internal.toml`. It sends one Program Change per
activation and shows both instruments' sound names. Both units must already be
set to Internal. From the repository root:

```sh
uv run --frozen cntrl compile examples/patch-switcher/dual-internal.toml \
  --target cntrl/maker/morningstar/mc/patch-switcher-target.json \
  --table instruments/tags.toml --output /tmp/dual-internal.bin
```
