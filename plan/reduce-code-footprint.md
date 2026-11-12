# Reduce executable code footprint

Measure executable bytes independently of embedded fonts, constant data, debug
information and the original firmware carried in the combined image. Keep all
existing pedal features while evaluating compiler size optimizations.

## SD-card formatting code

The cntrl firmware does not call the SD-card formatter. The SDK's `SDClass`
virtual method table retains `SDClass::format`, which references the FAT16/FAT32
and exFAT formatters. Startup mounts an existing filesystem and returns failure
if mounting fails; there is no automatic formatting fallback.

The previously inspected build at `9b27cc3` contains these executable bytes:

| Formatting component | Bytes |
| --- | ---: |
| FAT16/FAT32 formatter, callback and local printing helper | 1,248 |
| exFAT formatter and local printing helper | 1,436 |
| `SDClass::format` | 108 |
| Total identified formatting code | 2,792 |

This is about 2.73 KiB, plus formatting-specific strings. An actual removal and
comparison build is needed to establish the exact total saving. Shared helpers,
string pooling and alignment can change the difference.

The pedal must never format, repartition or create a filesystem on an SD card.
Establishing card communication and mounting an existing filesystem remain
necessary. Ordinary application file and directory creation is separate.
Consider replacing the Arduino `SDClass` wrapper with direct `SdFs` use, while
preserving mounting and timestamp handling. Verify absence of formatting symbols
in the resulting image. This investigation does not establish what the original
Morningstar firmware does.

## Compiler experiments

Compare the production `-Os` configuration with `-Oz`, each with and without
link-time optimization. Keep the pinned SDK, linker scripts, feature definitions,
fonts and source revision constant across the matrix. Do not change production
defaults as part of these experiments.

Measured on 2026-11-12 against firmware source revision
`05cc5c20e92b297ca84aa46ee29d8a442f0fba25`, including the long double-tap gesture
change. The only build-helper change for this experiment extracts the existing
production compiler command for reuse; production flags remain unchanged.

Environment: Teensy 4.1 at 600 MHz, serial/MIDI USB configuration, Arduino CLI
1.5.1, Teensy core 1.62.0 and ARM GCC 15.2.1. Existing SDK patches, heap-accounting
linker wrappers and all existing feature definitions remain enabled.

| Compiler configuration | Build result | Executable bytes | Usable saving |
| --- | --- | ---: | ---: |
| `-Os`, no LTO, production baseline | Success | 365,708 | Baseline |
| `-Oz`, no LTO | Success | 365,708 | 0 |
| `-Os`, LTO | Link failure: DTCM overflow | Not linked | Not established |
| `-Oz`, LTO | Link failure: DTCM overflow | Not linked | Not established |

Executable bytes mean `.text.code + .text.itcm + .fini`, including those
sections' padding. This excludes font/constant data in `.text.progmem`, initialized
data, boot headers, authentication data, unwind metadata and debug information.
The earlier roughly 357 KiB estimate and the new 365,708-byte baseline are
consistent: the latter is 357.14 KiB and uses a newer firmware revision.

Both successful builds have 169,848 bytes in `.text.code`, 195,856 in
`.text.itcm`, and four in `.fini`. Their font/constant section is 859,860 bytes;
RAM1 `.data + .bss` is 309,824 bytes and static RAM2 usage is 90,752 bytes.
The verbose compile and link commands confirm that `-Oz` was actually used for
the variant; equal section sizes are not an assumption based on the selected
board menu label.

LTO uses the SDK's `opt=oslto` selection plus `-flto=auto`,
`-fno-fat-lto-objects` and the linker plugin. Optimization and LTO flags apply
at compilation and final linking, including SDK/library compilation.

### Diagnostic LTO measurements

To measure code generation despite the failed link, two additional experiments
pass `--noinhibit-exec` to the linker and save a link map. These deliberately
retain invalid ELF files for analysis. Their build commands still fail; these
are not usable firmware images and must never be uploaded.

| Diagnostic configuration | Executable bytes | Reduction | Reduction percentage |
| --- | ---: | ---: | ---: |
| `-Os` with LTO, invalid ELF retained | 352,404 | 13,304 | 3.64% |
| `-Oz` with LTO, invalid ELF retained | 352,404 | 13,304 | 3.64% |

Both diagnostic images have these section sizes:

| Section/accounting | Bytes |
| --- | ---: |
| Flash code, `.text.code` | 27,952 |
| Instruction RAM code, `.text.itcm` | 324,448 |
| `.fini` | 4 |
| Font/constant flash, `.text.progmem` | 1,124 |
| Initialized data, `.data` | 903,872 |
| Uninitialized data, `.bss` | 278,016 |
| RAM1 data total | 1,181,888 |
| Static RAM2, `.bss.dma` | 90,784 |

The linker's reported DTCM overflow is 657,600 bytes. RAM1 data alone exceeds
the pedal's entire 512 KiB RAM1, before accounting for instruction RAM banks
and stack space. The diagnostic symbol table places `ui_font_0`, `ui_font_2`
and `ui_font_5` in `.data`, rather than their intended flash section.
The intended code placement also changes: `Document::load`, for example, is
in `.text.itcm` in the diagnostic LTO image. Thus the lower executable byte
count is accompanied by an unusable memory layout.

The LTO logs also report type-mismatch warnings for the custom USB string
descriptors and linker-symbol declarations for `_ebss`, `_heap_start` and
`_heap_end`. These are separate from the fatal memory overflow and remain
unresolved in this experiment.

### Decision and next investigation

Keep production `-Os` without LTO. `-Oz` alone delivers no measured saving.
LTO shows about 13 KiB of potential executable reduction, but that is not a
deployable saving. A subsequent LTO investigation should preserve font flash
placement and the intended flash/instruction-RAM code placement, resolve the
type warnings, and then repeat size and runtime timing checks. Correcting the
placement may change the measured saving.

No firmware was uploaded or exercised on a pedal. These experiments establish
build and section-size results, not USB, MIDI timing, startup or hardware
correctness for an LTO build. SD formatting removal remains a separate change.

## Reproduction

The versioned experiment runner reuses the production compiler command, performs
the ordinary baseline build and isolates the other five build directories. It
records commands, build logs, section sizes, symbols, ELF hashes, source revision
and toolchain provenance. It never uploads or runs firmware on a pedal.

```sh
uv run python -m cntrl.maker.morningstar.mc.code_footprint --output build/code-footprint-2026-11-12
```

Use a new output directory for each run. The runner refuses an existing output
directory so a failed compilation cannot accidentally measure a stale ELF.
Normal LTO failures are recorded before the explicitly labelled diagnostic
variants run. Production build options are never overwritten.

The measured run's generated artifacts are in
`/private/tmp/cntrl-code-footprint-complete`, with `results.toml` and
`toolchain.toml` at its root. Each variant has a build log, and variants that
produced an ELF also have `memory.txt` and `symbols.txt`. Diagnostic variants
add `link.map`. Generated build outputs and logs are disposable; the experiment
source and this record belong in version control.

## Additional work beyond the prompt

None.
