# Controller scrolling diagnostic

Standalone experimental firmware, not a patch-switcher mode. It initializes only
the display/backlight and switch-reader pins, uses USB serial and never opens
the SD card or sends MIDI. Saved shows and animation assets are untouched. It
remains installed across power cycles until normal firmware is uploaded again.

The diagnostic uses the stock panel initialization, followed by native-coordinate
`MADCTL = 0x88` (MY/BGR retained, MV cleared), `SCRLAR = 1,160,1` and scroll start
1. It assumes a 162-line controller with the visible gate lines 1..160. This
mapping is an experimental hypothesis, not identification of the installed chip.
Unexpected orientation or edge lines must be distinguished from absent scrolling.

## Speed comparison

Initial painting happens while dark. Tiles are 32 pixels wide, numbered 00..39,
with a shared grid/diagonal pattern in a repeating 1280x128 image. It reuses the
installed ST7735 library's classic font data. Each panel initially shows 160
consecutive columns. With the default 64-pixel gap the windows are left 0..159,
centre 224..383 and right 448..607. Columns 160..223 and 384..447 are hidden
behind the two physical gaps, rather than squeezed out of the image.

All three panels show windows at world positions `x`, `x+224` and `x+448`,
modulo 1280. The artwork keeps the same pixel scale; it continues behind the
estimated gaps. Updates target 16,667 microseconds, approximately 60 Hz.
Each update fills the entering circular-buffer columns per panel, then changes
all three scroll offsets once. Pixel traffic is 768 bytes times the stride per
update, plus command traffic. Tiles should continue across panel boundaries.

Initially the speed is one pixel per update (60 pixels/second). Press and release
C to cycle through 1, 2, 4, 8, 16 and 32 pixels per update, then back to 1, without
blanking, repainting or resetting position. These correspond to 60, 120, 240,
480, 960 and 1,920 pixels/second if the update fits within its time budget.
The current isolation baseline cannot maintain 60 Hz at the highest strides.
Press and release B to toggle the panel update order. The initial order is
left, centre, right (`LTR`); the alternate is right, centre, left (`RTL`). Both
the entering-pixel writes and the subsequent scroll commands use that order.
The image's position, direction, speed and physical window assignments do not
change, and toggling does not blank or repaint the screens. Other buttons do
nothing.
All SPI calls finish and restore GPIO 13/12 before the next button scan.

The complete source pattern is cached once at startup in 327,680 bytes of RAM.
The isolation baseline restores 8 MHz byte transfers and addresses each column
separately, even when the caller supplies several columns. This removes the
faster clock, 16-bit transfers and controller multi-column addressing from the
rendering path while keeping the cache and six speeds. Each source column wraps
independently, so gaps need not be multiples of a tile or controller width.
Initialization uses the shared driver; the normal application's driver is unchanged.

`CNTRL STATUS` returns fields such as:

```text
CNTRL SCROLL READY order=LTR spi=8 gap=64 rate=60 stride=1 x=110 ssa=51 steps=110 pixels=84480 us=... period=... max_us=... overruns=...
```

`pixels` counts streamed pixel bytes after the baseline, not baseline painting or
command bytes. `us` measures the most recent step's SPI writes and scroll
commands. `period` is the interval between the latest two update starts, in
microseconds. `max_us` is the worst step duration and `overruns` counts steps
exceeding the 16,667-microsecond budget; both reset on speed or order changes.
These are
elapsed times, not sampled CPU load. `rate=60` is the target, not a measurement.
`CNTRL SCROLL NEXT` toggles speed from the host. The usual `cntrl status --port
/dev/cu.usbmodem144682801` command can read status without new host tooling.

## Build and test

```bash
make -C cntrl/maker/morningstar/mc/firmware test-scroll-probe inspect-scroll-probe
make -C cntrl/maker/morningstar/mc/firmware test
```

`SCROLL_GAP` configures both equal gaps in equivalent display pixels at build
time, default 64. For example, to test and build a 48-pixel estimate:

```bash
make -C cntrl/maker/morningstar/mc/firmware SCROLL_GAP=48 test-scroll-probe inspect-scroll-probe
```

The firmware status reports the compiled gap. This standalone diagnostic does
not load show configuration or modify saved settings.

Native tests cover all six strides, including unaligned starts and source-image
wraps, pixel byte order, per-column register payloads, both panel command orders,
selected-panel isolation,
coordinate bounds and SPI/GPIO handoff. These establish software behaviour, not
whether the physical controller implements scrolling. Inspection compares HEX
with ELF bytes and checks the stock flash-configuration digest. No build target
uploads anything.

The image is `../build/scroll-probe.hex`. To use the existing fixed-image upload
helper, it is copied to `../build/patch-switcher.hex` only after inspection. This
does not change the normal application sources or the helper's target restriction.
The normal build was preserved as `../build/application-before-scroll.hex` before
staging the diagnostic. That saved build has SHA-256
`60ebbcc450560197396faecdc9e6c96827fb078737ec73d82585ec9fb27353cd`.
Restore it through the same software bootloader/helper workflow, or build and
inspect the current patch-switcher target before restoration. Neither restoration
path needs to modify saved card files.

## Original upload record, 2026-10-24

Diagnostic HEX SHA-256:
`977daac74c91f826d1da56238a2029b75d358b4aeb59621dca18494d6fa54851`.
Both native test targets and stock flash configuration/HEX-ELF checks passed.
Software bootloader entry and helper upload completed; the uploader returned
status 1 after its usual reset/disappearance, then USB confirmed the ONE status
above. Before upload the application reported `CNTRL READY 7 0 00000000`:
no compatible show was active. The diagnostic made no changes to that state.
The user subsequently reported that both demonstrations looked good. This is
visual confirmation, not an instrumented measurement of panel synchronization.

## 60 Hz speed-comparison upload, 2026-10-24

HEX SHA-256:
`1ffaca937276d46776e006bfec7415a5565bca4e84582147ff6c3335fe68f8a3`.
Both diagnostic tests, the existing button/display/renderer tests, and stock
flash configuration/HEX-ELF checks passed. After software bootloader entry and
upload, USB confirmed the `rate=60 stride=1` report above. The uploader returned
its usual status 1 on reset/disappearance; the serial response confirms the new
firmware booted. Visual comparison of the two speeds remains a physical test.

## Optimized six-speed upload, 2026-10-24

HEX SHA-256:
`019c1c671fb4a4e2a12bd6f0c3a6cb5a63059ee2e1bf0c7e4f4ee0caa1d18735`.
All native tests and image checks passed. The first attempted optimization used
the SPI library's bulk-buffer transfer; the device enumerated but stopped
responding to status. Software bootloader entry still worked. Replacing that
call with blocking 16-bit transfers restored normal USB responses. The precise
cause of the bulk-transfer stall was not established; that path is not retained.

After upload, each stride ran for two seconds under USB observation:

| Pixels/update | Measured updates/second | Worst update, microseconds |
| --- | --- | --- |
| 1 | 59.86 | 438 |
| 2 | 60.32 | 858 |
| 4 | 59.89 | 1,745 |
| 8 | 60.31 | 3,452 |
| 16 | 59.83 | 6,727 |
| 32 | 59.93 | 13,739 |

All six reported a latest start-to-start period of 16,667 microseconds and zero
overruns. The host rate estimates use short sample windows and integer frame
counts, hence the small spread around 60 Hz. The test ended back at stride 1.
Pixel transfer throughput is confirmed; visual quality at the increased clock
and higher strides still needs the user's observation. Saved card data was not
touched. At the highest stride, button scanning can be delayed by a whole update
(about 14 ms); the diagnostic does not claim a 3 ms worst-case scan interval.

The user subsequently reported intermittent glitches on the left panel and
stable rendering garbage on the other two. The timing results did not establish
correct rendering. The exact cause has not been isolated.

## Rendering isolation baseline

Restore the previously working 8 MHz byte-wise, single-column write protocol,
keeping the cached image and six-speed controls. At high strides, frame timing
will exceed the 60 Hz budget and button scanning can be delayed by that longer
update. This test prioritizes rendering correctness over speed. A clean result
would narrow the fault to the removed transfer/addressing changes, not identify
which one caused it. Saved card files remain untouched.

Built and uploaded with HEX SHA-256
`3c79bbc0f3a017aa98782f98c210bb0e9108af4da41bc2b1e2ce4387a284b7f2`.
Diagnostic and button/display/renderer native tests passed, as did the stock
flash-configuration and HEX/ELF checks. The uploader returned its usual status 1
on reset/disappearance; USB then confirmed this baseline running at stride 1:

```text
CNTRL SCROLL READY spi=8 rate=60 stride=1 x=333 ssa=148 steps=333 pixels=255744 us=965 period=16667 max_us=965 overruns=0
```

The user subsequently confirmed that rendering was clean again. This restores
a visually confirmed baseline, without identifying which removed optimization
caused the earlier corruption.

## Gap-aware panorama, 2026-10-24

The image now contains 40 numbered tiles over 1,280 columns, with a configurable
equal gap between the three windows. The uploaded build uses `SCROLL_GAP=64`.
The known-working 8 MHz single-column transfer path is unchanged. The cache
grows to 327,680 bytes, leaving 156,704 bytes free in RAM1 according to the build.

Native tests passed for gaps of 64 and 17 pixels, including all six strides and
source-image wraparound. Button/display/renderer tests and stock flash-config
and HEX/ELF checks passed. Uploaded HEX SHA-256:
`f60225f7b146f561994525c3c85d8812313415cbf0a67129c62dea9026c538dd`.
After the uploader's usual reset/disappearance status 1, USB confirmed:

```text
CNTRL SCROLL READY spi=8 gap=64 rate=60 stride=1 x=500 ssa=141 steps=500 pixels=384000 us=965 period=16667 max_us=965 overruns=0
```

The physical continuity illusion at the estimated gap width awaits visual
confirmation. The diagnostic still does not open the SD card or send MIDI.

## Panel update-order comparison, 2026-10-24

B toggles both pixel-write and scroll-command order; C retains its six speeds.
This tests whether the observed left/centre motion hitch follows transaction
order. It does not synchronize the three independent panel refresh cycles or
establish the cause of the hitch by itself.

Native tests verify both panel-index sequences and actual chip-select order for
forward/reverse scroll commands, including a selected-panel subset. Diagnostic,
button/display/renderer tests and stock flash-config/HEX-ELF checks passed.
Uploaded HEX SHA-256:
`5bea12f8816fd8576788914d78735fd9c9b0fe00e49e48539b78f9428ab8df91`.
The uploader returned its usual status 1 on reset/disappearance; USB then
confirmed the new build running in the original left-to-right order:

```text
CNTRL SCROLL READY order=LTR spi=8 gap=64 rate=60 stride=1 x=449 ssa=32 steps=449 pixels=344832 us=964 period=16667 max_us=965 overruns=0
```

The effect of B on visible motion awaits physical observation. Saved card data
remains untouched.
