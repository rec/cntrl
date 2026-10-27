# Bitmap display fonts

The MC firmware includes four converted faces alongside classic, Arial and
ComicSansMS:

| Configuration name | Source | Face ID | License |
| --- | --- | --- | --- |
| `Circuit` | Orbitron Medium | 3 | SIL OFL 1.1 |
| `CircuitBold` | Orbitron Bold | 4 | SIL OFL 1.1 |
| `StillTime` | Still Time | 5 | CC0 1.0 |
| `MetalLord` | Metal Lord | 6 | CC0 1.0 |

Each face contains printable ASCII, characters 32 through 126, at nominal pixel
sizes 12, 16, 20, 24, 28, 32, 40 and 48. Actual line heights depend on the face.
`autoscale_fonts = true` selects a fitting size for each line. Fixed sizes use
the usual `font_size` setting. Styles can select these fonts at any existing
style level, including individual page fields.

Circuit and CircuitBold are bitmap derivatives of Matt McInerney's Orbitron.
The original font reserves the name Orbitron, so these converted versions have
different names. Their font data remains under the [SIL OFL 1.1](../../../fonts/orbitron/OFL.txt).
Still Time and Metal Lord are by Raymond Larabie / Typodermic and remain under
[CC0 1.0](../../../fonts/CC0-1.0.txt). Source fonts, attribution and provenance
are in [`cntrl/fonts`](../../../fonts).

## Regenerating

Run from the repository root, using the locked Pillow build dependency:

```bash
uv run --frozen -m cntrl.maker.morningstar.mc.build_bitmap_fonts
uv run --frozen -m cntrl.maker.morningstar.mc.build_font_metrics
```

The first command writes `firmware/patch_switcher/font_*.cpp` and
`bitmap_fonts.h`. It uses monochrome FreeType rendering without kerning and
encodes the existing ST7735 version 1 font format. The second command exports
glyph advances, bearings and line heights into the existing target description.
Do not edit generated files directly. Font bytes and descriptors are placed in
flash on the pedal. No outline rasterizer or Pillow dependency runs there.

Tests compare all 3,040 generated glyphs with their source pixels and baseline
positions, check reproducible generated files and exported metrics, and verify
that autoscaling finds the largest fitting size. Native tests cover the font
registry; the ARM build checks integration with the real display library.

## Demo

[`font-demo.toml`](font-demo.toml) displays CircuitBold in cyan, Still Time in
yellow and Metal Lord in red, on three separately scaled lines. It disables
the screensaver and startup animation. Initially the candidate display shows
the samples. It uses the normal performance controls; pressing C sends program
1 on DIN MIDI channel 1, so viewing the demo does not require pressing C.

Compile and copy it as a separate named setup:

```bash
uv run --frozen cntrl setup copy cntrl/maker/morningstar/mc/font-demo.toml \
  --target cntrl/maker/morningstar/mc/patch-switcher-target.json \
  --port /dev/cu.usbmodem144682801
uv run --frozen cntrl setup set font-demo --port /dev/cu.usbmodem144682801
```

Return to the saved performance setup with `cntrl setup set pman --port
/dev/cu.usbmodem144682801`, or select it through the pedal's A+D management menu.
Firmware uploads use the existing [upload helper](../../../../doc/upload_helper.md)
and the combined stock/cntrl image to preserve dual boot.

## Build verification, 2026-10-27

All 303 Python tests, target native firmware tests, Ruff, formatting, ty and
pyupgrade checks passed. The combined stock/cntrl build passed its stock patch,
flash configuration and boot-envelope checks. Its SHA-256 is
`6d460a7109dd71189dd678330c33c28e9bcf9f47590f7a66063a7507a626e9d6`.
The linked font data and descriptors are in the `0x601...` flash region. RAM1
leaves 96,768 bytes for the stack, and RAM2 leaves 309,216 bytes for the heap.
These are linker budgets. Physical readability requires checking the pedal.

The combined image was uploaded through the running upload helper. The first
attempt reported a USB write error; after confirming the pedal still enumerated
as HalfKay with serial 14468280, the same verified image uploaded successfully.
The pedal returned directly to cntrl and reported the unchanged pman setup as
`CNTRL READY 11 34583 43363d4a`. A resolved pman backup is saved locally under
the ignored `firmware/build/fonts-backup/` directory.

The separate `font-demo` setup was copied with byte-for-byte read-back
verification and selected. Status is `CNTRL READY 11 1306 9d0922bd`; the setup
catalogue still includes pman. No MIDI send was requested during deployment.
Physical appearance has not yet been confirmed.
