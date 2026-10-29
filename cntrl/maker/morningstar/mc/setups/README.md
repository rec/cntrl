# Saved pedal setups

These are the three saved pedal setups from the October 28, 2026 backup,
converted to setup format 18 authoring. The saved selection was `pman`. The
original backup files in `/private/tmp/cntrl-actions-device-backup` are preserved.
These files have not been compared with a fresh read-back from the pedal.

- `pman.toml` preserves all 256 patches in their 15 patch lists, the program names,
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
uses `instruments/definitions.toml` and `instruments/pman.toml`. Instrument tables
are program descriptions, so they retain the flat `[[patches]]` table format and
explicit setup names. They are not executable Patch values.
