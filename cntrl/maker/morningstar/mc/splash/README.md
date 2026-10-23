# Isolated MC6 Pro splash animation

Everything in this directory is additive. No existing graphics, show-format,
firmware target, CLI or build file is changed. Nothing here has been uploaded to
the pedal. The running firmware does **not** recognize the splash protocol yet.

The source GIF is preserved in `assets/spiral.gif`: 16 frames, 100 ms per frame,
1,600 ms per loop. The Mac scales it to cover 160x128 and crops centrally, with
no margins or letterboxing. This preserves proportions and crops the top and
bottom of the square original. The centre screen is horizontally mirrored;
both outer screens use the unmirrored image. Padding and show text colours do
not apply to this full-bleed splash.

## Prepare on the Mac

FFmpeg and ffprobe must already be available on PATH. No Python dependency was
added. GIF decoding/compositing, resizing, cropping, mirroring, colour conversion
and frame-timing conversion all happen here, not on the pedal.

From the repository root:

```bash
uv run --frozen -m cntrl.maker.morningstar.mc.splash.cli prepare \
  cntrl/maker/morningstar/mc/splash/assets/spiral.gif \
  --output cntrl/maker/morningstar/mc/splash/build/spiral.cnsa
```

The supplied animation produces 1,310,800 bytes. Generated assets and binaries
stay under ignored `build/`; the source GIF is committed. There is no on-pedal
GIF decoder, decompression, RGB conversion, resizing or image mirroring. Rows
are stored in the exact RGB565 byte order needed by the display bus.

## Components ready for integration

- `animation.h`: bounded row-streaming player. It loops while active, honours
  frame durations with a wrap-safe millisecond clock, and returns `dismissed`
  immediately when any button is pressed. That press must not reach the normal
  patch controller. One poll reads/transmits at most one 320-byte row.
- `receiver.h`: independent upload receiver and active-asset `Source`. It uses
  two slots and never overwrites the active slot. Chunks are acknowledged before
  the host sends more. It validates the asset geometry, durations and transfer
  CRC, flushes a final commit marker, reads back and verifies the stored asset,
  then activates it. Boot validation and commit readback are incremental, with
  at most 512 bytes per CRC read. Individual SD calls may still block.
- `board.h`: SD/SdFat slot backend, with persistent reader handles, and an output
  adapter that reuses the existing `write_display_segment` driver. Reserved files
  are `/cntrl-splash-0.bin` and `/cntrl-splash-1.bin`, separate from show files.
  The application still initializes SD and sets `SdSlots.ready`.
- `cli.py`: Pydantic/Tyro preparation and chunked serial-upload commands. The
  uploader reuses the existing serial connection class without modifying it.

The adapter treats panel indices 0/2 as outer and 1 as centre. Confirm physical
ordering when wiring it into the target.

### Work deliberately deferred until the other agent finishes

1. Add the splash asset selection/enabling to the show configuration, using the
   other agent's final schema. Do not add parallel firmware build settings.
2. Instantiate `SdSlots`, `Receiver`, `Panels` and `Player` in the target, and
   connect replies to the existing nonblocking USB reply mechanism.
3. After SD initialization, call `Receiver.boot()` and poll until validation
   finishes. Start the player if the selected splash is available; otherwise
   enter the normal interface. Boot validation is incremental so scans/USB can
   continue while SD reads are serviced.
4. Dispatch complete `CNTRL SPLASH` command lines to this receiver, and route
   its binary payload exclusively while receiving. Do not feed splash bytes to
   the show receiver or vice versa. Keep calling `poll(now)` during CRC readback
   and boot; `accepts_input()` distinguishes those phases from receiving data.
5. Pause normal rendering while the splash owns the panels. Continue scanning
   buttons, servicing USB and polling the player. On dismissal consume the
   triggering press, stop playback, **invalidate all three normal-renderer pixel
   caches**, then redraw the interface. Merely calling `show()` is insufficient
   because the splash overwrote pixels outside those caches. Stop playback before
   replacing an asset, so it cannot read across active-slot changes.
6. To preserve 10 fps, extend the shared SPI driver to broadcast the identical
   outer-screen rows with both outer chip selects asserted together. This was
   not added here because that would edit existing code. Our output mask already
   expresses that operation. Keep all panels deselected before button scanning.
7. Build/inspect the actual target, upload it and verify on the pedal. Do **not**
   upload the compile-check sketch in this directory.

At 8 MHz, three separate full-panel transfers need at least 122.88 ms of wire
time per animation frame, before commands and SD access. The existing-driver
adapter is therefore limited to less than 8.14 fps for full redraws. Broadcasting
the two outer panels reduces this to 81.92 ms, making the original 10 fps a
plausible target that still needs physical qualification. If a transfer exceeds
a frame's duration, the player finishes that frame and starts the next without
an extra delay; it does not skip frames or issue catch-up bursts.

## Upload protocol, pending firmware integration

Do not run this command against the current pedal firmware:

```bash
uv run --frozen -m cntrl.maker.morningstar.mc.splash.cli upload \
  cntrl/maker/morningstar/mc/splash/build/spiral.cnsa \
  --port /dev/cu.usbmodem144682801
```

The host validates the prepared asset before opening serial. All messages are
newline-terminated ASCII, except the known-length binary chunks:

1. Host: `CNTRL SPLASH BEGIN SIZE CRC32_HEX`
2. Pedal: `CNTRL SPLASH SEND`
3. Host: up to 512 raw bytes, then waits for `CNTRL SPLASH NEXT RECEIVED`.
4. After the final bytes and durable readback, pedal:
   `CNTRL SPLASH DONE SIZE CRC32_HEX`.
5. Host queries `CNTRL SPLASH STATUS` and verifies
   `CNTRL SPLASH READY SIZE CRC32_HEX`.

Failures use `CNTRL SPLASH ERROR REASON`. Incomplete transfers expire after
10 seconds without incoming data. The previous committed asset remains usable.
FAT/SD power-loss behaviour still requires hardware testing; simulated write
failures do not prove filesystem-level atomicity.

## Binary formats

All metadata is little-endian. RGB565 pixels themselves are big-endian, in bus
order. The 16-byte asset header is magic `CNSA`, u16 version 1, u16 width 160,
u16 height 128, u16 frame count, u32 total bytes. Every frame is u32 duration in
milliseconds followed by 128 pairs of rows: normal outer-screen row, then
pre-mirrored centre row. Each row is 320 bytes. Every frame is independent.

Each SD slot wraps the entire asset with a 16-byte header (`CNSP`, u32 generation,
u32 asset bytes, u32 whole-asset CRC32) and a final four-byte `DONE` marker.
Boot chooses the newest CRC-valid committed generation. No vendor data or show
files are written by this backend.

## Verification

```bash
uv run --frozen pytest cntrl/maker/morningstar/mc/splash/test/test_asset.py
make -C cntrl/maker/morningstar/mc/splash test check-board
```

Python tests exercise the byte-accurate mirrored orientation, frame timing,
invalid assets and actual Python-to-C++ uploads through a pseudo-terminal,
including a 16-frame asset. Native C++ tests cover row bounds, timer wraparound,
looping, dismissal, read failures, CRC rejection, interrupted writes, reboot
selection and timeouts under address/undefined-behaviour sanitizers.
`check-board` compiles the adapters against the installed Teensy/SD interfaces in
a separate build directory. It is an API check only, not executable firmware
for this pedal. No hardware access occurs in these tests.
