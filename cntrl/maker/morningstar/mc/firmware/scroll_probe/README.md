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

## Stages

Initial painting happens while dark. Tiles are 32 pixels wide, numbered 00..29,
with a shared grid/diagonal pattern in a repeating 960x128 image. It reuses the
installed ST7735 library's classic font data. Each panel initially shows 160
consecutive columns: left 0..159, centre 160..319, right 320..479.

- `ONE`: every 33 ms only the left panel receives `0x37` and two address bytes.
  No pixel data is rewritten after initial painting. Its five initial tiles
  should move horizontally and wrap; the other panels should remain stationary.
- `PAN`: every step fills one circular-buffer column per panel, then changes
  all three scroll offsets. Panels show adjacent windows at world positions
  `x`, `x+160` and `x+320`, modulo 960. Pixel traffic is exactly 768 bytes per
  step, plus command traffic. Tiles should continue across panel boundaries.

Press and release C to toggle stages. Each toggle starts again at world x=0,
briefly blanks the backlight and repaints the baseline. Other buttons do nothing.
All SPI calls finish and restore GPIO 13/12 before the next button scan.

`CNTRL STATUS` returns, for example:

```text
CNTRL SCROLL READY stage=ONE x=380 ssa=101 steps=380 pixels=0 us=5
```

`pixels` counts streamed pixel bytes after the baseline, not baseline painting or
command bytes. `us` measures the most recent step, including pixel generation,
SPI writes and scroll commands. It is elapsed time, not a sampled CPU load.
`CNTRL SCROLL NEXT` toggles stages from the host. The usual `cntrl status --port
/dev/cu.usbmodem144682801` command can read status without new host tooling.

## Build and test

```bash
make -C cntrl/maker/morningstar/mc/firmware test-scroll-probe inspect-scroll-probe
make -C cntrl/maker/morningstar/mc/firmware test
```

Native tests cover circular-buffer contents over two complete source-image
cycles, pixel byte order, exact register payloads, selected-panel isolation,
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

## Upload record, 2026-10-24

Diagnostic HEX SHA-256:
`977daac74c91f826d1da56238a2029b75d358b4aeb59621dca18494d6fa54851`.
Both native test targets and stock flash configuration/HEX-ELF checks passed.
Software bootloader entry and helper upload completed; the uploader returned
status 1 after its usual reset/disappearance, then USB confirmed the ONE status
above. Before upload the application reported `CNTRL READY 7 0 00000000`:
no compatible show was active. The diagnostic made no changes to that state.
Physical scrolling support, direction, seams and synchronization await visual
confirmation. No claim of hardware-assisted scrolling is based on counters alone.
