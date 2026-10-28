# Bitmap display fonts

Seven bundled outline families are available alongside classic, Arial and
ComicSansMS. The IDs below identify their generated reference tables:

| Configuration name | Source | Face ID | License |
| --- | --- | --- | --- |
| `Circuit` | Orbitron Medium | 3 | SIL OFL 1.1 |
| `CircuitBold` | Orbitron Bold | 4 | SIL OFL 1.1 |
| `StillTime` | Still Time | 5 | CC0 1.0 |
| `MetalLord` | Metal Lord | 6 | CC0 1.0 |
| `CaveatBold` | Caveat Bold | 7 | SIL OFL 1.1 |
| `CourierPrime` | Courier Prime Regular | 8 | SIL OFL 1.1 |
| `CourierPrimeBold` | Courier Prime Bold | 9 | SIL OFL 1.1 |

The generated reference tables contain printable ASCII, characters 32 through
126, at nominal pixel
sizes 12, 16, 20, 24, 28, 32, 40 and 48. Actual line heights depend on the face.
Show text uses sparse SD assets built from the corresponding outlines, as
described below. Each line automatically selects its largest fitting size. Styles select a font
using `font_name` at any existing
style level, including individual page fields.

Circuit and CircuitBold are bitmap derivatives of Matt McInerney's Orbitron.
The original font reserves the name Orbitron, so these converted versions have
different names. Their font data remains under the [SIL OFL 1.1](../../../fonts/orbitron/OFL.txt).
Still Time and Metal Lord are by Raymond Larabie / Typodermic and remain under
[CC0 1.0](../../../fonts/CC0-1.0.txt). Source fonts, attribution and provenance
are in [`cntrl/fonts`](../../../fonts).

Caveat Bold is by the Caveat Project Authors, originally designed by Pablo
Impallari. Courier Prime is by the Courier Prime Project Authors, designed by
Alan Dague-Greene for Quote-Unquote Apps. Their bitmap data remains under the
included [Caveat OFL](../../../fonts/caveat/OFL.txt) and
[Courier Prime OFL](../../../fonts/courier-prime/OFL.txt), respectively. Neither
included license declares a Reserved Font Name.

## Fonts supplied by a show

A show can declare local TrueType or OpenType files and select them with the
existing style settings:

```toml
[fonts]
Handwriting = "fonts/MyHandwriting.ttf"

[style]
font_name = "Handwriting"
```

Paths resolve relative to the TOML file containing the declaration, including
when combining several documents with `+`. Names must differ from built-in font
names. A declaration can be selected anywhere `font_name` is supported, including
page and line styles.

The normal `cntrl setup copy` command shapes text with HarfBuzz and rasterizes
it with FreeType on the development machine. Only glyphs actually used by the
show, at the sizes selected by automatic fitting, are uploaded to
`/cntrl/fonts/` on the pedal's SD card. The bundled outline families (Circuit,
CircuitBold, StillTime, MetalLord, CaveatBold, CourierPrime and CourierPrimeBold)
use this same sparse path automatically. Classic, Arial and ComicSansMS remain
built-in ASCII fonts for the UI and shows that select them. StillTime at size
28 remains built in for the fixed cable-test title; show text uses its SD subset.

Display labels and static page fields accept Unicode supported by the selected
outline font. UTF-8 labels are limited to 63 bytes by the existing display
buffers. Identifiers, routing names and the on-pedal editing UI retain their
existing ASCII repertoire. Before emitting a show, the compiler reports all
missing glyphs with their font, Unicode codepoint and field location, then fails.
It does not silently replace missing characters or choose another font.

The host prepares complete glyph runs, including ligatures, combining marks,
script shaping and bidirectional ordering. The pedal draws those positions and
does not run a Unicode shaping engine. Explicit bidi isolation controls are not
supported. Fonts must provide monochrome-renderable outlines; color emoji and
vertical text are not supported. Runtime numeric fields reserve digits and a
minus sign, composed individually on the pedal. Boolean settings reserve both
`true` and `false`. Colors and stippling remain runtime effects, with transparent
glyph backgrounds.

Font changes use normal SD uploads once the supporting firmware is installed.
They do not erase or program the firmware flash. Identical assets are shared
between shows and are read back for verification without being rewritten.
The SD card itself also uses flash storage; this feature avoids firmware writes,
not all flash writes.

`cntrl compile --output PATH` also writes assets under `fonts/` beside its binary
output. `cntrl setup download` downloads the show's bitmap assets beside its TOML,
with declarations pointing to those `.cfont` files. They can be recompiled with
their existing text, sizes and styling without the original outline fonts.
Adding text or choosing a size absent from a subset requires rebuilding with
the original TTF/OTF file. Original outline files are not stored on the pedal.

Only sizes referenced by the active show are loaded into RAM, before activation.
Rendering does not read the SD card. The compiler and pedal enforce a maximum of
32 distinct asset/size pairs and 96 KiB of loaded bitmap data. A single SD asset
is limited to 256 KiB. Candidate fonts are loaded separately before replacing the
active fonts, so a failed load preserves the current show. Missing assets prevent
show upload; corrupt pixel data or insufficient memory prevent activation.
Unused font files remain on SD when a show is deleted.

The `.cfont` format begins with `CFN2` and a little-endian 32-bit size count.
Each size has a 23-byte descriptor: nominal size, format byte `2`, five reserved
zero bytes, three 16-bit counts (glyphs, runs, runtime characters), byte-sized
line height and baseline, and 32-bit index/pixel lengths. The index contains:

- Sorted glyph records `<IHHhhI>`: original glyph ID, width, height, left bearing,
  top bearing and bitmap offset.
- Runtime character records `<IHh>`: Unicode codepoint, local glyph index and
  integer advance.
- Exact UTF-8 runs: `<HhhH>` for text byte length, left bound, width and position
  count, followed by the text and `<Hhh>` glyph-index/x/y positions.

All fields are little-endian. Each glyph bitmap uses one bit per pixel, most
significant bit first, with each row padded to a byte. Repeated characters and
ligatures share glyph bitmaps within a size. Assets are named by eight-digit
CRC32, validated in a temporary file, then renamed into place. CRC32 checks
integrity; byte-for-byte read-back also detects a collision with an existing
asset. Old `CFN1` assets must be regenerated with their outline files.
Show-format 14 display records use font ID 255 and the final four bytes for the
asset CRC32. Built-in font records retain zero in those bytes. Firmware with the
CFN2 reader is required before uploading sparse fonts.

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

Tests compare all 5,320 generated glyphs with their source pixels and baseline
positions, check reproducible generated files and exported metrics, and verify
that autoscaling finds the largest fitting size. Native tests cover the font
registry; the ARM build checks integration with the real display library.

## Demo

[`font-demo.toml`](font-demo.toml) displays Caveat Bold in cyan, Courier Prime in
yellow and Courier Prime Bold in red, on three separately scaled lines. Both
Courier samples read `Courier` so their weights can be compared at the same size.
It disables
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
The user confirmed a rendering defect in Still Time: opaque per-character
backgrounds erased overlapping italic strokes. Individual glyph conversion
tests did not cover that compositing behavior.

### Transparent text correction

The renderer now paints only glyph foreground pixels over the existing line
background bands. Both initial graphics configuration and per-line color
selection use the display library's transparent mode. A native regression
checks that an overlapping space preserves previously drawn strokes, and the
existing repaint, background, padding and transition tests still pass.

The corrected combined image has SHA-256
`f4edd145bf919958535d8dd23e575e580fdcb5ff2df6635299b0d78a5d7b5817`.
It passed the native tests and combined-image build checks and was flashed
through the upload helper. The first USB write failed; after verifying HalfKay
identity, the second attempt completed and stock firmware returned. Enter cntrl
with A+F to check the saved demo and confirm the corrected appearance.

### Caveat and Courier Prime additions

Caveat Bold and both Courier Prime weights use the same conversion, metrics
export and transparent rendering paths as the first four faces. Existing face
IDs are unchanged. All 330 Python tests and native firmware tests passed, as
did Ruff, formatting, ty, pyupgrade and combined-image build checks. The new
font bytes and descriptors were verified in flash; stack and heap linker
budgets remain 96,768 and 309,216 bytes.

Combined firmware SHA-256:
`39057d704478f0c42d16b80ce04f5cab0d346aef16c881dd0340bc9e61e11c0e`.
The image was flashed through the upload helper after a USB write error and
HalfKay identity verification before retrying. Stock firmware returned.
The revised demo compiles to 1,340 bytes and uses size 32 for all three samples.
After entering cntrl with A+F, the revised `font-demo` was copied with
byte-for-byte read-back verification and selected. The pedal reports
`CNTRL READY 11 1340 1dcf95b1`; its catalogue still includes pman. No MIDI send
was requested. Physical appearance of the new samples awaits confirmation.

### Custom SD font assets, 2026-10-27

All 341 Python tests, both native firmware suites, Ruff, formatting, ty and
pyupgrade checks passed. New sanitizer-enabled host tests exercise the real C++
SD font receiver and document activation with a host filesystem adapter. They
cover missing and malformed assets, byte-for-byte transfer, unchanged-file reuse,
interrupted uploads, switching between built-in and custom fonts, preservation
of active fonts after a failed load, and the compiler's RAM budget check.

The combined stock/cntrl image built successfully with SHA-256
`ba2bda4ba4b6e87a65a893d40ee8eb8a268d388c3e5a01fe08a228755e632dc3`.
RAM1 leaves 92,384 bytes for stack/local variables and RAM2 leaves 309,216 bytes
for allocation before custom fonts are loaded. The 96 KiB font budget applies
separately to active and candidate font data while switching shows.

This image was not flashed. A read-only device query returned
`CNTRL READY 11 34583 43363d4a`, while the current source uses show format 14.
Deployment therefore also requires recompiling the pedal's existing setups.
Physical rendering and SD-card latency have not been verified for custom assets.

### SD font deployment completed, 2026-10-27

The image above was subsequently installed through the running upload helper.
The first attempt reported a USB write error. After checking that the expected
serial 14468280 still enumerated as HalfKay, the same verified image uploaded
successfully. Stock firmware returned, and the user entered cntrl with A+F.

Before flashing, both existing setups were downloaded with checksum verification.
The historical version-11 exporter from `681ed88^` exported each backup, and the
historical compiler reproduced both original binaries byte-for-byte. Local
backups, converted TOML, verification scripts and downloaded results are retained
under the ignored `firmware/build/sd-font-deploy-20261027/` directory.
The conversion preserved all 256 pman patches, their order, instrument routes and
MIDI bytes. Display settings were converted to the current automatic fitting and
checkerboard treatment of already-sent names.

The three installed setups passed byte-for-byte read-back:

| Setup | Show bytes | CRC32 |
| --- | ---: | --- |
| `pman` | 34597 | `488beaf6` |
| `font-demo` | 1354 | `e824a96d` |
| `sd-font-demo` | 1473 | `b98c9bca` |

The SD demo uses Caveat Bold, Courier Prime and Courier Prime Bold through local
font declarations. Its three SD assets total 107365 bytes:
`7cdcbd69.cfont` (35586), `a60f4793.cfont` (36466), and
`f57d73e3.cfont` (35313). Each asset was downloaded and compared byte-for-byte;
a second copy request exercised reuse of each existing asset. The demo activated
successfully, and the user confirmed correct appearance of all three samples.

The normal setup download command then downloaded all three setups and the SD
font files. Each downloaded TOML recompiled byte-for-byte to its installed binary,
and its regenerated font assets matched the uploaded files. No MIDI send was
requested during deployment or verification. The pedal was returned to `pman`;
final status is `CNTRL READY 14 34597 488beaf6`. SD latency was not measured.
